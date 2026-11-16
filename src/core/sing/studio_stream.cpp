#include "studio_stream.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>

#include "vocal_splitter.hpp"

namespace ampify::sing {

namespace {

constexpr double kFadeSeconds = 0.01;    // between the song as it is and processed
constexpr double kVoiceSeconds = 0.05;   // a change of voice is spread over this
constexpr double kChunkSeconds = 0.1;    // the audio thread hands blocks to the rings in pieces this long at most
constexpr float kMinWeight = 1e-6f;      // where no take weighs anything yet, the vocals are 0

int ValidRate(int rate) { return rate > 0 ? rate : kModelRate; }

// A hop is a whole number of 1 / gcd(rate, 44100) seconds, so it is a whole number of samples on both sides.
std::int64_t HopUnits(int rate, double hopSeconds) {
    const std::int64_t unit = std::gcd(rate, kModelRate);
    const std::int64_t most = static_cast<std::int64_t>(kChunk / 2) * unit / kModelRate;  // two hops in a window
    const std::int64_t asked = std::isfinite(hopSeconds) ? std::llround(hopSeconds * unit) : 0;
    return std::clamp<std::int64_t>(asked, 1, most);
}

}  // namespace

StudioStream::StudioStream(int rate, double delaySeconds, double hopSeconds)
    : m_rate(ValidRate(rate)),
      m_down(m_rate, kModelRate),
      m_up(kModelRate, m_rate) {
    const std::int64_t unit = std::gcd(m_rate, kModelRate);
    const std::int64_t hopUnits = HopUnits(m_rate, hopSeconds);
    m_hop = hopUnits * m_rate / unit;
    m_hop44 = static_cast<size_t>(hopUnits * kModelRate / unit);
    m_latency = m_down.Latency() + m_up.OutputLatency();
    m_fade = std::max<std::int64_t>(1, std::llround(kFadeSeconds * m_rate));
    m_chunk = std::max<std::int64_t>(1, std::llround(kChunkSeconds * m_rate));
    const std::int64_t shortest = 2 * m_hop + m_latency + m_fade;
    const double asked = std::isfinite(delaySeconds) ? delaySeconds * m_rate : 0.0;
    m_delay = std::max<std::int64_t>(std::llround(std::max(0.0, asked)), shortest);
    m_voiceStep = 1.0 / (kVoiceSeconds * m_rate);
    m_capacity = m_delay + 4 * m_hop + m_latency + 2 * m_chunk;
    m_dry.assign(static_cast<size_t>(m_capacity) * 2, 0.0f);
    m_vocals.assign(static_cast<size_t>(m_capacity) * 2, 0.0f);
    m_played.store(-m_delay);

    m_hopIn.assign(static_cast<size_t>(m_hop) * 2, 0.0f);
    m_hopModel.assign(m_down.MaxOutput(static_cast<size_t>(m_hop)) * 2, 0.0f);
    m_historyLeft.assign(kChunk, 0.0f);
    m_historyRight.assign(kChunk, 0.0f);
    m_windowLeft.assign(kChunk, 0.0f);
    m_windowRight.assign(kChunk, 0.0f);
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

void StudioStream::Flush(bool heldWasHeard) {
    m_heldWasHeard.store(heldWasHeard, std::memory_order_relaxed);
    m_flushes.fetch_add(1, std::memory_order_release);
}

bool StudioStream::Buffering() const {
    return m_flushes.load(std::memory_order_acquire) != m_epoch.load(std::memory_order_acquire) ||
           m_buffering.load(std::memory_order_relaxed);
}

double StudioStream::Held() const {
    if (m_flushes.load(std::memory_order_acquire) != m_epoch.load(std::memory_order_acquire)) return 0;
    const std::int64_t since =
        m_written.load(std::memory_order_acquire) - m_epochStart.load(std::memory_order_relaxed);
    return static_cast<double>(std::clamp<std::int64_t>(since, 0, m_delay)) / m_rate;
}

double StudioStream::Delay() const { return static_cast<double>(m_delay) / m_rate; }

std::uint64_t StudioStream::Misses() const { return m_misses.load(std::memory_order_relaxed); }

// ---- the audio thread

void StudioStream::Process(float* interleaved, size_t frames, double voice) {
    if (interleaved == nullptr || frames == 0) return;
    TakeFlush();
    const double target = std::isfinite(voice) ? std::clamp(voice, 0.0, 1.0) : 1.0;
    while (frames > 0) {
        const size_t count = std::min(frames, static_cast<size_t>(m_chunk));
        ProcessChunk(interleaved, count, target);
        interleaved += count * 2;
        frames -= count;
    }
}

void StudioStream::TakeFlush() {
    const std::uint64_t asked = m_flushes.load(std::memory_order_acquire);
    if (asked == m_audioEpoch) return;
    m_audioEpoch = asked;
    const std::int64_t written = m_written.load(std::memory_order_relaxed);
    m_fadeLive = !m_heldWasHeard.exchange(true, std::memory_order_relaxed);
    const std::int64_t cut = written - m_delay;  // the next frame that would have been heard
    // Nothing to fade while the last epoch was still silent.
    m_fadeOutFrom = m_fadeLive ? written : cut >= m_start ? cut : -1;
    m_start = written;
    m_mix = 0;
    m_buffering.store(true, std::memory_order_relaxed);
    m_epochStart.store(m_start, std::memory_order_relaxed);
    m_epoch.store(asked, std::memory_order_release);  // the worker reads m_epochStart after seeing it
}

double StudioStream::NextVoice(double voice) {
    if (m_voice < 0) m_voice = voice;
    m_voice = voice > m_voice ? std::min(voice, m_voice + m_voiceStep) : std::max(voice, m_voice - m_voiceStep);
    return m_voice;
}

void StudioStream::ProcessChunk(float* interleaved, size_t frames, double voice) {
    const std::int64_t first = m_written.load(std::memory_order_relaxed);  // this thread is its only writer
    for (size_t i = 0; i < frames; i++) {
        const size_t slot = static_cast<size_t>((first + static_cast<std::int64_t>(i)) % m_capacity) * 2;
        m_dry[slot] = interleaved[i * 2];
        m_dry[slot + 1] = interleaved[i * 2 + 1];
    }
    m_written.store(first + static_cast<std::int64_t>(frames), std::memory_order_release);  // publishes them
    // End before begin: a worker restart stores begin first, so a new end always comes with its begin.
    const std::int64_t end = m_vocalEnd.load(std::memory_order_acquire);
    const std::int64_t begin = m_vocalBegin.load(std::memory_order_acquire);
    const double step = 1.0 / static_cast<double>(m_fade);
    bool missed = false;
    for (size_t i = 0; i < frames; i++) {
        const std::int64_t at = first + static_cast<std::int64_t>(i) - m_delay;
        const double gain = (1 - NextVoice(voice)) * kCompensate;
        if (at < m_start) {
            // Silent from a flush on, after a fade of the song as it was heard: a cut mid-wave clicks.
            const std::int64_t source = m_fadeLive ? first + static_cast<std::int64_t>(i) : at;
            const std::int64_t into = source - m_fadeOutFrom;
            const bool fading = m_fadeOutFrom >= 0 && into >= 0 && into < m_fade;
            const float fade = fading ? static_cast<float>(1.0 - static_cast<double>(into + 1) / m_fade) : 0.0f;
            const size_t slot = fading ? static_cast<size_t>(source % m_capacity) * 2 : 0;
            interleaved[i * 2] = fading ? m_dry[slot] * fade : 0.0f;
            interleaved[i * 2 + 1] = fading ? m_dry[slot + 1] * fade : 0.0f;
            continue;
        }
        // Processed only while the vocals reach a fade ahead, so a fade back to the song always has vocals to use.
        const bool ready = at >= begin && at + m_fade < end;
        missed = missed || !ready;
        m_mix = ready ? std::min(1.0, m_mix + step) : std::max(0.0, m_mix - step);
        const size_t slot = static_cast<size_t>(at % m_capacity) * 2;
        const bool held = at >= begin && at < end;
        const double take = m_mix * gain;
        interleaved[i * 2] = static_cast<float>(m_dry[slot] - (held ? take * m_vocals[slot] : 0.0));
        interleaved[i * 2 + 1] = static_cast<float>(m_dry[slot + 1] - (held ? take * m_vocals[slot + 1] : 0.0));
    }
    const std::int64_t next = first + static_cast<std::int64_t>(frames) - m_delay;
    m_played.store(next, std::memory_order_release);  // the worker may overwrite the vocals below it
    m_buffering.store(next <= m_start, std::memory_order_relaxed);
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
    if (written - m_read < m_hop) return false;
    // The vocals this hop finishes end here; their slots must hold nothing the audio thread may still read.
    const std::int64_t publishEnd = m_origin + m_upCount - m_latency;
    if (publishEnd > m_played.load(std::memory_order_acquire) + m_capacity) return false;
    if (!ReadHop()) return true;
    if (!RunWindow(splitter)) Restart(m_read);
    return true;
}

void StudioStream::Restart(std::int64_t origin) {
    m_down.Reset();
    m_up.Reset();
    std::fill(m_historyLeft.begin(), m_historyLeft.end(), 0.0f);
    std::fill(m_historyRight.begin(), m_historyRight.end(), 0.0f);
    std::fill(m_sum.begin(), m_sum.end(), 0.0f);
    std::fill(m_weight.begin(), m_weight.end(), 0.0f);
    m_read = m_origin = origin;
    m_upCount = 0;
    m_vocalBegin.store(origin, std::memory_order_release);
    m_vocalEnd.store(origin, std::memory_order_release);
}

// Copies the next hop out of the ring. The audio thread never waits, so it may have written over it meanwhile: then
// the analysis starts again from the newest sound, and false.
bool StudioStream::ReadHop() {
    for (std::int64_t i = 0; i < m_hop; i++) {
        const size_t slot = static_cast<size_t>((m_read + i) % m_capacity) * 2;
        m_hopIn[static_cast<size_t>(i) * 2] = m_dry[slot];
        m_hopIn[static_cast<size_t>(i) * 2 + 1] = m_dry[slot + 1];
    }
    const std::int64_t now = m_written.load(std::memory_order_acquire);
    if (now + m_chunk > m_read + m_capacity) {
        Restart(now);
        return false;
    }
    m_read += m_hop;
    return true;
}

// One model window ending at the newest sample. Its last two hops are taken with a Hann fade over both: the older
// one, now faded in on top of the previous window's fade out, is finished; the newer waits for the next window.
bool StudioStream::RunWindow(VocalSplitter& splitter) {
    const size_t got = std::min(m_down.Process(m_hopIn.data(), static_cast<size_t>(m_hop), m_hopModel.data()), kChunk);
    std::copy(m_historyLeft.begin() + static_cast<std::ptrdiff_t>(got), m_historyLeft.end(), m_historyLeft.begin());
    std::copy(m_historyRight.begin() + static_cast<std::ptrdiff_t>(got), m_historyRight.end(), m_historyRight.begin());
    for (size_t i = 0; i < got; i++) {
        m_historyLeft[kChunk - got + i] = m_hopModel[i * 2];
        m_historyRight[kChunk - got + i] = m_hopModel[i * 2 + 1];
    }
    if (!splitter.Vocals(m_historyLeft.data(), m_historyRight.data(), m_windowLeft.data(), m_windowRight.data())) {
        return false;
    }
    const size_t hop = m_hop44;
    std::copy(m_sum.begin() + static_cast<std::ptrdiff_t>(hop * 2), m_sum.end(), m_sum.begin());
    std::fill(m_sum.begin() + static_cast<std::ptrdiff_t>(hop * 2), m_sum.end(), 0.0f);
    std::copy(m_weight.begin() + static_cast<std::ptrdiff_t>(hop), m_weight.end(), m_weight.begin());
    std::fill(m_weight.begin() + static_cast<std::ptrdiff_t>(hop), m_weight.end(), 0.0f);
    const size_t from = kChunk - 2 * hop;
    for (size_t i = 0; i < 2 * hop; i++) {
        m_sum[i * 2] += m_windowLeft[from + i] * m_fadeIn[i];
        m_sum[i * 2 + 1] += m_windowRight[from + i] * m_fadeIn[i];
        m_weight[i] += m_fadeIn[i];
    }
    for (size_t i = 0; i < hop; i++) {
        const float weight = std::max(m_weight[i], kMinWeight);
        m_final[i * 2] = m_sum[i * 2] / weight;
        m_final[i * 2 + 1] = m_sum[i * 2 + 1] / weight;
    }
    Publish(m_up.Process(m_final.data(), hop, m_upOut.data()));
    return true;
}

// The upsampler's frame m is the vocals of the song's frame m_origin + m, late by the finished hop's place (one hop
// behind the window's end) and the resamplers' round trip.
void StudioStream::Publish(size_t frames) {
    std::int64_t end = m_vocalEnd.load(std::memory_order_relaxed);  // this thread is its only writer
    for (size_t i = 0; i < frames; i++) {
        const std::int64_t at = m_origin + m_upCount + static_cast<std::int64_t>(i) - m_hop - m_latency;
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
