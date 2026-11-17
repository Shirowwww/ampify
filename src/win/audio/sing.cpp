// Sing in AMPLibraryAgent.exe: the singer's voice turned down in Apple's stereo stream, as Apple Music Sing does on
// the iPhone. Light mode filters the centre of the stereo image in the voice's band, at once. Studio mode separates
// the voice with UVR's Voc_FT on the graphics card (sing_model.cpp), which needs the sound held back a moment: Apple's
// player is told that much earlier a time (automix.cpp), so its lyrics stay on what is heard. While AutoMix mixes, the
// old song plays in a stream of its own (the deck's), which gets the same treatment and the same delay.
//
// Threads: the audio threads call SingMain and SingTail for each block and never wait; Sing's own thread follows the
// request the interface writes in the shared block, prepares studio mode (download, model) and runs the model.

#include <windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "../../core/audio/audio_share.hpp"
#include "../../core/sing/light_filter.hpp"
#include "../../core/sing/studio_stream.hpp"
#include "../../core/sing/vocal_splitter.hpp"
#include "../../core/sing_pack.hpp"
#include "../kit_command.hpp"
#include "../win_common.hpp"
#include "audio_sharing.hpp"
#include "hooks_detail.hpp"
#include "sing_model.hpp"

namespace ampify::win::audio_hooks {

namespace {

using namespace std::chrono_literals;
using detail::Fixed;
using detail::Note;
namespace core_sing = ampify::sing;
using Fade = core_sing::StudioStream::Fade;

constexpr auto kTick = 10ms;            // the thread's pace while studio mode is on
constexpr auto kRequestEvery = 100ms;   // how often the interface's request is read, and the pace otherwise
constexpr double kStudioDelay = 1.3;    // seconds held back: two hops of the model, its run and some margin
constexpr double kStudioHop = 0.4;
// A song switched to stereo has its new stream by then: measured 0.1 to 0.3 s, though Apple's position then stands
// still for 3 to 4 s while the song reloads.
constexpr auto kSwitchSettles = 4s;
// Studio mode off this long: the model leaves the graphics card, to be loaded again (about a second) when asked.
constexpr auto kModelRests = 2min;
constexpr int kRates[] = {44100, 48000, 88200, 96000, 176400, 192000};

enum class Mode : int { Off = 0, Light = 1, Studio = 2 };

using Studios = std::array<std::atomic<core_sing::StudioStream*>, std::size(kRates)>;

// Read by the audio threads for each block.
std::atomic<int> g_mode{static_cast<int>(Mode::Off)};
std::atomic<float> g_voice{1.0f};
std::atomic<bool> g_paused{false};  // Apple's player paused, as seen through AVCFPlayerSetRate
// A rate an audio thread met with no studio stream yet: Sing's thread builds one at its next tick.
std::atomic<int> g_wantedRate{0};
std::atomic<int> g_wantedTailRate{0};
// One studio stream per rate for Apple's stream and one for the deck's, built by Sing's thread, never freed: an audio
// thread may hold one at any time.
Studios g_studio{};
Studios g_tailStudio{};
core_sing::LightFilter g_light;      // Apple's audio thread only
core_sing::LightFilter g_tailLight;  // the deck's audio thread only
// The rate of Apple's last block in studio mode; StartStudio sets it too, for the stream it flushed itself.
std::atomic<int> g_lastRate{0};
bool g_tailOn = false;               // the deck's audio thread only: its studio stream holds the old song
std::int64_t g_tailQuiet = 0;        // the deck's audio thread only: frames fed since the deck fell silent

// Studio mode on and off (Sing's thread) and Apple's player paused and playing (the thread that sets its rate) both
// move Apple's clock: under one lock.
std::mutex g_transition;
bool g_holding = false;           // studio mode holds the sound back
const void* g_pausedOn = nullptr; // the song Apple's player paused on, only to compare

int RateIndex(int rate) {
    for (size_t i = 0; i < std::size(kRates); i++) {
        if (kRates[i] == rate) return static_cast<int>(i);
    }
    return -1;
}

core_sing::StudioStream* StudioFor(const Studios& studios, int rate) {
    const int index = RateIndex(rate);
    return index < 0 ? nullptr : studios[index].load(std::memory_order_acquire);
}

core_sing::StudioStream* MainStudio() { return StudioFor(g_studio, detail::MainStreamRate()); }

void FlushAll(Studios& studios, Fade fade) {
    for (auto& slot : studios) {
        if (core_sing::StudioStream* studio = slot.load(std::memory_order_acquire)) studio->Flush(fade);
    }
}

// Seconds the playing stream holds and nobody heard: what Apple's player must go back to stay on the sound.
double HeldNow() {
    core_sing::StudioStream* studio = MainStudio();
    return studio ? studio->Held() : 0;
}

// What Apple's clock is held back by: the playing stream's lag, the nominal delay before there is one.
double LagNow() {
    core_sing::StudioStream* studio = MainStudio();
    return studio ? studio->Lag() : kStudioDelay;
}

// A block in studio mode with no stream for its rate yet: silent, as the stream will be for its delay anyway.
void Mute(float* samples, unsigned frames) { std::fill(samples, samples + static_cast<size_t>(frames) * 2, 0.0f); }

}  // namespace

namespace detail {

void SingMain(float* samples, unsigned frames, int rate) {
    const auto mode = static_cast<Mode>(g_mode.load(std::memory_order_relaxed));
    if (mode == Mode::Off) return;
    const double voice = g_voice.load(std::memory_order_relaxed);
    if (mode == Mode::Light) {
        g_light.Process(samples, frames, rate, voice);
        return;
    }
    if (RateIndex(rate) < 0) return;  // a rate studio mode does not run at: the song as it is
    core_sing::StudioStream* studio = StudioFor(g_studio, rate);
    if (!studio) {
        g_wantedRate.store(rate, std::memory_order_relaxed);
        return Mute(samples, frames);
    }
    // A stream of another rate played meanwhile: what this one holds is of an older song.
    if (g_lastRate.exchange(rate, std::memory_order_relaxed) != rate) studio->Flush(Fade::None);
    studio->Process(samples, frames, voice, g_paused.load(std::memory_order_relaxed));
}

// The deck's copy of the old song gets what Apple's stream gets: in studio mode, the same lag, so the mix stays in
// step as heard, and after the deck falls silent, what its stream still holds.
bool SingTail(float* samples, unsigned frames, int rate, bool heard) {
    const auto mode = static_cast<Mode>(g_mode.load(std::memory_order_relaxed));
    if (mode != Mode::Studio) {
        g_tailOn = false;
        if (mode == Mode::Light && heard) g_tailLight.Process(samples, frames, rate, g_voice.load());
        return heard;
    }
    if (RateIndex(rate) < 0) return heard;
    core_sing::StudioStream* studio = StudioFor(g_tailStudio, rate);
    if (!studio) {
        g_wantedTailRate.store(rate, std::memory_order_relaxed);
        if (heard) Mute(samples, frames);
        return false;
    }
    if (!heard && !g_tailOn) return false;
    if (heard && !g_tailOn) {
        core_sing::StudioStream* main = MainStudio();
        studio->Flush(Fade::None, main ? main->Lag() : kStudioDelay);
        g_tailOn = true;
    }
    if (heard) {
        g_tailQuiet = 0;
    } else {
        Mute(samples, frames);
        g_tailQuiet += frames;
        if (static_cast<double>(g_tailQuiet) > studio->Lag() * rate) g_tailOn = false;  // all of it heard
    }
    studio->Process(samples, frames, g_voice.load(std::memory_order_relaxed));
    return true;
}

void SingSeeked() {
    FlushAll(g_studio, Fade::Held);
    std::lock_guard lock(g_transition);
    if (g_holding) HoldClockBack(kStudioDelay);  // what a flushed stream lags by
}

// Paused, studio mode stops reading its stream and keeps what it holds, so playing again goes on from there at once.
void SingPaused() {
    const void* song = CurrentSong();  // Apple's player is called outside the lock
    std::lock_guard lock(g_transition);
    if (g_paused.exchange(true, std::memory_order_relaxed)) return;
    g_pausedOn = song;
}

// Called before Apple's player plays, which reads its time then. Another song meanwhile: what was held is dropped.
void SingPlays() {
    const void* song = CurrentSong();
    std::lock_guard lock(g_transition);
    if (!g_paused.load(std::memory_order_relaxed)) return;
    const bool sameSong = song == g_pausedOn;
    if (!sameSong) FlushAll(g_studio, Fade::None);
    if (g_holding) HoldClockBack(sameSong ? LagNow() : kStudioDelay);
    g_paused.store(false, std::memory_order_relaxed);
}

bool SingHoldsSound() { return g_mode.load(std::memory_order_relaxed) == static_cast<int>(Mode::Studio); }

double SingLag() {
    std::lock_guard lock(g_transition);
    return g_holding ? LagNow() : 0;
}

}  // namespace detail

namespace {

// What Sing's thread knows and tells the interface.
struct Studio {
    std::unique_ptr<sing::OrtVocalModel> model;
    std::unique_ptr<core_sing::VocalSplitter> splitter;
    std::atomic<bool> installing{false};    // ampify sing install runs
    std::atomic<bool> installEnded{false};  // it ended and nobody looked yet
    std::atomic<int> installCode{0};        // then its exit code: -1 when it could not start or was stopped
    bool failed = false;
    std::string reason;
};

constexpr std::string_view kDownloadFailed = "download failed";

std::filesystem::path SingFolder() { return KitDirectory() / L"sing"; }

bool PackReady() { return sing_pack::Ready(SingFolder()); }

// How much of the two downloads has come in, 0 to 1, from the files being written.
double DownloadProgress() {
    const std::filesystem::path downloads = KitDirectory() / L"downloads";
    std::uintmax_t have = 0;
    for (const sing_pack::Download* item : {&sing_pack::kRuntime, &sing_pack::kModel}) {
        std::error_code ec;
        std::filesystem::path done = downloads / std::string(item->name);
        std::filesystem::path part = done;
        part += ".part";
        if (std::filesystem::exists(done, ec)) have += item->size;
        else if (auto size = std::filesystem::file_size(part, ec); !ec) have += size;
    }
    return static_cast<double>(have) / static_cast<double>(sing_pack::kRuntime.size + sing_pack::kModel.size);
}

struct SingWish {
    Mode mode = Mode::Off;
    double voice = 1;
};

// What the interface asked last, through the shared block; off when there is none.
SingWish ReadWish() {
    audio::SingRequest request;
    audio::SharedAudio* block = SharedAudioBlock();
    if (!block || !audio::ReadSingRequest(*block, request)) return {};
    return {static_cast<Mode>(request.mode), request.voice};
}

audio::SingReason ReasonCode(const std::string& reason) {
    if (reason.empty()) return audio::SingReason::None;
    if (reason == "dolby atmos") return audio::SingReason::DolbyAtmos;
    if (reason == kDownloadFailed) return audio::SingReason::DownloadFailed;
    if (reason.find("graphics card") != std::string::npos) return audio::SingReason::NoGraphicsCard;
    return audio::SingReason::ModelFailed;
}

// The stream an audio thread asked for, made here.
void Build(Studios& studios, std::atomic<int>& wanted) {
    const int rate = wanted.exchange(0, std::memory_order_relaxed);
    const int index = RateIndex(rate);
    if (index < 0 || studios[index].load(std::memory_order_acquire)) return;
    // Kept for the process's life: an audio thread may hold it at any time.
    studios[index].store(new core_sing::StudioStream(rate, kStudioDelay, kStudioHop), std::memory_order_release);
}

class SingThread {
public:
    void Run() {
        auto nextRequest = std::chrono::steady_clock::now();
        for (;;) {
            if (std::chrono::steady_clock::now() >= nextRequest) {
                Follow();
                nextRequest = std::chrono::steady_clock::now() + kRequestEvery;
            }
            if (m_studioOn) {
                Build(g_studio, g_wantedRate);
                Build(g_tailStudio, g_wantedTailRate);
            }
            if (!RunModel()) std::this_thread::sleep_for(m_studioOn ? kTick : kRequestEvery);
        }
    }

private:
    Studio m_studio;
    bool m_studioOn = false;
    Mode m_reported = Mode::Off;
    Mode m_wished = Mode::Off;
    bool m_stereoHeld = false;  // Apple's player asked for stereo versions (FollowStereo)
    bool m_stereoAsked = false;  // Sing was asked for at the last look
    std::chrono::steady_clock::time_point m_switchJumpAt{};  // the jump that last made a song pick stereo
    bool m_switchHold = false;  // that jump held the clock back for studio mode, which has not started yet
    std::chrono::steady_clock::time_point m_restingSince{};  // studio mode last asked for, or the model loaded
    std::uint64_t m_missesNoted = 0;                         // the playing stream's misses already in the journal
    std::chrono::steady_clock::time_point m_missNotedAt{};

    // The interface's request, applied: the voice at once, the mode once what it needs is there.
    void Follow() {
        const SingWish wish = ReadWish();
        g_voice.store(static_cast<float>(wish.voice), std::memory_order_relaxed);
        if (wish.mode == Mode::Studio && m_wished != Mode::Studio && m_studio.reason == kDownloadFailed) {
            m_studio.failed = false;  // asked again after a failed download: it is tried again
            m_studio.reason.clear();
        }
        m_wished = wish.mode;
        FollowStereo(wish.mode != Mode::Off, wish.mode == Mode::Studio && m_studio.model != nullptr);
        Mode effective = wish.mode;
        std::string reason;
        if (wish.mode == Mode::Studio && !StudioUsable(reason)) effective = reason.empty() ? Mode::Off : Mode::Light;
        if (effective != Mode::Studio && m_studioOn) StopStudio();
        if (effective == Mode::Studio && !m_studioOn) StartStudio();
        if (effective != Mode::Studio) {
            g_mode.store(static_cast<int>(effective), std::memory_order_relaxed);
            DropSwitchHold();
        }
        if (effective != m_reported) Note("Sing: " + ModeName(effective) + (reason.empty() ? "" : " (" + reason + ")"));
        m_reported = effective;
        RestModel(wish.mode == Mode::Studio);
        NoteMisses();
        PublishState(wish, effective, reason);
    }

    // Blocks played with the voice because the model ran late, in the journal at most every 10 s: what tells a busy
    // computer from a voice the model does not recognise.
    void NoteMisses() {
        core_sing::StudioStream* studio = m_studioOn ? MainStudio() : nullptr;
        const auto now = std::chrono::steady_clock::now();
        if (!studio || now - m_missNotedAt < 10s) return;
        const std::uint64_t misses = studio->Misses();
        if (misses < m_missesNoted) m_missesNoted = 0;  // another stream
        if (misses == m_missesNoted) return;
        Note("Sing: the model ran late, " + std::to_string(misses - m_missesNoted) +
             " block(s) played with the voice");
        m_missesNoted = misses;
        m_missNotedAt = now;
    }

    // The model, its graphics card memory and the streams' spectra go once studio mode was not asked for a while.
    void RestModel(bool asked) {
        const auto now = std::chrono::steady_clock::now();
        if (asked || !m_studio.model) {
            m_restingSince = now;
            return;
        }
        if (now - m_restingSince < kModelRests) return;
        m_studio.splitter.reset();
        m_studio.model.reset();
        for (Studios* studios : {&g_studio, &g_tailStudio}) {
            for (auto& slot : *studios) {
                if (core_sing::StudioStream* studio = slot.load(std::memory_order_acquire)) studio->Rest();
            }
        }
        Note("Sing: the voice model left the graphics card, studio mode unused for a while");
    }

    // Sing works on Apple's stereo stream, which a song playing in Dolby Atmos does not have: while Sing is asked
    // for, Apple's player is asked for the songs' stereo versions, and gets its own choice back when Sing goes off.
    // A change of that choice then a jump in place makes the song playing pick its version again (measured: 0.3 s
    // each way); a jump alone does not. Turning off with studio mode on, StopStudio's own jump does it.
    void FollowStereo(bool asked, bool studio) {
        const bool wasAsked = m_stereoAsked;
        m_stereoAsked = asked;
        if (asked && !m_stereoHeld && detail::InAtmosNow() && detail::HoldStereo(true)) {
            m_stereoHeld = true;
            Note("Sing: Apple's player asked for the song's stereo version");
            // One jump for both: a second one right after reads the player's time while the first still lands,
            // and once read 0 (the song started again). Studio mode's hold is set first, so Apple reads its time
            // held back; StartStudio then leaves the jump out.
            if (studio) {
                std::lock_guard lock(g_transition);
                if (!g_paused.load()) {  // paused, SingPlays holds it when the player plays
                    detail::HoldClockBack(kStudioDelay);
                    m_switchHold = true;
                }
            }
            detail::JumpInPlace(0);
            m_switchJumpAt = std::chrono::steady_clock::now();
            return;
        }
        if (asked || !wasAsked) return;
        // Apple sometimes starts a song in stereo by itself: with studio mode on, whose stop jumps anyway, the choice
        // is changed and given back even when Sing did not hold it, so that the song looks again for its Dolby Atmos
        // version. Otherwise only a held choice is, which saves a jump on a plain stereo song.
        if (!m_stereoHeld && !(m_studioOn && detail::HoldStereo(true))) return;
        detail::HoldStereo(false);
        m_stereoHeld = false;
        {
            std::lock_guard lock(g_transition);
            if (m_switchHold && !g_holding) detail::HoldClockBack(0);  // the studio mode it was for never started
        }
        m_switchHold = false;
        Note("Sing: Apple's player back on its own choice, Dolby Atmos included");
        if (!m_studioOn) detail::JumpInPlace(0);
    }

    // Whether studio mode can run now; while it cannot, `reason` says why, or stays empty while it is being prepared
    // (the sound is then left alone rather than switched to light mode a while).
    bool StudioUsable(std::string& reason) {
        if (detail::InAtmosNow()) {
            // Asked for its stereo version, the song comes in a moment; one that never comes stays Dolby Atmos.
            const bool switching = m_stereoHeld && std::chrono::steady_clock::now() - m_switchJumpAt < kSwitchSettles;
            if (!switching) reason = "dolby atmos";
            return false;
        }
        if (m_studio.failed) {
            reason = m_studio.reason;
            return false;
        }
        // No stream yet (0) is fine: the first one is held from its first block. Another rate is left alone.
        const int rate = detail::MainStreamRate();
        const bool rateFits = rate == 0 || RateIndex(rate) >= 0;
        if (m_studio.model) return rateFits;
        if (!PackReady()) {
            Install();
            return false;
        }
        LoadModel();
        reason = m_studio.failed ? m_studio.reason : "";
        return m_studio.model != nullptr && rateFits;
    }

    // Called while the pack is not ready: starts ampify sing install, or, once it ended without the pack, fails.
    void Install() {
        if (m_studio.installing.load()) return;
        if (m_studio.installEnded.exchange(false)) {
            m_studio.failed = true;
            m_studio.reason = std::string(kDownloadFailed);
            Note("Sing: ampify sing install failed (code " + std::to_string(m_studio.installCode.load()) + ")");
            return;
        }
        m_studio.installing = true;
        Note("Sing: downloading the studio files");
        RunAmpifyInBackground({"sing", "install", "--kit-dir", ToUtf8(KitDirectory().wstring())}, [this](int code) {
            m_studio.installCode = code;
            m_studio.installEnded = true;  // before `installing` goes: Install reads them the other way round
            m_studio.installing = false;
        });
    }

    void LoadModel() {
        std::string why;
        const auto started = std::chrono::steady_clock::now();
        m_studio.model = sing::OrtVocalModel::Load(SingFolder(), why);
        if (!m_studio.model) {
            m_studio.failed = true;
            m_studio.reason = why;
            Note("Sing: studio mode unavailable: " + why);
            return;
        }
        m_studio.splitter = std::make_unique<core_sing::VocalSplitter>(*m_studio.model);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        Note("Sing: the voice model is loaded and run once on the graphics card in " + Fixed(ms, 0) + " ms");
    }

    // Holds the sound back from now: the stream starts again silent for the delay, Apple's player is told it is that
    // much earlier and jumps to where it is, so that it reads its time again: one short pause, then all in step. With
    // no stream yet (nothing played since the start), the first one is held from its first block, with no jump.
    void StartStudio() {
        const int rate = detail::MainStreamRate();
        g_wantedRate.store(rate, std::memory_order_relaxed);
        Build(g_studio, g_wantedRate);
        core_sing::StudioStream* studio = StudioFor(g_studio, rate);
        bool jump = false;
        {
            std::lock_guard lock(g_transition);
            if (studio) studio->Flush(Fade::Live);  // it was not playing: what it holds is old
            g_lastRate.store(studio ? rate : 0, std::memory_order_relaxed);  // any other stream met is flushed
            g_holding = true;
            g_mode.store(static_cast<int>(Mode::Studio), std::memory_order_relaxed);
            if (!g_paused.load()) {  // paused, the clock is held back when the player plays (SingPlays)
                detail::HoldClockBack(kStudioDelay);
                jump = studio && !m_switchHold;  // the stereo switch's jump, just made, reads the time held back
            }
        }
        if (jump) detail::JumpInPlace(0);
        m_switchHold = false;
        m_studioOn = true;
    }

    // The stereo switch held the clock back for a studio mode that did not start after all: Apple's own time again.
    void DropSwitchHold() {
        if (!m_switchHold || std::chrono::steady_clock::now() - m_switchJumpAt < kSwitchSettles) return;
        m_switchHold = false;
        {
            std::lock_guard lock(g_transition);
            if (g_holding) return;  // studio mode holds it
            detail::HoldClockBack(0);
        }
        detail::JumpInPlace(0);
    }

    // Back to Apple's own time: the player goes back by what was held and not heard, so nothing is skipped.
    void StopStudio() {
        double back = 0;
        {
            std::lock_guard lock(g_transition);
            back = g_holding ? HeldNow() : 0;
            g_holding = false;
            g_mode.store(static_cast<int>(Mode::Off), std::memory_order_relaxed);
            detail::HoldClockBack(0);
        }
        m_studioOn = false;
        detail::JumpInPlace(back);
    }

    // One step of every stream with work waiting, Apple's first; false when none had any.
    bool RunModel() {
        if (!m_studioOn || !m_studio.splitter) return false;
        bool worked = false;
        for (Studios* studios : {&g_studio, &g_tailStudio}) {
            for (auto& slot : *studios) {
                core_sing::StudioStream* studio = slot.load(std::memory_order_acquire);
                if (studio && studio->Work(*m_studio.splitter)) worked = true;
            }
        }
        return worked;
    }

    void PublishState(const SingWish& wish, Mode effective, const std::string& reason) {
        audio::SharedAudio* block = SharedAudioBlock();
        if (!block) return;
        audio::SingStatus status;
        status.mode = static_cast<audio::SingMode>(effective);
        status.reason = ReasonCode(reason);
        status.studio = m_studio.model ? audio::SingStudio::Yes
                        : m_studio.failed && status.reason != audio::SingReason::DownloadFailed ? audio::SingStudio::No
                                                                                                 : audio::SingStudio::Unknown;
        core_sing::StudioStream* studio = m_studioOn ? MainStudio() : nullptr;
        if (m_studioOn) {
            status.phase = !studio || studio->Buffering() ? audio::SingPhase::Buffering : audio::SingPhase::On;
            status.delaySeconds = studio ? studio->Delay() : kStudioDelay;
        } else if (wish.mode == Mode::Studio && m_studio.failed) {
            status.phase = audio::SingPhase::Failed;
        } else if (wish.mode == Mode::Studio && m_studio.installing.load()) {
            status.phase = audio::SingPhase::Downloading;
            status.progress = DownloadProgress();
        } else if (wish.mode == Mode::Studio && reason.empty()) {
            status.phase = audio::SingPhase::Loading;
        } else {
            status.phase = effective == Mode::Off ? audio::SingPhase::Off : audio::SingPhase::On;
        }
        audio::WriteSingStatus(*block, status);
    }

    static std::string ModeName(Mode mode) {
        return mode == Mode::Studio ? "studio mode" : mode == Mode::Light ? "light mode" : "off";
    }
};

}  // namespace

namespace detail {

void StartSing() {
    static std::once_flag once;
    std::call_once(once, [] {
        std::thread([] {
            // The model runs here: above the interface's work, below the audio thread's.
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
            static SingThread thread;
            thread.Run();
        }).detach();
    });
}

}  // namespace detail

}  // namespace ampify::win::audio_hooks
