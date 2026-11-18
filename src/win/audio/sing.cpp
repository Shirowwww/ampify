// Sing in AMPLibraryAgent.exe: the singer's voice turned down in Apple's stereo stream, as Apple Music Sing does on
// the iPhone. Light mode filters the centre of the stereo image in the voice's band, at once. Studio mode separates
// the voice on the graphics card (sing_model.cpp) with one of two models (src/core/sing/vocal_model.hpp): the standard
// one, UVR's Voc_FT, or the precise one, Kim's Mel-Band RoFormer. Either needs the sound held back a moment: Apple's
// player is told that much earlier a time (automix.cpp), so its lyrics stay on what is heard. While AutoMix mixes, the
// old song plays in a stream of its own (the deck's), which gets the same treatment and the same delay.
//
// Threads: the audio threads call SingMain and SingTail for each block and never wait; Sing's own thread follows the
// request the interface writes in the shared block, prepares studio mode (download, model) and runs the model.

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
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

// The thread's pace while studio mode is on and nothing is due: a window comes every 0.4 s or more.
constexpr auto kTick = 25ms;
constexpr auto kRequestEvery = 100ms;   // how often the interface's request is read, and the pace otherwise
// Studio mode off this long: the model leaves the graphics card, to be loaded again (about a second) when asked.
constexpr auto kModelRests = 2min;
constexpr int kRates[] = {44100, 48000, 88200, 96000, 176400, 192000};

enum class Mode : int { Off = 0, Light = 1, Studio = 2 };

using Studios = std::array<std::atomic<core_sing::StudioStream*>, std::size(kRates)>;
constexpr size_t kModelCount = std::size(core_sing::kModels);
using ModelStudios = std::array<Studios, kModelCount>;

// Read by the audio threads for each block.
std::atomic<int> g_mode{static_cast<int>(Mode::Off)};
std::atomic<float> g_voice{1.0f};
// The model studio mode runs, an index of core_sing::kModels. Changed only while studio mode is off, so an audio
// thread in studio mode always meets the streams of the model being run.
std::atomic<int> g_model{0};
std::atomic<bool> g_paused{false};  // Apple's player paused, as seen through AVCFPlayerSetRate
// Apple's local file player (local_player.cpp) dropped what it queued and was not stopped since: a pause, which a stop
// follows, or a skip or a move in the song, after which it sounds again with no start. That sound is another place
// in a song: the audio thread drops what studio mode held then.
constexpr float kSilentSample = 1e-6f;
std::atomic<bool> g_localMoved{false};
std::atomic<bool> g_localMoveSeen{false};  // set by the audio thread, noted by Sing's thread
const char kLocalSong = 0;                 // g_pausedOn while the local file player is paused: its address
// A rate an audio thread met with no studio stream yet: Sing's thread builds one at its next tick.
std::atomic<int> g_wantedRate{0};
std::atomic<int> g_wantedTailRate{0};
// One studio stream per model and rate for Apple's stream and one for the deck's, built by Sing's thread, never freed:
// an audio thread may hold one at any time.
ModelStudios g_studios{};
ModelStudios g_tailStudios{};
core_sing::LightFilter g_light;      // Apple's audio thread only
core_sing::LightFilter g_tailLight;  // the deck's audio thread only
// The rate of Apple's last block in studio mode; StartStudio sets it too, for the stream it flushed itself.
std::atomic<int> g_lastRate{0};
// Light mode ran on the last block: else its filter still holds a few ms of another moment, which it forgets first.
bool g_lightOn = false;              // Apple's audio thread only
bool g_tailLightOn = false;          // the deck's audio thread only
bool g_tailOn = false;               // the deck's audio thread only: its studio stream holds the old song
std::int64_t g_tailQuiet = 0;        // the deck's audio thread only: frames fed since the deck fell silent
// Counts the mixes' second players, started and stopped. One whose stream stopped before all it held was heard (a mix
// cancelled, a song that ended) leaves that in the deck's studio stream: the next mix must not play it.
std::atomic<std::uint32_t> g_tailMix{0};
std::uint32_t g_tailMixSeen = 0;     // the deck's audio thread only

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

const core_sing::ModelSpec& Spec() { return *core_sing::kModels[g_model.load(std::memory_order_relaxed)]; }

// The streams of the model being run.
Studios& MainStudios() { return g_studios[static_cast<size_t>(g_model.load(std::memory_order_relaxed))]; }
Studios& TailStudios() { return g_tailStudios[static_cast<size_t>(g_model.load(std::memory_order_relaxed))]; }

core_sing::StudioStream* StudioFor(const Studios& studios, int rate) {
    const int index = RateIndex(rate);
    return index < 0 ? nullptr : studios[index].load(std::memory_order_acquire);
}

core_sing::StudioStream* MainStudio() { return StudioFor(MainStudios(), detail::MainStreamRate()); }

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
    return studio ? studio->Lag() : Spec().delaySeconds;
}

// A block in studio mode with no stream for its rate yet: silent, as the stream will be for its delay anyway.
void Mute(float* samples, unsigned frames) { std::fill(samples, samples + static_cast<size_t>(frames) * 2, 0.0f); }

bool Sounds(const float* samples, unsigned frames) {
    return std::any_of(samples, samples + static_cast<size_t>(frames) * 2,
                       [](float sample) { return std::fabs(sample) > kSilentSample; });
}

// The main audio thread only: true when the local file player sounds again from another place, what `studio` held
// then dropped. Before the block is processed, so none of the new place is lost.
bool LocalPlayerMoved(core_sing::StudioStream& studio, const float* samples, unsigned frames) {
    if (!g_localMoved.load(std::memory_order_relaxed) || !Sounds(samples, frames)) return false;
    if (!g_localMoved.exchange(false, std::memory_order_relaxed)) return false;
    studio.Flush(Fade::None);
    g_paused.store(false, std::memory_order_relaxed);
    g_localMoveSeen.store(true, std::memory_order_relaxed);
    return true;
}

}  // namespace

namespace detail {

void SingMain(float* samples, unsigned frames, int rate) {
    const auto mode = static_cast<Mode>(g_mode.load(std::memory_order_relaxed));
    if (mode == Mode::Light && !g_lightOn) g_light.Reset();
    g_lightOn = mode == Mode::Light;
    if (mode == Mode::Off) return;
    const double voice = g_voice.load(std::memory_order_relaxed);
    if (mode == Mode::Light) {
        g_light.Process(samples, frames, rate, voice);
        return;
    }
    if (RateIndex(rate) < 0) return;  // a rate studio mode does not run at: the song as it is
    core_sing::StudioStream* studio = StudioFor(MainStudios(), rate);
    if (!studio) {
        g_wantedRate.store(rate, std::memory_order_relaxed);
        return Mute(samples, frames);
    }
    // A stream of another rate played meanwhile: what this one holds is of an older song.
    if (g_lastRate.exchange(rate, std::memory_order_relaxed) != rate) studio->Flush(Fade::None);
    bool paused = g_paused.load(std::memory_order_relaxed);
    if (paused && LocalPlayerMoved(*studio, samples, frames)) paused = false;
    studio->Process(samples, frames, voice, paused);
}

// The deck's copy of the old song gets what Apple's stream gets: in studio mode, the same lag, so the mix stays in
// step as heard, and after the deck falls silent, what its stream still holds.
bool SingTail(float* samples, unsigned frames, int rate, bool heard) {
    if (const std::uint32_t mix = g_tailMix.load(std::memory_order_relaxed); mix != g_tailMixSeen) {
        g_tailMixSeen = mix;
        g_tailOn = g_tailLightOn = false;  // the next sound the deck gives starts a flushed stream
        g_tailQuiet = 0;
    }
    const auto mode = static_cast<Mode>(g_mode.load(std::memory_order_relaxed));
    if (mode != Mode::Studio) {
        g_tailOn = false;
        const bool light = mode == Mode::Light && heard;
        if (light && !g_tailLightOn) g_tailLight.Reset();
        g_tailLightOn = light;
        if (light) g_tailLight.Process(samples, frames, rate, g_voice.load());
        return heard;
    }
    if (RateIndex(rate) < 0) return heard;
    core_sing::StudioStream* studio = StudioFor(TailStudios(), rate);
    if (!studio) {
        g_wantedTailRate.store(rate, std::memory_order_relaxed);
        if (heard) Mute(samples, frames);
        return false;
    }
    if (!heard && !g_tailOn) return false;
    if (heard && !g_tailOn) {
        core_sing::StudioStream* main = MainStudio();
        studio->Flush(Fade::None, main ? main->Lag() : Spec().delaySeconds);
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
    FlushAll(MainStudios(), Fade::Held);
    std::lock_guard lock(g_transition);
    if (g_holding) HoldClockBack(Spec().delaySeconds);  // what a flushed stream lags by
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
    if (!sameSong) FlushAll(MainStudios(), Fade::None);
    if (g_holding) HoldClockBack(sameSong ? LagNow() : Spec().delaySeconds);
    g_localMoved.store(false, std::memory_order_relaxed);  // the local file player is left
    g_paused.store(false, std::memory_order_relaxed);
}

// The local file player starts: after its own pause, with nothing moved since, it goes on from what studio mode held.
void SingLocalPlays() {
    std::lock_guard lock(g_transition);
    const bool sameSpot = g_paused.load(std::memory_order_relaxed) && g_pausedOn == &kLocalSong &&
                          !g_localMoved.load(std::memory_order_relaxed);
    if (!sameSpot) FlushAll(MainStudios(), Fade::None);
    if (g_holding) HoldClockBack(sameSpot ? LagNow() : Spec().delaySeconds);
    g_localMoved.store(false, std::memory_order_relaxed);
    g_paused.store(false, std::memory_order_relaxed);
}

// Studio mode stops reading its stream as Apple's sound stops, and keeps what it holds until it knows why.
void SingLocalReset() {
    std::lock_guard lock(g_transition);
    g_localMoved.store(true, std::memory_order_relaxed);
    if (!g_paused.exchange(true, std::memory_order_relaxed)) g_pausedOn = &kLocalSong;
}

// The reset before it was a pause.
void SingLocalStopped() { g_localMoved.store(false, std::memory_order_relaxed); }

bool SingHoldsSound() { return g_mode.load(std::memory_order_relaxed) == static_cast<int>(Mode::Studio); }

double SingLag() {
    std::lock_guard lock(g_transition);
    return g_holding ? LagNow() : 0;
}

void SingTailNewMix() { g_tailMix.fetch_add(1, std::memory_order_relaxed); }

void SingTailDrop() { FlushAll(TailStudios(), Fade::Held); }

}  // namespace detail

namespace {

// What Sing's thread knows and tells the interface.
struct Studio {
    int index = -1;  // of core_sing::kModels, while a model is loaded
    std::unique_ptr<sing::OrtVocalModel> model;
    std::unique_ptr<core_sing::VocalSplitter> splitter;
    std::atomic<bool> installing{false};    // ampify sing install runs
    std::atomic<bool> installEnded{false};  // it ended and nobody looked yet
    std::atomic<int> installCode{0};        // then its exit code: -1 when it could not start or was stopped
    std::uint64_t installSize = 0;          // bytes it downloads
    int installModel = 0;                   // for that model
    bool failed = false;
    std::string reason;
};

constexpr std::string_view kDownloadFailed = "download failed";

std::filesystem::path SingFolder() { return KitDirectory() / L"sing"; }

const sing_pack::Model& PackModel(int index) { return *sing_pack::kModels[index]; }

// How much of a download of `total` bytes has come in, 0 to 1, from the files being written: those in place count
// whole (the runtime when only a model was fetched counts nothing, as it was not part of `total`).
double DownloadProgress(const sing_pack::Model& model, std::uint64_t total) {
    const std::filesystem::path downloads = KitDirectory() / L"downloads";
    std::uintmax_t have = 0;
    for (const sing_pack::Download* item : {&sing_pack::kRuntime, &model.download}) {
        std::error_code ec;
        std::filesystem::path part = downloads / std::string(item->name);
        part += ".part";
        if (auto size = std::filesystem::file_size(part, ec); !ec) have += size;
    }
    if (sing_pack::RuntimeReady(SingFolder()) && total > model.download.size) have += sing_pack::kRuntime.size;
    return total == 0 ? 0 : std::min(1.0, static_cast<double>(have) / static_cast<double>(total));
}

struct SingWish {
    Mode mode = Mode::Off;
    double voice = 1;
    int model = 0;  // index of core_sing::kModels
};

// What the interface asked last, through the shared block; off when there is none.
SingWish ReadWish() {
    audio::SingRequest request;
    audio::SharedAudio* block = SharedAudioBlock();
    if (!block || !audio::ReadSingRequest(*block, request)) return {};
    return {static_cast<Mode>(request.mode), request.voice, request.model == audio::SingModel::Precise ? 1 : 0};
}

audio::SingReason ReasonCode(const std::string& reason) {
    if (reason.empty()) return audio::SingReason::None;
    if (reason == "dolby atmos") return audio::SingReason::DolbyAtmos;
    if (reason == kDownloadFailed) return audio::SingReason::DownloadFailed;
    if (reason.find("graphics card") != std::string::npos) return audio::SingReason::NoGraphicsCard;
    return audio::SingReason::ModelFailed;
}

// The stream an audio thread asked for, made here for the model being run.
void Build(Studios& studios, std::atomic<int>& wanted) {
    const int rate = wanted.exchange(0, std::memory_order_relaxed);
    const int index = RateIndex(rate);
    if (index < 0 || studios[index].load(std::memory_order_acquire)) return;
    // Kept for the process's life: an audio thread may hold it at any time.
    studios[index].store(new core_sing::StudioStream(rate, Spec()), std::memory_order_release);
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
                Build(MainStudios(), g_wantedRate);
                Build(TailStudios(), g_wantedTailRate);
            }
            if (!RunModel()) std::this_thread::sleep_for(m_studioOn ? kTick : kRequestEvery);
        }
    }

private:
    Studio m_studio;
    bool m_studioOn = false;
    Mode m_reported = Mode::Off;
    Mode m_wished = Mode::Off;
    int m_wishedModel = 0;
    std::chrono::steady_clock::time_point m_restingSince{};  // studio mode last asked for, or the model loaded
    std::uint64_t m_missesNoted = 0;                         // the playing stream's misses already in the journal
    std::chrono::steady_clock::time_point m_missNotedAt{};

    // The interface's request, applied: the voice at once, the mode once what it needs is there.
    void Follow() {
        const SingWish wish = ReadWish();
        g_voice.store(static_cast<float>(wish.voice), std::memory_order_relaxed);
        if (g_localMoveSeen.exchange(false, std::memory_order_relaxed)) {
            Note("Sing: Apple's local file player moved, studio mode starts again from there");
        }
        if (wish.mode == Mode::Studio && m_wished != Mode::Studio && m_studio.reason == kDownloadFailed) {
            m_studio.failed = false;  // asked again after a failed download: it is tried again
            m_studio.reason.clear();
        }
        if (wish.model != m_wishedModel) SwitchModel(wish.model);
        m_wished = wish.mode;
        Mode effective = wish.mode;
        std::string reason;
        if (wish.mode == Mode::Studio && !StudioUsable(reason)) effective = reason.empty() ? Mode::Off : Mode::Light;
        if (effective != Mode::Studio && m_studioOn) StopStudio();
        if (effective == Mode::Studio && !m_studioOn) StartStudio();
        detail::WantStereo(wish.mode != Mode::Off);  // after StopStudio: its jump lands on the stereo version
        if (effective != Mode::Studio) g_mode.store(static_cast<int>(effective), std::memory_order_relaxed);
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

    // Another model asked for: studio mode stops (back to Apple's own time), the old model leaves the graphics card at
    // once, and whatever failed with it is forgotten; the next Follow prepares the new one.
    void SwitchModel(int model) {
        if (m_studioOn) StopStudio();
        UnloadModel();
        m_studio.failed = false;
        m_studio.reason.clear();
        m_wishedModel = model;
        g_model.store(model, std::memory_order_relaxed);  // studio mode is off: no audio thread reads the streams
    }

    void UnloadModel() {
        m_studio.splitter.reset();
        m_studio.model.reset();
        m_studio.index = -1;
        for (ModelStudios* models : {&g_studios, &g_tailStudios}) {
            for (Studios& studios : *models) {
                for (auto& slot : studios) {
                    if (core_sing::StudioStream* studio = slot.load(std::memory_order_acquire)) studio->Rest();
                }
            }
        }
    }

    // The model, its graphics card memory and the streams' spectra go once studio mode was not asked for a while.
    void RestModel(bool asked) {
        const auto now = std::chrono::steady_clock::now();
        if (asked || !m_studio.model) {
            m_restingSince = now;
            return;
        }
        if (now - m_restingSince < kModelRests) return;
        UnloadModel();
        Note("Sing: the voice model left the graphics card, studio mode unused for a while");
    }

    // Whether studio mode can run now; while it cannot, `reason` says why, or stays empty while it is being prepared
    // (the sound is then left alone rather than switched to light mode a while). A Dolby Atmos song has no stereo
    // stream and is never switched under way: Sing waits for a song in stereo (WantStereo).
    bool StudioUsable(std::string& reason) {
        if (detail::InAtmosNow()) {
            reason = "dolby atmos";
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
        if (!sing_pack::Ready(SingFolder(), PackModel(m_wishedModel))) {
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
        // An install for the model asked before this one ended: its model's own is started now.
        if (m_studio.installEnded.exchange(false) && m_studio.installModel == m_wishedModel) {
            m_studio.failed = true;
            m_studio.reason = std::string(kDownloadFailed);
            Note("Sing: ampify sing install failed (code " + std::to_string(m_studio.installCode.load()) + ")");
            return;
        }
        m_studio.installing = true;
        const sing_pack::Model& model = PackModel(m_wishedModel);
        m_studio.installSize = sing_pack::DownloadSize(SingFolder(), model);
        m_studio.installModel = m_wishedModel;
        Note("Sing: downloading the studio files of the " + std::string(model.id) + " model");
        RunAmpifyInBackground({"sing", "install", std::string(model.id), "--kit-dir", ToUtf8(KitDirectory().wstring())},
                              [this](int code) {
            m_studio.installCode = code;
            m_studio.installEnded = true;  // before `installing` goes: Install reads them the other way round
            m_studio.installing = false;
        });
    }

    void LoadModel() {
        std::string why;
        const auto started = std::chrono::steady_clock::now();
        const core_sing::ModelSpec& spec = *core_sing::kModels[m_wishedModel];
        m_studio.model = sing::OrtVocalModel::Load(SingFolder(), spec, PackModel(m_wishedModel).file, why);
        if (!m_studio.model) {
            m_studio.failed = true;
            m_studio.reason = why;
            Note("Sing: studio mode unavailable: " + why);
            return;
        }
        m_studio.splitter = std::make_unique<core_sing::VocalSplitter>(*m_studio.model);
        m_studio.index = m_wishedModel;
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        Note("Sing: the " + std::string(spec.id) + " voice model is loaded and run once on the graphics card in " +
             Fixed(ms, 0) + " ms");
    }

    // Holds the sound back from now: the stream starts again silent for the delay, Apple's player is told it is that
    // much earlier and jumps to where it is, so that it reads its time again: one short pause, then all in step. With
    // no stream yet (nothing played since the start), the first one is held from its first block, with no jump.
    void StartStudio() {
        const int rate = detail::MainStreamRate();
        g_wantedRate.store(rate, std::memory_order_relaxed);
        Build(MainStudios(), g_wantedRate);
        core_sing::StudioStream* studio = StudioFor(MainStudios(), rate);
        bool jump = false;
        {
            std::lock_guard lock(g_transition);
            if (studio) studio->Flush(Fade::Live);  // it was not playing: what it holds is old
            g_lastRate.store(studio ? rate : 0, std::memory_order_relaxed);  // any other stream met is flushed
            g_holding = true;
            g_mode.store(static_cast<int>(Mode::Studio), std::memory_order_relaxed);
            if (!g_paused.load()) {  // paused, the clock is held back when the player plays (SingPlays)
                detail::HoldClockBack(Spec().delaySeconds);
                jump = studio != nullptr;
            }
        }
        if (jump) detail::JumpInPlace(0);
        m_studioOn = true;
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
        for (Studios* studios : {&MainStudios(), &TailStudios()}) {
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
            status.delaySeconds = studio ? studio->Delay() : Spec().delaySeconds;
        } else if (wish.mode == Mode::Studio && m_studio.failed) {
            status.phase = audio::SingPhase::Failed;
        } else if (wish.mode == Mode::Studio && m_studio.installing.load()) {
            status.phase = audio::SingPhase::Downloading;
            status.progress = DownloadProgress(PackModel(m_studio.installModel), m_studio.installSize);
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
            // Never destroyed: by the time a static destructor runs at exit, Windows has ended ONNX Runtime's and
            // DirectML's threads, and releasing the model's session then crashed AMPLibraryAgent.exe in onnxruntime.dll.
            auto* thread = new SingThread();
            thread->Run();
        }).detach();
    });
}

}  // namespace detail

}  // namespace ampify::win::audio_hooks
