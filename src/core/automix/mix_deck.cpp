#include "mix_deck.hpp"

#include <algorithm>
#include <numeric>

#include "../audio/equalizer.hpp"
#include "tempo_stretch.hpp"

namespace ampify::automix {

namespace {

constexpr double kHistorySeconds = 1.0;
constexpr double kWindowSeconds = 0.25;  // of Apple's sound, looked for in the second player's
constexpr double kSearchSeconds = 0.4;   // on each side of where the clocks say it is
constexpr int kCoarseRate = 12000;       // the search runs on mono at about this rate, then at the stream's
constexpr double kMinMatch = 0.8;
constexpr double kQuiet = 1e-8;          // mean square under which there is nothing to recognise
constexpr size_t kChunk = 256;           // frames mixed at once: curves and speed move in steps this long
constexpr double kWarmSeconds = 0.5;     // the stretch runs this long at normal speed before it is heard
constexpr double kBlendSeconds = 0.05;   // then replaces the plain copy within this
constexpr double kLowHz = 150;           // the shelf that takes a song's bass away
constexpr double kLowFloor = 0.0316;     // -30 dB: "no lows"
constexpr double kMeasureSeconds = 0.25;
constexpr double kMaxSpeed = 1.2;
constexpr double kMaxPadSeconds = 0.2;   // of empty frames at the start of a stream: more is the song's own silence
constexpr double kGainSeconds = 0.02;   // new gains are reached this far ahead of what both streams have written
constexpr size_t kCurvePoints = 48;      // of the gains: half a second at the session's pace, streams 20 ms apart
// A stream's first blocks are written before Windows takes anything: they say nothing of how it is taken.
constexpr std::uint64_t kPreRollBlocks = 8;

struct Match {
    size_t offset = 0;  // where in the area the reference starts
    double match = 0;
    double scale = 1;
};

double Dot(const float* a, const float* b, size_t count) {
    double sum = 0;
    for (size_t i = 0; i < count; i++) sum += static_cast<double>(a[i]) * b[i];
    return sum;
}

std::vector<float> Thinned(const std::vector<float>& samples, size_t by) {
    std::vector<float> out(samples.size() / by);
    for (size_t i = 0; i < out.size(); i++) {
        float sum = 0;
        for (size_t k = 0; k < by; k++) sum += samples[i * by + k];
        out[i] = sum / static_cast<float>(by);
    }
    return out;
}

// The offset in [from, to] where `ref` lies best in `area`, by normalised correlation.
Match BestOffset(const std::vector<float>& ref, const std::vector<float>& area, size_t from, size_t to) {
    Match best;
    best.match = -1;
    const double refPower = Dot(ref.data(), ref.data(), ref.size());
    for (size_t offset = from; offset <= to; offset++) {
        const float* at = area.data() + offset;
        const double power = Dot(at, at, ref.size());
        if (power <= 0 || refPower <= 0) continue;
        const double dot = Dot(ref.data(), at, ref.size());
        const double match = dot / std::sqrt(refPower * power);
        if (match > best.match) best = {offset, match, dot / power};
    }
    return best;
}

// Where `ref` (Apple's sound) lies in `area` (the second player's): a first pass on thinned copies, then every
// offset around its answer at the full rate.
std::optional<Match> Find(const std::vector<float>& ref, const std::vector<float>& area, int sampleRate) {
    if (ref.empty() || area.size() < ref.size()) return std::nullopt;
    if (Dot(ref.data(), ref.data(), ref.size()) / static_cast<double>(ref.size()) < kQuiet) return std::nullopt;
    const size_t by = static_cast<size_t>(std::max(1, sampleRate / kCoarseRate));
    const std::vector<float> thinRef = Thinned(ref, by);
    const std::vector<float> thinArea = Thinned(area, by);
    if (thinRef.empty() || thinArea.size() < thinRef.size()) return std::nullopt;
    const Match coarse = BestOffset(thinRef, thinArea, 0, thinArea.size() - thinRef.size());
    if (coarse.match < 0) return std::nullopt;
    const size_t last = area.size() - ref.size();
    const size_t centre = coarse.offset * by;
    const Match fine = BestOffset(ref, area, centre > 2 * by ? centre - 2 * by : 0, std::min(last, centre + 2 * by));
    if (fine.match < kMinMatch) return std::nullopt;
    return fine;
}

double Lerp(double from, double to, double t) { return from + (to - from) * t; }

// How many frames of nothing a block starts with, or ends with.
size_t EmptyFrames(const float* stereo, size_t frames, bool fromEnd) {
    size_t count = 0;
    while (count < frames) {
        const size_t at = fromEnd ? frames - 1 - count : count;
        if (stereo[at * 2] != 0 || stereo[at * 2 + 1] != 0) break;
        count++;
    }
    return count;
}

DeckGains Lerp(const DeckGains& from, const DeckGains& to, double t) {
    return {Lerp(from.main, to.main, t), Lerp(from.tail, to.tail, t), Lerp(from.mainLow, to.mainLow, t),
            Lerp(from.tailLow, to.tailLow, t)};
}

}  // namespace

MixDeck::MixDeck() = default;
MixDeck::~MixDeck() = default;

void MixDeck::LowShelf::Clear() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }

// At 0 dB the shelf's output is its input and its state stays zero, so it comes in and goes out without a step.
void MixDeck::LowShelf::Run(float* stereo, size_t frames, double low, int sampleRate) {
    const double gainDb = 20 * std::log10(std::max(low, kLowFloor));
    const audio::BiquadCoefficients c =
        audio::BiquadCoefficients::Design(audio::FilterKind::LowShelf, kLowHz, 1.0, gainDb, sampleRate);
    for (int channel = 0; channel < 2; channel++) {
        double s1 = z1[channel], s2 = z2[channel];
        for (size_t i = 0; i < frames; i++) {
            const double x = stereo[i * 2 + channel];
            const double y = c.b0 * x + s1;
            s1 = c.b1 * x - c.a1 * y + s2;
            s2 = c.b2 * x - c.a2 * y;
            stereo[i * 2 + channel] = static_cast<float>(y);
        }
        z1[channel] = s1;
        z2[channel] = s2;
    }
}

void MixDeck::Open(int sampleRate, double holdSeconds) {
    if (sampleRate <= 0 || !(holdSeconds > 0)) return;
    // Made before the lock: the audio threads never wait for memory.
    const size_t hold = static_cast<size_t>(holdSeconds * sampleRate);
    std::vector<float> tail(hold * 2);
    std::vector<float> history(static_cast<size_t>(kHistorySeconds * sampleRate));
    std::vector<float> dry(kChunk * 2), wet(kChunk * 2), in(static_cast<size_t>(kChunk * kMaxSpeed + 2) * 2);
    std::vector<float> check(kChunk * 2);
    std::vector<Point> curve;
    curve.reserve(kCurvePoints * 2);
    curve.push_back({0, DeckGains{}});
    std::unique_ptr<TempoStretch> old;
    {
        std::lock_guard<std::mutex> guard(m_lock);
        m_rate = sampleRate;
        m_hold = hold;
        m_tail.swap(tail);
        m_history.swap(history);
        m_dry.swap(dry);
        m_wet.swap(wet);
        m_in.swap(in);
        m_check.swap(check);
        m_curve.swap(curve);
        m_tailFrames = m_tailSent = m_mainFrames = 0;
        m_mainFeed = m_tailFeed = Feed{};
        m_aligned = m_exact = false;
        m_apart = m_delay = 0;
        m_oldEnd.reset();
        m_emptyEnd = m_emptyHead = 0;
        m_headCounted = false;
        m_mainShelf.Clear();
        m_tailShelf.Clear();
        m_residualDb = NAN;
        m_residualError = m_residualPower = 0;
        m_residualFrames = 0;
        old = std::move(m_stretch);
        m_state = Stretch::Off;
        m_rateTo = 1;
        m_open = true;
    }
}

void MixDeck::Close() {
    std::vector<float> tail, history;
    std::unique_ptr<TempoStretch> stretch;
    {
        std::lock_guard<std::mutex> guard(m_lock);
        m_open = m_aligned = false;
        m_curve.assign(1, Point{});
        m_tail.swap(tail);
        m_history.swap(history);
        stretch = std::move(m_stretch);
        m_state = Stretch::Off;
    }
}

bool MixDeck::Feed::Note(const DeckBlock& block, std::uint64_t count, int sampleRate) {
    const bool fresh = stream != block.stream;
    if (fresh) {
        stream = block.stream;
        origin = count;
        blocks = 0;
    }
    const size_t at = static_cast<size_t>(blocks % kBlocks);
    taken[at] = static_cast<std::int64_t>(count - origin) - static_cast<std::int64_t>(block.queued);
    ahead[at] = static_cast<double>(taken[at]) - sampleRate * block.now;
    blocks++;
    return fresh;
}

bool MixDeck::Feed::Settled() const { return blocks >= kBlocks + kPreRollBlocks; }

// What Windows takes at once: every count of frames taken is a multiple of it.
std::int64_t MixDeck::Feed::Period() const {
    std::int64_t period = 0;
    for (std::int64_t value : taken) period = std::gcd(period, value - taken[0]);
    return period;
}

// The frames taken against the clock, right after Windows took some: the same instant for every stream.
double MixDeck::Feed::Ahead() const { return *std::max_element(ahead.begin(), ahead.end()); }

// How many frames further the second player's stream is than Apple's, counted as the deck counts them, at any one
// instant of the speakers. Windows has taken a whole number of periods from each stream, but the deck met Apple's
// stream on its way: each count is whole periods plus what it began at, which every block shows (`left`).
std::optional<std::int64_t> MixDeck::Apart() const {
    if (!m_mainFeed.Settled() || !m_tailFeed.Settled()) return std::nullopt;
    const std::int64_t period = std::gcd(m_mainFeed.Period(), m_tailFeed.Period());
    if (period <= 0) return std::nullopt;
    auto left = [period](const Feed& feed) { return ((feed.taken[0] % period) + period) % period; };
    const std::int64_t odd = left(m_tailFeed) - left(m_mainFeed);
    const double apart = m_tailFeed.Ahead() - m_mainFeed.Ahead() - static_cast<double>(odd);
    return std::llround(apart / static_cast<double>(period)) * period + odd +
           static_cast<std::int64_t>(m_tailFeed.origin) - static_cast<std::int64_t>(m_mainFeed.origin);
}

bool MixDeck::Audible() const {
    return m_curve.back().gains.tail > 0 || GainsAt(static_cast<std::int64_t>(m_tailSent)).tail > 0;
}

DeckGains MixDeck::GainsAt(std::int64_t at) const {
    if (at >= m_curve.back().at) return m_curve.back().gains;
    for (size_t i = m_curve.size() - 1; i > 0; i--) {
        const Point& from = m_curve[i - 1];
        const Point& to = m_curve[i];
        if (at >= from.at) {
            return Lerp(from.gains, to.gains, static_cast<double>(at - from.at) / static_cast<double>(to.at - from.at));
        }
    }
    return m_curve.front().gains;
}

// Shortens a chunk so that it ends at the next point of the gains: between two points they move in a straight line.
size_t MixDeck::UntilPoint(std::int64_t at, size_t frames) const {
    for (const Point& point : m_curve) {
        if (point.at > at) return static_cast<size_t>(std::min<std::int64_t>(static_cast<std::int64_t>(frames), point.at - at));
    }
    return frames;
}

std::optional<DeckStep> MixDeck::Align(double leadHint, double aTime, double volume) {
    int openRate = 0;
    {
        std::lock_guard<std::mutex> guard(m_lock);
        openRate = m_rate;
    }
    // Sized before the lock is taken for good: the audio threads wait for it, never for memory.
    std::vector<float> ref, area;
    ref.reserve(static_cast<size_t>(kWindowSeconds * openRate));
    area.reserve(ref.capacity() + 2 * static_cast<size_t>(kSearchSeconds * openRate));
    std::uint64_t refEnd = 0;
    std::int64_t areaStart = 0;
    std::int64_t apart = 0;
    std::uint64_t mainStream = 0;
    std::uint64_t tailStream = 0;
    size_t window = 0;
    int rate = 0;
    {
        std::lock_guard<std::mutex> guard(m_lock);
        if (!m_open) return std::nullopt;
        if (m_aligned && Audible()) return DeckStep{(static_cast<double>(m_tailFrames) - m_cursor) / m_rate, 1};
        const std::optional<std::int64_t> streams = Apart();
        if (!streams) return std::nullopt;
        apart = *streams;
        mainStream = m_mainFeed.stream;
        tailStream = m_tailFeed.stream;
        rate = m_rate;
        window = static_cast<size_t>(kWindowSeconds * rate);
        const auto search = static_cast<std::int64_t>(kSearchSeconds * rate);
        if (m_mainFrames < window || m_tailFrames < window) return std::nullopt;
        const auto newest = static_cast<std::int64_t>(m_tailFrames);
        const std::int64_t guess = newest - std::llround(leadHint * rate) - static_cast<std::int64_t>(window);
        const std::int64_t oldest = std::max<std::int64_t>(0, newest - static_cast<std::int64_t>(m_hold));
        const std::int64_t low = std::max(oldest, guess - search);
        const std::int64_t high = std::min(newest - static_cast<std::int64_t>(window), guess + search);
        if (high < low) return std::nullopt;
        ref.resize(window);
        for (size_t i = 0; i < window; i++) {
            ref[i] = m_history[(m_mainFrames - window + i) % m_history.size()];
        }
        area.resize(static_cast<size_t>(high - low) + window);
        for (size_t i = 0; i < area.size(); i++) {
            const size_t at = static_cast<size_t>((static_cast<std::uint64_t>(low) + i) % m_hold) * 2;
            area[i] = (m_tail[at] + m_tail[at + 1]) * 0.5f;
        }
        refEnd = m_mainFrames;
        areaStart = low;
    }
    const std::optional<Match> found = Find(ref, area, rate);
    if (!found) return std::nullopt;
    std::lock_guard<std::mutex> guard(m_lock);
    if (!m_open || m_rate != rate || (m_aligned && Audible())) return std::nullopt;
    if (m_mainFeed.stream != mainStream || m_tailFeed.stream != tailStream) return std::nullopt;
    // Apple's frame n is the same samples as frame n + delay of the second player's sound. What the second
    // player's stream is handed now leaves the speakers with Apple's frame m_tailSent - apart: the deck's copy of
    // that very frame is what must go out now.
    const std::int64_t delay = areaStart + static_cast<std::int64_t>(found->offset) -
                               (static_cast<std::int64_t>(refEnd) - static_cast<std::int64_t>(window));
    const std::int64_t heard = static_cast<std::int64_t>(m_tailSent) - apart;
    const std::int64_t lead = static_cast<std::int64_t>(m_tailFrames) - (heard + delay);
    const DeckStep step{static_cast<double>(lead) / rate, found->match};
    if (lead <= 0 || static_cast<size_t>(lead) >= m_hold) return step;
    m_delay = delay;
    m_apart = apart;
    m_exact = true;
    m_cursor = static_cast<double>(heard + delay);
    m_pos = aTime + static_cast<double>(heard - static_cast<std::int64_t>(refEnd)) / rate;
    m_scale = found->scale;
    m_alignVolume = m_volume = volume;
    m_aligned = true;
    m_state = Stretch::Off;
    m_residualDb = NAN;
    m_residualError = m_residualPower = 0;
    m_residualFrames = 0;
    return step;
}

void MixDeck::SetStretch(double from, double first, double rate) {
    int sampleRate = 0;
    {
        std::lock_guard<std::mutex> guard(m_lock);
        sampleRate = m_rate;
    }
    const bool wanted = rate != 1 && first > from && sampleRate > 0;
    std::unique_ptr<TempoStretch> stretch = wanted ? std::make_unique<TempoStretch>(sampleRate) : nullptr;
    std::lock_guard<std::mutex> guard(m_lock);
    if (!m_open || m_rate != sampleRate || m_state != Stretch::Off) return;
    m_stretch.swap(stretch);  // the old one, if any, goes after the lock
    m_from = from;
    m_rateTo = wanted ? rate : 1;
    m_rampFrames = wanted ? static_cast<std::uint64_t>(std::llround(2 * (first - from) / (1 + rate) * sampleRate)) : 0;
}

void MixDeck::SetGains(const DeckGains& gains, double volume) {
    std::lock_guard<std::mutex> guard(m_lock);
    m_volume = volume;
    if (!m_open) return;
    const Point last = m_curve.back();
    if (last.gains.main == gains.main && last.gains.tail == gains.tail && last.gains.mainLow == gains.mainLow &&
        last.gains.tailLow == gains.tailLow) {
        return;
    }
    // From where neither stream has written yet, so that both play the very same line.
    const std::int64_t written = std::max(static_cast<std::int64_t>(m_tailSent),
                                          static_cast<std::int64_t>(m_mainFrames) + m_apart);
    const std::int64_t from = std::max(written, last.at);
    if (from > last.at) m_curve.push_back({from, last.gains});
    m_curve.push_back({from + std::max<std::int64_t>(1, std::llround(kGainSeconds * m_rate)), gains});
    if (m_curve.size() > kCurvePoints) {
        m_curve.erase(m_curve.begin(), m_curve.begin() + static_cast<std::ptrdiff_t>(m_curve.size() - kCurvePoints));
    }
}

// Where the old stream's last frame was heard is known to the sample, and so is where the new stream's first one
// is, once Windows has taken enough blocks of it: no clock of either player comes into it.
std::optional<double> MixDeck::SwitchGap() const {
    std::lock_guard<std::mutex> guard(m_lock);
    if (!m_open || !m_oldEnd || !m_headCounted) return std::nullopt;
    // More than the player's own padding: the song itself starts silent, and its first sound tells nothing.
    if (m_emptyHead > static_cast<std::uint64_t>(kMaxPadSeconds * m_rate)) return std::nullopt;
    const std::optional<std::int64_t> apart = Apart();
    if (!apart) return std::nullopt;
    const std::int64_t first = static_cast<std::int64_t>(m_mainFeed.origin + m_emptyHead) + *apart;
    return static_cast<double>(first - *m_oldEnd) / m_rate;
}

std::optional<double> MixDeck::Time() const {
    std::lock_guard<std::mutex> guard(m_lock);
    if (!m_open || !m_aligned) return std::nullopt;
    return m_pos;
}

double MixDeck::TailSeconds() const {
    std::lock_guard<std::mutex> guard(m_lock);
    return m_open && m_rate > 0 ? static_cast<double>(m_tailFrames) / m_rate : 0;
}

double MixDeck::ResidualDb() const {
    std::lock_guard<std::mutex> guard(m_lock);
    return m_residualDb;
}

// False when the ring does not hold all of [from, from + frames): not received yet, or already written over.
bool MixDeck::ReadTail(std::uint64_t from, size_t frames, float* out) const {
    if (from + frames > m_tailFrames || from + m_hold < m_tailFrames) return false;
    for (size_t done = 0; done < frames;) {
        const size_t at = static_cast<size_t>((from + done) % m_hold);
        const size_t count = std::min(frames - done, m_hold - at);
        std::copy_n(m_tail.begin() + at * 2, count * 2, out + done * 2);
        done += count;
    }
    return true;
}

// Shortens a chunk so that it ends where the stretch warms up, starts to move, or reaches its speed.
size_t MixDeck::UntilEvent(size_t frames) {
    if (!m_stretch) return frames;
    auto until = [&](double aTime) { return static_cast<std::int64_t>(std::ceil((aTime - m_pos) * m_rate)); };
    if (m_state == Stretch::Off) {
        const std::int64_t left = until(m_from - kWarmSeconds);
        if (left > 0) return std::min(frames, static_cast<size_t>(left));
        m_stretch->Reset();
        m_feed = static_cast<std::uint64_t>(m_cursor) + static_cast<std::uint64_t>(m_stretch->Latency());
        m_feedDebt = 0;
        m_state = Stretch::Warming;
    }
    if (m_state == Stretch::Warming) {
        const std::int64_t left = until(m_from);
        if (left > 0) return std::min(frames, static_cast<size_t>(left));
        m_rampDone = 0;
        m_state = Stretch::On;
    }
    if (m_rampDone < m_rampFrames) return std::min(frames, static_cast<size_t>(m_rampFrames - m_rampDone));
    return frames;
}

// The next `frames` of the deck's copy of A, into m_dry. False, with silence, when the second player's sound is
// not there: the reading waits where it is.
bool MixDeck::NextTail(size_t frames) {
    auto starved = [&] {
        std::fill_n(m_dry.begin(), frames * 2, 0.0f);
        return false;
    };
    if (m_state == Stretch::Off) {
        if (!ReadTail(static_cast<std::uint64_t>(m_cursor), frames, m_dry.data())) return starved();
        m_cursor += static_cast<double>(frames);
        m_pos += static_cast<double>(frames) / m_rate;
        return true;
    }
    double speed = 1;
    if (m_state == Stretch::On) {
        const double done = m_rampFrames > 0
                                ? std::min(1.0, (static_cast<double>(m_rampDone) + frames * 0.5) / m_rampFrames)
                                : 1.0;
        speed = 1 + (m_rateTo - 1) * done;
    }
    const double owed = frames * speed + m_feedDebt;
    const size_t take = static_cast<size_t>(owed);
    if (take * 2 > m_in.size() || !ReadTail(m_feed, take, m_in.data())) return starved();
    // Read before the stretch is fed: starved, it must not be given the same sound twice.
    const bool warming = m_state == Stretch::Warming;
    if (warming && !ReadTail(static_cast<std::uint64_t>(m_cursor), frames, m_dry.data())) return starved();
    m_stretch->Process(m_in.data(), take, m_wet.data(), frames);
    if (warming) {
        // Both copies say the same thing at the same time: the stretched one replaces the plain one just before A
        // starts to move.
        const double blendFrom = m_from - kBlendSeconds;
        for (size_t i = 0; i < frames; i++) {
            const double at = m_pos + static_cast<double>(i) / m_rate;
            const float wet = static_cast<float>(std::clamp((at - blendFrom) / kBlendSeconds, 0.0, 1.0));
            m_dry[i * 2] += (m_wet[i * 2] - m_dry[i * 2]) * wet;
            m_dry[i * 2 + 1] += (m_wet[i * 2 + 1] - m_dry[i * 2 + 1]) * wet;
        }
    } else {
        std::copy_n(m_wet.begin(), frames * 2, m_dry.begin());
        m_rampDone += frames;
    }
    m_feedDebt = owed - static_cast<double>(take);
    m_feed += take;
    m_cursor += static_cast<double>(take);
    m_pos += static_cast<double>(take) / m_rate;
    return true;
}

// While Apple's player still plays A alone: how far the deck's copy is from it, for the journal and the session.
void MixDeck::Measure(const float* stereo, std::uint64_t mainFrame, size_t frames, double scale) {
    const std::int64_t from = static_cast<std::int64_t>(mainFrame) + m_delay;
    if (from < 0 || !ReadTail(static_cast<std::uint64_t>(from), frames, m_check.data())) return;
    for (size_t i = 0; i < frames; i++) {
        const double main = (stereo[i * 2] + stereo[i * 2 + 1]) * 0.5;
        const double tail = (m_check[i * 2] + m_check[i * 2 + 1]) * 0.5 * scale;
        m_residualError += (main - tail) * (main - tail);
        m_residualPower += main * main;
    }
    m_residualFrames += frames;
    if (m_residualFrames < static_cast<size_t>(kMeasureSeconds * m_rate)) return;
    if (m_residualPower / static_cast<double>(m_residualFrames) > kQuiet) {
        m_residualDb = 10 * std::log10(std::max(m_residualError / m_residualPower, 1e-12));
    }
    m_residualError = m_residualPower = 0;
    m_residualFrames = 0;
}

void MixDeck::ProcessMain(float* stereo, size_t frames, int sampleRate, const DeckBlock& block) {
    std::lock_guard<std::mutex> guard(m_lock);
    if (!m_open || frames == 0 || sampleRate != m_rate) return;
    const bool known = m_mainFeed.stream != 0;
    if (m_mainFeed.Note(block, m_mainFrames, m_rate) && known) {
        // Apple's player moved on to another stream (the next song): both players write at the same pace, so what
        // each is handed now is heard about together. Near enough for the curves; nothing is compared any more.
        m_oldEnd.reset();
        if (m_exact) m_oldEnd = static_cast<std::int64_t>(m_mainFrames - m_emptyEnd) + m_apart;
        m_emptyHead = 0;
        m_headCounted = false;
        m_apart = static_cast<std::int64_t>(m_tailSent) - static_cast<std::int64_t>(m_mainFrames);
        m_exact = false;
    }
    // Apple's player pads the end of a stream and the start of the next with empty frames: the songs are further
    // apart than the streams.
    const size_t head = EmptyFrames(stereo, frames, false);
    if (m_oldEnd && !m_headCounted) {
        m_emptyHead += head;
        m_headCounted = head < frames;
    }
    m_emptyEnd = head == frames ? m_emptyEnd + frames : EmptyFrames(stereo, frames, true);
    for (size_t i = 0; i < frames; i++) {
        m_history[(m_mainFrames + i) % m_history.size()] = (stereo[i * 2] + stereo[i * 2 + 1]) * 0.5f;
    }
    const std::uint64_t first = m_mainFrames;
    m_mainFrames += frames;
    if (!m_aligned) return;
    const double scale = m_scale * (m_alignVolume > 0 ? m_volume / m_alignVolume : 1);
    for (size_t done = 0; done < frames;) {
        const std::int64_t at = static_cast<std::int64_t>(first + done) + m_apart;
        const size_t count = UntilPoint(at, std::min(kChunk, frames - done));
        const DeckGains from = GainsAt(at);
        const DeckGains to = GainsAt(at + static_cast<std::int64_t>(count));
        float* chunk = stereo + done * 2;
        if (from.mainLow != 1 || to.mainLow != 1) {
            m_mainShelf.Run(chunk, count, (from.mainLow + to.mainLow) * 0.5, m_rate);
        } else {
            m_mainShelf.Clear();
        }
        if (from.main != 1 || to.main != 1) {
            for (size_t i = 0; i < count; i++) {
                const float gain = static_cast<float>(Lerp(from.main, to.main, static_cast<double>(i) / count));
                chunk[i * 2] *= gain;
                chunk[i * 2 + 1] *= gain;
            }
        } else if (m_exact && m_state == Stretch::Off && from.tail == 0 && to.tail == 0 && from.mainLow == 1 &&
                   to.mainLow == 1) {
            Measure(chunk, first + done, count, scale);
        }
        done += count;
    }
}

bool MixDeck::ProcessTail(float* stereo, size_t frames, int sampleRate, bool silent, const DeckBlock& block) {
    std::lock_guard<std::mutex> guard(m_lock);
    auto nothing = [&] {
        if (stereo) std::fill_n(stereo, frames * 2, 0.0f);
        return false;
    };
    if (!m_open || frames == 0 || sampleRate != m_rate || m_hold == 0) return nothing();
    // A new stream (the second player sought) or a block that cannot be had: what was found no longer holds.
    if (m_tailFeed.Note(block, m_tailSent, m_rate) || !stereo) m_aligned = false;
    const std::uint64_t first = m_tailSent;
    m_tailSent += frames;
    // A block the second player has nothing for is not sound of the song: what waits here covers for it, and the
    // deck stays in step for as long as some is left.
    for (size_t done = 0; stereo && !silent && done < frames;) {
        const size_t at = static_cast<size_t>(m_tailFrames % m_hold);
        const size_t count = std::min(frames - done, m_hold - at);
        std::copy_n(stereo + done * 2, count * 2, m_tail.begin() + at * 2);
        m_tailFrames += count;
        done += count;
    }
    if (!m_aligned) return nothing();
    const double scale = m_scale * (m_alignVolume > 0 ? m_volume / m_alignVolume : 1);
    bool heard = false;
    for (size_t done = 0; done < frames;) {
        const std::int64_t at = static_cast<std::int64_t>(first + done);
        const size_t count = UntilPoint(at, UntilEvent(std::min(kChunk, frames - done)));
        const DeckGains from = GainsAt(at);
        const DeckGains to = GainsAt(at + static_cast<std::int64_t>(count));
        const bool there = NextTail(count);
        float* chunk = stereo + done * 2;
        if (!there || (from.tail == 0 && to.tail == 0)) {
            std::fill_n(chunk, count * 2, 0.0f);
            m_tailShelf.Clear();
        } else {
            if (from.tailLow != 1 || to.tailLow != 1) {
                m_tailShelf.Run(m_dry.data(), count, (from.tailLow + to.tailLow) * 0.5, m_rate);
            } else {
                m_tailShelf.Clear();
            }
            for (size_t i = 0; i < count; i++) {
                const float gain = static_cast<float>(Lerp(from.tail, to.tail, static_cast<double>(i) / count) * scale);
                chunk[i * 2] = m_dry[i * 2] * gain;
                chunk[i * 2 + 1] = m_dry[i * 2 + 1] * gain;
            }
            heard = true;
        }
        done += count;
    }
    return heard;
}

}  // namespace ampify::automix
