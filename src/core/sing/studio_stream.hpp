#pragma once

// Sing's studio mode on one output stream: the song plays a fixed delay late, its vocals taken away. The model needs
// seconds of context, so the audio thread only swaps blocks through lock-free single-producer single-consumer rings
// (every buffer made in the constructor, never a wait) while a worker runs one model window per hop and publishes
// the vocals one hop behind the window's end, crossfaded with the previous window's over that hop.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "resampler.hpp"

namespace ampify::sing {

class VocalSplitter;

class StudioStream {
public:
    // `rate` 44100 to 192000, stereo. Vocals are ready up to two hops plus the resamplers' latency after their sound
    // came in: a shorter delay could never be met and is raised to that (Delay() tells). The rest of the delay is the
    // model's time; when it runs late, the song plays as it is (a miss) until the worker catches up.
    explicit StudioStream(int rate, double delaySeconds = 1.3, double hopSeconds = 0.5);
    ~StudioStream();
    StudioStream(const StudioStream&) = delete;
    StudioStream& operator=(const StudioStream&) = delete;

    // ---- the audio thread

    // In place, interleaved stereo. `voice` 0..1: how much of the vocals stays.
    void Process(float* interleaved, size_t frames, double voice);

    // ---- the worker thread

    // One hop, if a hop of new sound is waiting. False when there was nothing to do.
    bool Work(VocalSplitter& splitter);

    // ---- any thread

    // A seek or a new song: what came in so far is dropped, and the stream is silent for the delay again, after a
    // short fade of what was heard: the held sound, or with `heldWasHeard` false (the stream was not playing, so its
    // sound is old) the song coming in.
    void Flush(bool heldWasHeard = true);
    bool Buffering() const;
    double Held() const;  // seconds taken in and not heard yet: the delay, less while it fills again after a flush
    double Delay() const;  // seconds
    std::uint64_t Misses() const;  // blocks that played some of the song without its vocals taken away

private:
    void TakeFlush();
    void ProcessChunk(float* interleaved, size_t frames, double voice);
    double NextVoice(double voice);
    void Restart(std::int64_t origin);
    bool ReadHop();
    bool RunWindow(VocalSplitter& splitter);
    void Publish(size_t frames);

    int m_rate;
    std::int64_t m_delay;    // frames
    std::int64_t m_hop;      // frames at m_rate
    size_t m_hop44;          // the same hop at the model's rate
    std::int64_t m_latency;  // of the resamplers' round trip, frames at m_rate
    std::int64_t m_fade;     // between the song as it is and processed, frames
    std::int64_t m_chunk;    // the most the audio thread writes at once
    double m_voiceStep;      // per frame
    Resampler m_down;
    Resampler m_up;

    // The song as it came in, a ring indexed by frame (counted since construction), and the vocals for those same
    // frames, valid from m_vocalBegin to m_vocalEnd.
    std::int64_t m_capacity;
    std::vector<float> m_dry;
    std::vector<float> m_vocals;
    std::atomic<std::int64_t> m_written{0};      // audio -> worker: frames of m_dry written
    std::atomic<std::int64_t> m_played{0};       // audio -> worker: the oldest frame whose vocals may still be read
    std::atomic<std::int64_t> m_vocalBegin{0};   // worker -> audio
    std::atomic<std::int64_t> m_vocalEnd{0};     // worker -> audio
    std::atomic<std::uint64_t> m_flushes{0};     // asked, from any thread
    std::atomic<bool> m_heldWasHeard{true};      // asked with the last flush
    std::atomic<std::uint64_t> m_epoch{0};       // audio -> worker: the flushes the audio thread has taken
    std::atomic<std::int64_t> m_epochStart{0};   // audio -> worker: the first frame after the last of them
    std::atomic<bool> m_buffering{true};
    std::atomic<std::uint64_t> m_misses{0};

    // The audio thread's own.
    std::uint64_t m_audioEpoch = 0;
    std::int64_t m_start = 0;  // first frame of the current epoch; before it, silence
    std::int64_t m_fadeOutFrom = -1;  // the first frame played after the last flush, faded out; -1 for none
    bool m_fadeLive = false;          // that frame is the one coming in, not the held one
    double m_voice = -1;       // smoothed; below 0 until the first block
    double m_mix = 0;          // 0 the song as it is, 1 processed

    // The worker's own.
    std::uint64_t m_workerEpoch = 0;
    std::int64_t m_read = 0;     // next frame of m_dry to analyse
    std::int64_t m_origin = 0;   // the frame the analysis started at, silence before it
    std::int64_t m_upCount = 0;  // frames the upsampler has given since m_origin
    std::vector<float> m_hopIn;    // one hop at m_rate, interleaved
    std::vector<float> m_hopModel; // the same at 44.1 kHz
    std::vector<float> m_historyLeft;
    std::vector<float> m_historyRight;
    std::vector<float> m_windowLeft;   // the model's vocals for the window
    std::vector<float> m_windowRight;
    std::vector<float> m_sum;      // two hops at 44.1 kHz, interleaved: faded takes, the newer hop still open
    std::vector<float> m_weight;
    std::vector<float> m_fadeIn;   // np.hanning(2 * hop)
    std::vector<float> m_final;    // the finished hop, interleaved
    std::vector<float> m_upOut;
};

}  // namespace ampify::sing
