// Sing in AMPLibraryAgent.exe: the singer's voice turned down in Apple's stereo stream, as Apple Music Sing does on
// the iPhone. Light mode filters the centre of the stereo image in the voice's band, at once. Studio mode separates
// the voice with UVR's Voc_FT on the graphics card (sing_model.cpp), which needs the sound held back a moment: Apple's
// player is told that much earlier a time (automix.cpp), so its lyrics stay on what is heard.
//
// Threads: the audio thread calls SingMain for each block and never waits; Sing's own thread follows the request the
// interface writes in the shared block, prepares studio mode (download, model) and runs the model.

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

constexpr auto kTick = 10ms;            // the thread's pace while the model has nothing to do
constexpr auto kRequestEvery = 100ms;   // how often the interface's request is read
constexpr double kStudioDelay = 1.3;    // seconds held back: two hops of the model, its run and some margin
constexpr double kStudioHop = 0.5;
constexpr int kRates[] = {44100, 48000, 88200, 96000, 176400, 192000};

enum class Mode : int { Off = 0, Light = 1, Studio = 2 };

// Read by the audio thread for each block.
std::atomic<int> g_mode{static_cast<int>(Mode::Off)};
std::atomic<float> g_voice{1.0f};
std::atomic<int> g_wantedRate{0};  // the stream's rate, so that Sing's thread builds its studio stream
// One studio stream per rate, built by Sing's thread, never freed: the audio thread may hold one at any time.
std::array<std::atomic<core_sing::StudioStream*>, std::size(kRates)> g_studio{};
core_sing::LightFilter g_light;  // audio thread only

// Studio mode on and off (Sing's thread) and Apple's player paused and playing (the thread that sets its rate) both
// move Apple's clock and player: under one lock.
std::mutex g_transition;
bool g_paused = false;  // Apple's player paused, as seen through AVCFPlayerSetRate
double g_held = 0;      // seconds studio mode holds the sound back; 0 while it is off

void FlushStudios() {
    for (auto& slot : g_studio) {
        if (core_sing::StudioStream* studio = slot.load(std::memory_order_acquire)) studio->Flush();
    }
}

int RateIndex(int rate) {
    for (size_t i = 0; i < std::size(kRates); i++) {
        if (kRates[i] == rate) return static_cast<int>(i);
    }
    return -1;
}

core_sing::StudioStream* StudioFor(int rate) {
    const int index = RateIndex(rate);
    return index < 0 ? nullptr : g_studio[index].load(std::memory_order_acquire);
}

// Seconds the playing stream holds and nobody heard: what Apple's player must go back to stay on the sound.
double HeldNow() {
    core_sing::StudioStream* studio = StudioFor(detail::MainStreamRate());
    return studio ? studio->Held() : 0;
}

// A move back of Apple's player that Sing's thread makes, in seconds; below 0 for none. Never made under
// g_transition, nor on the thread that changes Apple's rate: Apple's player may call back into its own code.
std::atomic<double> g_pendingJump{-1};

}  // namespace

namespace detail {

void SingMain(float* samples, unsigned frames, int rate) {
    const auto mode = static_cast<Mode>(g_mode.load(std::memory_order_relaxed));
    if (mode == Mode::Off) return;
    g_wantedRate.store(rate, std::memory_order_relaxed);
    const double voice = g_voice.load(std::memory_order_relaxed);
    if (mode == Mode::Light) {
        g_light.Process(samples, frames, rate, voice);
        return;
    }
    if (core_sing::StudioStream* studio = StudioFor(rate)) studio->Process(samples, frames, voice);
}

void SingSeeked() { FlushStudios(); }

// Apple's stream goes on while paused, so the held second would play on after the pause: studio mode goes silent at
// once and puts the player back where the sound stopped, with Apple's own time, which it shows while paused.
void SingPaused() {
    std::lock_guard lock(g_transition);
    if (g_paused) return;
    g_paused = true;
    if (g_held <= 0) return;
    g_pendingJump.store(HeldNow(), std::memory_order_relaxed);  // before the flush, which empties it
    FlushStudios();
    HoldClockBack(0);
}

// Called before Apple's player plays, which reads its time then: held back again, after a silent fill.
void SingPlays() {
    std::lock_guard lock(g_transition);
    if (!g_paused) return;
    g_paused = false;
    g_pendingJump.store(-1, std::memory_order_relaxed);  // a pause too short to move the player: it plays on
    if (g_held <= 0) return;
    FlushStudios();
    HoldClockBack(g_held);
}

bool SingHoldsSound() { return g_mode.load(std::memory_order_relaxed) == static_cast<int>(Mode::Studio); }

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
    if (reason == "automix mixing") return audio::SingReason::AutoMixMixing;
    if (reason == kDownloadFailed) return audio::SingReason::DownloadFailed;
    if (reason.find("graphics card") != std::string::npos) return audio::SingReason::NoGraphicsCard;
    return audio::SingReason::ModelFailed;
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
            const double back = g_pendingJump.exchange(-1, std::memory_order_relaxed);
            if (back >= 0) detail::JumpInPlace(back);
            if (!RunModel()) std::this_thread::sleep_for(kTick);
        }
    }

private:
    Studio m_studio;
    bool m_studioOn = false;
    int m_studioRate = 0;
    Mode m_reported = Mode::Off;
    Mode m_wished = Mode::Off;

    // The interface's request, applied: the voice at once, the mode once what it needs is there.
    void Follow() {
        const SingWish wish = ReadWish();
        g_voice.store(static_cast<float>(wish.voice), std::memory_order_relaxed);
        if (wish.mode == Mode::Studio && m_wished != Mode::Studio && m_studio.reason == kDownloadFailed) {
            m_studio.failed = false;  // asked again after a failed download: it is tried again
            m_studio.reason.clear();
        }
        m_wished = wish.mode;
        Mode effective = wish.mode;
        std::string reason;
        if (wish.mode == Mode::Studio && !StudioUsable(reason)) effective = reason.empty() ? Mode::Off : Mode::Light;
        if (effective != Mode::Studio && m_studioOn) StopStudio();
        if (effective == Mode::Studio && (!m_studioOn || m_studioRate != detail::MainStreamRate())) StartStudio();
        if (effective != Mode::Studio) g_mode.store(static_cast<int>(effective), std::memory_order_relaxed);
        if (effective != m_reported) Note("Sing: " + ModeName(effective) + (reason.empty() ? "" : " (" + reason + ")"));
        m_reported = effective;
        PublishState(wish, effective, reason);
    }

    // Whether studio mode can run now; while it cannot, `reason` says why, or stays empty while it is being prepared
    // or Apple has no stereo stream yet (the sound is then left alone rather than switched to light mode a while).
    bool StudioUsable(std::string& reason) {
        if (detail::InAtmosNow()) {
            reason = "dolby atmos";
            return false;
        }
        if (detail::AutoMixOpen()) {
            reason = "automix mixing";
            return false;
        }
        if (m_studio.failed) {
            reason = m_studio.reason;
            return false;
        }
        if (m_studio.model) return RateIndex(detail::MainStreamRate()) >= 0;
        if (!PackReady()) {
            Install();
            return false;
        }
        LoadModel();
        reason = m_studio.failed ? m_studio.reason : "";
        return m_studio.model != nullptr && RateIndex(detail::MainStreamRate()) >= 0;
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
        Note("Sing: the voice model is loaded on the graphics card in " + Fixed(ms, 0) + " ms");
    }

    // Holds the sound back from now: the stream starts again silent for the delay, Apple's player is told it is that
    // much earlier and jumps to where it is, so that it reads its time again: one short pause, then all in step.
    void StartStudio() {
        const int rate = detail::MainStreamRate();
        core_sing::StudioStream* studio = StudioFor(rate);
        if (!studio && RateIndex(rate) >= 0) {
            studio = new core_sing::StudioStream(rate, kStudioDelay, kStudioHop);  // kept for the process's life
            g_studio[RateIndex(rate)].store(studio, std::memory_order_release);
        }
        if (!studio) return;
        bool jump = false;
        {
            std::lock_guard lock(g_transition);
            studio->Flush(false);  // it was not playing: what it holds is old
            g_held = studio->Delay();
            g_mode.store(static_cast<int>(Mode::Studio), std::memory_order_relaxed);
            if (!g_paused) {  // paused, the clock is held back when the player plays (SingPlays)
                detail::HoldClockBack(g_held);
                jump = true;
            }
        }
        if (jump) detail::JumpInPlace(0);
        m_studioOn = true;
        m_studioRate = rate;
    }

    // Back to Apple's own time: the player goes back by what was held and not heard, so nothing is skipped. Paused,
    // it is there already.
    void StopStudio() {
        double back = -1;
        {
            std::lock_guard lock(g_transition);
            const bool held = g_held > 0;
            g_held = 0;
            g_mode.store(static_cast<int>(Mode::Off), std::memory_order_relaxed);
            if (held && !g_paused) {
                back = HeldNow();
                detail::HoldClockBack(0);
            }
        }
        m_studioOn = false;
        if (back >= 0) detail::JumpInPlace(back);
    }

    // One hop of the model, or false when there was none to run.
    bool RunModel() {
        if (!m_studioOn || !m_studio.splitter) return false;
        core_sing::StudioStream* studio = StudioFor(m_studioRate);
        return studio && studio->Work(*m_studio.splitter);
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
        core_sing::StudioStream* studio = m_studioOn ? StudioFor(m_studioRate) : nullptr;
        if (studio) {
            status.phase = studio->Buffering() ? audio::SingPhase::Buffering : audio::SingPhase::On;
            status.delaySeconds = studio->Delay();
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
