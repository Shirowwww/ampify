#include "studio_stream.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "vocal_splitter.hpp"

namespace ampify::sing {

namespace {

constexpr double kFadeSeconds = 0.01;    // between the song as it is and processed, and in and out of a pause
constexpr double kVoiceSeconds = 0.05;   // a change of voice is spread over this
constexpr double kChunkSeconds = 0.1;    // the audio thread hands blocks to the rings in pieces this long at most
constexpr double kTailSeconds = 0.25;    // the most of the player's last sound a paused stream takes in
constexpr float kMinWeight = 1e-6f;      // where no take weighs anything yet, the vocals are 0
constexpr float kSilent = 1e-7f;         // a paused player's block below this everywhere is not its sound

int ValidRate(int rate) { return rate > 0 ? rate : kModelRate; }

// Whole STFT hops of the model, from one to half its window: the two hops taken must fit in it.
size_t HopHops(double hopSeconds, const ModelSpec& spec) {
    const double hops = std::isfinite(hopSeconds) ? hopSeconds * kModelRate / static_cast<double>(spec.fftHop) : 0.0;
    return std::clamp<size_t>(static_cast<size_t>(std::max(0.0, std::round(hops))), 1, (spec.frames - 1) / 2);
}

// Whole STFT hops the vocals are taken before the window's end, leaving the two hops taken inside the window.
size_t AheadHops(const ModelSpec& spec, size_t hopHops) {
    const double hops = spec.aheadSeconds * kModelRate / static_cast<double>(spec.fftHop);
    const size_t most = spec.frames - 1 - std::min(spec.frames - 1, 2 * hopHops);
    return std::min(static_cast<size_t>(std::max(0.0, std::round(hops))), most);
}

bool Silent(const float* interleaved, size_t frames) {
    return std::all_of(interleaved, interleaved + frames * 2, [](float s) { return std::fabs(s) < kSilent; });
}

}  // namespace

StudioStream::StudioStream(int rate, const ModelSpec& spec)
    : StudioStream(rate, spec.delaySeconds, spec.hopSeconds, spec) {}

StudioStream::StudioStream(int rate, double delaySeconds, double hopSeconds, const ModelSpec& spec)
    : m_spec(spec),
      m_rate(ValidRate(rate)),
      m_hopHops(HopHops(hopSeconds, spec)),
      m_hop44(m_hopHops * spec.fftHop),
      m_ahead44(AheadHops(spec, m_hopHops) * spec.fftHop),
      m_down(m_rate, kModelRate),
      m_up(kModelRate, m_rate) {
    const std::int64_t hop44 = static_cast<std::int64_t>(m_hop44);
    m_hop = (hop44 * m_rate + kModelRate - 1) / kModelRate;
    m_latency = m_down.Latency() + m_up.OutputLatency();
    m_fade = std::max<std::int64_t>(1, std::llround(kFadeSeconds * m_rate));
    m_chunk = std::max<std::int64_t>(1, std::llround(kChunkSeconds * m_rate));
    m_tail = std::llround(kTailSeconds * m_rate);
    const std::int64_t ahead = (static_cast<std::int64_t>(m_ahead44) * m_rate + kModelRate - 1) / kModelRate;
    const std::int64_t shortest = 2 * m_hop + ahead + m_latency + m_fade + 2;
    const double asked = std::isfinite(delaySeconds) ? delaySeconds * m_rate : 0.0;
    m_delay = std::max<std::int64_t>(std::llround(std::max(0.0, asked)), shortest);
    m_voiceStep = 1.0 / (kVoiceSeconds * m_rate);
    m_capacity = m_delay + m_tail + 4 * m_hop + m_latency + 2 * m_chunk;
    m_dry.assign(static_cast<size_t>(m_capacity) * 2, 0.0f);
    m_vocals.assign(static_cast<size_t>(m_capacity) * 2, 0.0f);
    m_readAt = -m_delay;
    m_played.store(m_readAt);

    m_in.assign(static_cast<size_t>(m_hop) * 2, 0.0f);
    m_in44.assign(m_down.MaxOutput(static_cast<size_t>(m_hop)) * 2, 0.0f);
    const size_t history = m_spec.Chunk() + m_hop44 + m_down.MaxOutput(static_cast<size_t>(m_hop));
    m_left.assign(history, 0.0f);
    m_right.assign(history, 0.0f);
    m_fill = m_spec.Chunk() - m_ahead44;
    m_windowLeft.assign(m_spec.Chunk(), 0.0f);
    m_windowRight.assign(m_spec.Chunk(), 0.0f);
    m_sum.assign(m_hop44 * 4, 0.0f);
    m_weight.assign(m_hop44 * 2, 0.0f);
    m_fadeIn.resize(m_hop44 * 2);
    const double last = static_cast<double>(m_fadeIn.size() - 1);
    for (size_t i = 0; i < m_fadeIn.size(); i++) {
        m_fadeIn[i] = static_cast<float>(0.5 - 0.5 * std::cos(2 * std::numbers::pi * static_cast<double>(i) / last));
    }
    m_final.assign(m_hop44 * 2, 0.0f);
    m_upOut.assign(m_up.MaxOutput(m_hop44) * 2, 0.0f);
}

StudioStream::~StudioStream() = default;

void StudioStream::Flush(Fade fade, double lagSeconds) {
    const double lag = std::isfinite(lagSeconds) ? lagSeconds * m_rate : 0.0;
    m_flushFade.store(fade, std::memory_order_relaxed);
    m_flushLag.store(std::clamp<std::int64_t>(std::llround(std::max(0.0, lag)), m_delay, m_delay + m_tail),
                     std::memory_order_relaxed);
    m_flushes.fetch_add(1, std::memory_order_release);
}

bool StudioStream::Buffering() const {
    return m_flushes.load(std::memory_order_acquire) != m_epoch.load(std::memory_order_acquire) ||
           m_buffering.load(std::memory_order_relaxed);
}

double StudioStream::Held() const {
    if (m_flushes.load(std::memory_order_acquire) != m_epoch.load(std::memory_order_acquire)) return 0;
    const std::int64_t heard =
        std::max(m_played.load(std::memory_order_acquire), m_epochStart.load(std::memory_order_relaxed));
    return static_cast<double>(std::max<std::int64_t>(0, m_written.load(std::memory_order_acquire) - heard)) / m_rate;
}

double StudioStream::Lag() const {
    if (m_flushes.load(std::memory_order_acquire) != m_epoch.load(std::memory_order_acquire)) {
        return static_cast<double>(m_flushLag.load(std::memory_order_relaxed)) / m_rate;
    }
    const std::int64_t lag = m_written.load(std::memory_order_acquire) - m_played.load(std::memory_order_acquire);
    return static_cast<double>(std::max<std::int64_t>(0, lag)) / m_rate;
}

double StudioStream::Delay() const { return static_cast<double>(m_delay) / m_rate; }

std::uint64_t StudioStream::Misses() const { return m_misses.load(std::memory_order_relaxed); }

// ---- the audio thread

void StudioStream::Process(float* interleaved, size_t frames, double voice, bool paused) {
    if (interleaved == nullptr || frames == 0) return;
    TakeFlush();
    const double target = std::isfinite(voice) ? std::clamp(voice, 0.0, 1.0) : 1.0;
    while (frames > 0) {
        const size_t count = std::min(frames, static_cast<size_t>(m_chunk));
        const std::int64_t live = m_written.load(std::memory_order_relaxed);  // this thread is its only writer
        const bool taken = Write(interleaved, count, paused);
        Read(interleaved, count, taken ? live : -1, target, paused);
        interleaved += count * 2;
        frames -= count;
    }
}

void StudioStream::TakeFlush() {
    const std::uint64_t asked = m_flushes.load(std::memory_order_acquire);
    if (asked == m_audioEpoch) return;
    m_audioEpoch = asked;
    const std::int64_t written = m_written.load(std::memory_order_relaxed);
    const Fade fade = m_flushFade.load(std::memory_order_relaxed);
    m_fadeLive = fade == Fade::Live;
    const std::int64_t heard = m_readAt;  // the next frame that would have been heard
    m_readAt = written - m_flushLag.load(std::memory_order_relaxed);
    m_fadeShift = heard - m_readAt;
    // Nothing to fade while the last epoch was still silent.
    m_fadeOutFrom = fade == Fade::None ? -1 : m_fadeLive ? written : heard >= m_start ? heard : -1;
    m_start = written;
    m_mix = 0;
    m_buffering.store(true, std::memory_order_relaxed);
    m_played.store(m_readAt, std::memory_order_release);
    m_epochStart.store(m_start, std::memory_order_relaxed);
    m_epoch.store(asked, std::memory_order_release);  // the worker reads m_epochStart after seeing it
}

// Takes a block in; paused, only the player's last sound, and no more than the ring keeps for it.
bool StudioStream::Write(const float* interleaved, size_t frames, bool paused) {
    const std::int64_t first = m_written.load(std::memory_order_relaxed);
    if (paused) {
        if (Silent(interleaved, frames)) return false;
        if (first + static_cast<std::int64_t>(frames) - m_readAt > m_delay + m_tail) return false;
    }
    for (size_t i = 0; i < frames; i++) {
        const size_t slot = static_cast<size_t>((first + static_cast<std::int64_t>(i)) % m_capacity) * 2;
        m_dry[slot] = interleaved[i * 2];
        m_dry[slot + 1] = interleaved[i * 2 + 1];
    }
    m_written.store(first + static_cast<std::int64_t>(frames), std::memory_order_release);  // publishes them
    return true;
}

double StudioStream::NextVoice(double voice) {
    if (m_voice < 0) m_voice = voice;
    m_voice = voice > m_voice ? std::min(voice, m_voice + m_voiceStep) : std::max(voice, m_voice - m_voiceStep);
    return m_voice;
}

// Fills `frames` with what is heard. `live` is the first of the frames this block took in, below 0 when none.
void StudioStream::Read(float* interleaved, size_t frames, std::int64_t live, double voice, bool paused) {
    // End before begin: a worker restart stores begin first, so a new end always comes with its begin.
    const std::int64_t end = m_vocalEnd.load(std::memory_order_acquire);
    const std::int64_t begin = m_vocalBegin.load(std::memory_order_acquire);
    const double step = 1.0 / static_cast<double>(m_fade);
    const double gainTo = paused ? 0.0 : 1.0;
    bool missed = false;
    size_t i = 0;
    for (; i < frames; i++) {
        if (paused && m_gain <= 0) break;  // faded out: the reading waits here until the player plays
        const std::int64_t at = m_readAt++;
        const double gain = (1 - NextVoice(voice)) * m_spec.compensate;
        double left = 0, right = 0;
        if (at < m_start) {
            // Silent from a flush on, after a fade of the song as it was heard: a cut mid-wave clicks.
            const std::int64_t source = m_fadeLive ? (live >= 0 ? live + static_cast<std::int64_t>(i) : -1)
                                                   : at + m_fadeShift;
            const std::int64_t into = source - m_fadeOutFrom;
            if (source >= 0 && m_fadeOutFrom >= 0 && into >= 0 && into < m_fade) {
                const double fade = 1.0 - static_cast<double>(into + 1) / static_cast<double>(m_fade);
                const size_t slot = static_cast<size_t>(source % m_capacity) * 2;
                left = m_dry[slot] * fade;
                right = m_dry[slot + 1] * fade;
            }
        } else {
            // Processed only while the vocals reach a fade ahead, so a fade back to the song always has vocals.
            const bool ready = at >= begin && at + m_fade < end;
            missed = missed || !ready;
            m_mix = ready ? std::min(1.0, m_mix + step) : std::max(0.0, m_mix - step);
            const size_t slot = static_cast<size_t>(at % m_capacity) * 2;
            const double take = at >= begin && at < end ? m_mix * gain : 0.0;
            left = m_dry[slot] - take * m_vocals[slot];
            right = m_dry[slot + 1] - take * m_vocals[slot + 1];
        }
        m_gain = gainTo > m_gain ? std::min(gainTo, m_gain + step) : std::max(gainTo, m_gain - step);
        interleaved[i * 2] = static_cast<float>(left * m_gain);
        interleaved[i * 2 + 1] = static_cast<float>(right * m_gain);
    }
    std::fill(interleaved + i * 2, interleaved + frames * 2, 0.0f);
    m_played.store(m_readAt, std::memory_order_release);  // the worker may overwrite the vocals below it
    m_buffering.store(m_readAt <= m_start, std::memory_order_relaxed);
    if (missed) m_misses.fetch_add(1, std::memory_order_relaxed);
}

// ---- the worker thread

bool StudioStream::Work(VocalSplitter& splitter) {
    const std::uint64_t epoch = m_epoch.load(std::memory_order_acquire);
    if (epoch != m_workerEpoch) {
        m_workerEpoch = epoch;
        Restart(m_epochStart.load(std::memory_order_relaxed));
    }
    const std::int64_t written = m_written.load(std::memory_order_acquire);
    if (written + m_chunk > m_read + m_capacity) Restart(written);  // so late its sound was written over
    if (splitter.Spec().id != m_spec.id) return false;  // a stream of another model: never fed that one
    const size_t full = m_spec.Chunk() + m_hop44;
    if (m_fill >= full) return RunWindow(splitter);
    if (!ReadSong()) return false;
    if (m_fill >= full) RunWindow(splitter);
    return true;
}

void StudioStream::Rest() { m_spectrum = {}; }

void StudioStream::Restart(std::int64_t origin) {
    m_down.Reset();
    m_up.Reset();
    std::fill(m_left.begin(), m_left.end(), 0.0f);
    std::fill(m_right.begin(), m_right.end(), 0.0f);
    m_fill = m_spec.Chunk() - m_ahead44;  // a window of silence before the song, less the ahead
    std::fill(m_sum.begin(), m_sum.end(), 0.0f);
    std::fill(m_weight.begin(), m_weight.end(), 0.0f);
    m_read = m_origin = origin;
    m_upCount = 0;
    m_windows = 0;
    m_vocalBegin.store(origin, std::memory_order_release);
    m_vocalEnd.store(origin, std::memory_order_release);
}

// Takes up to a hop of new song out of the ring, at the model's rate. The audio thread never waits, so it may have
// written over it meanwhile: then the analysis starts again from the newest sound.
bool StudioStream::ReadSong() {
    const std::int64_t count = std::min(m_written.load(std::memory_order_acquire) - m_read, m_hop);
    if (count <= 0) return false;
    for (std::int64_t i = 0; i < count; i++) {
        const size_t slot = static_cast<size_t>((m_read + i) % m_capacity) * 2;
        m_in[static_cast<size_t>(i) * 2] = m_dry[slot];
        m_in[static_cast<size_t>(i) * 2 + 1] = m_dry[slot + 1];
    }
    const std::int64_t now = m_written.load(std::memory_order_acquire);
    if (now + m_chunk > m_read + m_capacity) {
        Restart(now);
        return true;
    }
    m_read += count;
    const size_t made = m_down.Process(m_in.data(), static_cast<size_t>(count), m_in44.data());
    for (size_t i = 0; i < made; i++) {
        m_left[m_fill + i] = m_in44[i * 2];
        m_right[m_fill + i] = m_in44[i * 2 + 1];
    }
    m_fill += made;
    return true;
}

// One model window, a hop later than the last. Its last two hops before the ahead are taken with a Hann fade over both:
// the older one, now faded in on top of the previous window's fade out, is finished; the newer waits for the next
// window. The silence before the song is shorter by the ahead, so the first window still finishes the hop before it.
bool StudioStream::RunWindow(VocalSplitter& splitter) {
    // The vocals this hop finishes end below here; their slots must hold nothing the audio thread may still read.
    const std::int64_t publishEnd = m_origin + m_upCount - m_latency + m_hop + 1;
    if (publishEnd > m_played.load(std::memory_order_acquire) + m_capacity) return false;
    const auto hop = static_cast<std::ptrdiff_t>(m_hop44);
    const auto filled = static_cast<std::ptrdiff_t>(m_fill);
    std::copy(m_left.begin() + hop, m_left.begin() + filled, m_left.begin());
    std::copy(m_right.begin() + hop, m_right.begin() + filled, m_right.begin());
    m_fill -= m_hop44;
    const size_t from = m_spec.Chunk() - m_ahead44 - 2 * m_hop44;
    const size_t shift = m_windows == 0 ? VocalSplitter::kNewWindow : m_hopHops;
    if (!splitter.Vocals(m_left.data(), m_right.data(), m_spectrum, shift, from, m_windowLeft.data(),
                         m_windowRight.data())) {
        Restart(m_read);
        return true;
    }
    std::copy(m_sum.begin() + hop * 2, m_sum.end(), m_sum.begin());
    std::fill(m_sum.begin() + hop * 2, m_sum.end(), 0.0f);
    std::copy(m_weight.begin() + hop, m_weight.end(), m_weight.begin());
    std::fill(m_weight.begin() + hop, m_weight.end(), 0.0f);
    for (size_t i = 0; i < 2 * m_hop44; i++) {
        m_sum[i * 2] += m_windowLeft[from + i] * m_fadeIn[i];
        m_sum[i * 2 + 1] += m_windowRight[from + i] * m_fadeIn[i];
        m_weight[i] += m_fadeIn[i];
    }
    for (size_t i = 0; i < m_hop44; i++) {
        const float weight = std::max(m_weight[i], kMinWeight);
        m_final[i * 2] = m_sum[i * 2] / weight;
        m_final[i * 2 + 1] = m_sum[i * 2 + 1] / weight;
    }
    // The first window finishes the hop before the song: the upsampler starts with the song's first sample, so its
    // frames and the song's stay a whole number apart at any rate.
    if (++m_windows >= 2) Publish(m_up.Process(m_final.data(), m_hop44, m_upOut.data()));
    return true;
}

// The upsampler's frame m is the vocals of the song's frame m_origin + m, late by the resamplers' round trip.
void StudioStream::Publish(size_t frames) {
    std::int64_t end = m_vocalEnd.load(std::memory_order_relaxed);  // this thread is its only writer
    for (size_t i = 0; i < frames; i++) {
        const std::int64_t at = m_origin + m_upCount + static_cast<std::int64_t>(i) - m_latency;
        if (at < m_origin) continue;
        const size_t slot = static_cast<size_t>(at % m_capacity) * 2;
        m_vocals[slot] = m_upOut[i * 2];
        m_vocals[slot + 1] = m_upOut[i * 2 + 1];
        end = at + 1;
    }
    m_upCount += static_cast<std::int64_t>(frames);
    m_vocalEnd.store(end, std::memory_order_release);  // the audio thread reads the vocals below it
}

}  // namespace ampify::sing
