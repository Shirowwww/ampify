#pragma once

// Sing's studio mode on one output stream: the song plays a fixed delay late, its vocals taken away. The model needs
// seconds of context, so the audio thread only swaps blocks through lock-free single-producer single-consumer rings
// (every buffer made in the constructor, never a wait) while a worker keeps the song at the model's rate, runs one
// model window each hop of whole STFT hops (so that the next window takes over most of the last one's spectrum) and
// publishes the vocals one hop behind the window's end, crossfaded with the previous window's over that hop.
//
// The ring is read where the song was taken in a delay ago. A pause stops the reading (after a short fade) while the
// player's last sound still comes in, so playing again goes on from there at once: what Apple's clock is held back
// by is then Lag(), the delay and that last sound.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "resampler.hpp"
#include "vocal_splitter.hpp"

namespace ampify::sing {

class StudioStream {
public:
    // `rate` 44100 to 192000, stereo. `hopSeconds` is rounded to whole STFT hops of the model (23 ms). Vocals are
    // ready up to two hops plus the resamplers' latency after their sound came in: a shorter delay could never be
    // met and is raised to that (Delay() tells). The rest of the delay is the model's time; when it runs late, the
    // song plays as it is (a miss) until the worker catches up.
    explicit StudioStream(int rate, double delaySeconds = 1.3, double hopSeconds = 0.4);
    ~StudioStream();
    StudioStream(const StudioStream&) = delete;
    StudioStream& operator=(const StudioStream&) = delete;

    // ---- the audio thread

    // In place, interleaved stereo. `voice` 0..1: how much of the vocals stays. `paused`: the player is paused, so
    // the sound fades out and stays silent, and only blocks with sound in them are taken in (its last ones).
    void Process(float* interleaved, size_t frames, double voice, bool paused = false);

    // ---- the worker thread

    // Some of the song read, or one model window. False when there was nothing to do.
    bool Work(VocalSplitter& splitter);
    // Frees the last window's spectrum (12 MB) while studio mode rests; the next window is analysed whole.
    void Rest();

    // ---- any thread

    // What a flush fades out over 10 ms before its silence: the held sound, which was being heard; the song coming
    // in, when the stream was not playing (its sound is old) but the song was heard as it is; or nothing.
    enum class Fade { Held, Live, None };

    // A seek or a new song: what came in so far is dropped, and the stream is silent for the delay again, or for
    // `lagSeconds` when that is longer (up to the delay and the tail a pause may add), to stay in step with another
    // stream that lags that much.
    void Flush(Fade fade = Fade::Held, double lagSeconds = 0);
    bool Buffering() const;
    double Held() const;   // seconds of the song taken in and not heard yet: 0 just after a flush
    double Lag() const;    // seconds between the song taken in and what is heard, silence after a flush included
    double Delay() const;  // seconds
    std::uint64_t Misses() const;  // blocks that played some of the song without its vocals taken away

private:
    void TakeFlush();
    bool Write(const float* interleaved, size_t frames, bool paused);
    void Read(float* interleaved, size_t frames, std::int64_t live, double voice, bool paused);
    double NextVoice(double voice);
    void Restart(std::int64_t origin);
    bool ReadSong();
    bool RunWindow(VocalSplitter& splitter);
    void Publish(size_t frames);

    int m_rate;
    std::int64_t m_delay;    // frames
    size_t m_hopHops;        // STFT hops in one model hop
    size_t m_hop44;          // the model hop at the model's rate
    std::int64_t m_hop;      // the same at m_rate, rounded up
    std::int64_t m_latency;  // of the resamplers' round trip, frames at m_rate
    std::int64_t m_fade;     // between the song as it is and processed, frames
    std::int64_t m_chunk;    // the most the audio thread writes at once
    std::int64_t m_tail;     // the most a paused stream takes in
    double m_voiceStep;      // per frame
    Resampler m_down;
    Resampler m_up;

    // The song as it came in, a ring indexed by frame (counted since construction), and the vocals for those same
    // frames, valid from m_vocalBegin to m_vocalEnd.
    std::int64_t m_capacity;
    std::vector<float> m_dry;
    std::vector<float> m_vocals;
    std::atomic<std::int64_t> m_written{0};      // audio -> worker: frames of m_dry written
    std::atomic<std::int64_t> m_played{0};       // audio -> worker: the next frame read, whose vocals it may need
    std::atomic<std::int64_t> m_vocalBegin{0};   // worker -> audio
    std::atomic<std::int64_t> m_vocalEnd{0};     // worker -> audio
    std::atomic<std::uint64_t> m_flushes{0};     // asked, from any thread
    std::atomic<Fade> m_flushFade{Fade::Held};   // asked with the last flush
    std::atomic<std::int64_t> m_flushLag{0};     // frames, asked with the last flush
    std::atomic<std::uint64_t> m_epoch{0};       // audio -> worker: the flushes the audio thread has taken
    std::atomic<std::int64_t> m_epochStart{0};   // audio -> worker: the first frame after the last of them
    std::atomic<bool> m_buffering{true};
    std::atomic<std::uint64_t> m_misses{0};

    // The audio thread's own.
    std::uint64_t m_audioEpoch = 0;
    std::int64_t m_readAt = 0;        // the next frame heard
    std::int64_t m_start = 0;         // first frame of the current epoch; before it, silence
    std::int64_t m_fadeOutFrom = -1;  // the first frame played after the last flush, faded out; -1 for none
    std::int64_t m_fadeShift = 0;     // the old reading place against the new, for that fade
    bool m_fadeLive = false;          // that frame is the one coming in, not the held one
    double m_voice = -1;              // smoothed; below 0 until the first block
    double m_mix = 0;                 // 0 the song as it is, 1 processed
    double m_gain = 1;                // 0 while paused, faded in and out

    // The worker's own.
    std::uint64_t m_workerEpoch = 0;
    std::int64_t m_read = 0;     // next frame of m_dry to take
    std::int64_t m_origin = 0;   // the frame the analysis started at, silence before it
    std::int64_t m_upCount = 0;  // frames the upsampler has given since m_origin
    std::uint64_t m_windows = 0; // run since m_origin
    std::vector<float> m_in;       // a piece of the song at m_rate, interleaved
    std::vector<float> m_in44;     // the same at 44.1 kHz
    std::vector<float> m_left;     // the model's window, then what came in after it, at 44.1 kHz
    std::vector<float> m_right;
    size_t m_fill = 0;             // samples in m_left and m_right
    std::vector<float> m_windowLeft;   // the model's vocals for the window
    std::vector<float> m_windowRight;
    std::vector<float> m_sum;      // two hops at 44.1 kHz, interleaved: faded takes, the newer hop still open
    std::vector<float> m_weight;
    std::vector<float> m_fadeIn;   // np.hanning(2 * hop)
    std::vector<float> m_final;    // the finished hop, interleaved
    std::vector<float> m_upOut;
    VocalSplitter::Spectrum m_spectrum;  // the last window's, which the next takes most of
};

}  // namespace ampify::sing
