// The Atmos leveler's Windows side: the gain policy of the audio platform (src/win/audio/audio_hooks.hpp). It follows
// the playing item through AVFoundationCF, measures each track from the samples the platform hands it, keeps what it
// learnt in atmos-levels.txt, and turns the leveler's decision into the gain the platform applies.

#include "atmos_windows.hpp"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "core/kit_layout.hpp"
#include "format_detection.hpp"
#include "level_file.hpp"
#include "leveler.hpp"
#include "win/audio/audio_hooks.hpp"
#include "win/win_common.hpp"

namespace atmos::hooks {

namespace {

using namespace std::chrono_literals;
namespace platform = ampify::win::audio_hooks;

constexpr auto kTakeoverWait = 1500ms;  // longer than a pause takes to silence the stream
constexpr int kPollsPerReport = 30;     // about every 9 s at one Poll per 300 ms
constexpr const wchar_t* kLevelsFileName = L"atmos-levels.txt";

// AVFoundationCF (the player's item) and CoreFoundation (the objects it returns), resolved by name.
using GetRate_t = float(__cdecl*)(const void* player);
using CopyObject_t = void*(__cdecl*)(const void* object);
using CFRelease_t = void(__cdecl*)(const void* object);
using CFIndex_t = long long;
struct CFRange {
    CFIndex_t location;
    CFIndex_t length;
};
using CFArrayGetCount_t = CFIndex_t(__cdecl*)(const void* array);
using CFArrayGetValueAtIndex_t = const void*(__cdecl*)(const void* array, CFIndex_t index);
using CFStringGetLength_t = CFIndex_t(__cdecl*)(const void* string);
using CFStringGetBytes_t = CFIndex_t(__cdecl*)(const void* string, CFRange range, unsigned encoding,
                                               unsigned char lossByte, unsigned char external, unsigned char* buffer,
                                               CFIndex_t size, CFIndex_t* used);

constexpr unsigned kUtf8 = 0x08000100;

struct Api {
    CopyObject_t CopyCurrentItem = nullptr;        // AVCFPlayerCopyCurrentItem
    GetRate_t GetRate = nullptr;                   // AVCFPlayerGetRate
    CopyObject_t CopyItemTracks = nullptr;         // AVCFPlayerItemCopyTracks
    CopyObject_t CopyItemTrackAsset = nullptr;     // AVCFPlayerItemTrackCopyAssetTrack
    CopyObject_t CopyAssetFormats = nullptr;       // AVCFAssetTrackCopyFormatDescriptions
    CopyObject_t CopyStableVariant = nullptr;      // AVCFPlayerItemCopyCurrentStableVariantID
    CopyObject_t CFCopyDescription = nullptr;
    CFRelease_t CFRelease = nullptr;
    CFArrayGetCount_t CFArrayGetCount = nullptr;
    CFArrayGetValueAtIndex_t CFArrayGetValueAtIndex = nullptr;
    CFStringGetLength_t CFStringGetLength = nullptr;
    CFStringGetBytes_t CFStringGetBytes = nullptr;
};

ampify::Journal* g_journal = nullptr;  // set once, before the policy is installed
Api g_api;                              // resolved in Prepare, before the platform's hooks feed the policy

// No gain at all: no auto-level, 0 dB for every track. The leveler off still measures, so the sound hooks keep
// teaching it the levels while the samples go through untouched.
LevelerConfig NeutralConfig() {
    LevelerConfig neutral;
    neutral.autoLevel = false;
    neutral.fallbackGainDb = 0;
    neutral.maxGainDb = 0;
    return neutral;
}

// The leveler, its settings and the gain it decides. The audio threads only try to lock: a block they miss is not
// measured.
std::mutex g_levelerLock;
LevelerConfig g_config = NeutralConfig();
std::unique_ptr<AtmosLeveler> g_leveler;
int g_journaledStreamRate = 0;  // rates last written to the log
int g_journaledObjectRate = 0;

// Poll thread only: the item being followed, and the gain of its current Atmos track.
struct Playing {
    void* item = nullptr;  // retained
    std::string variant;   // its stable variant id; empty while the item loads
    bool started = false;  // the leveler knows what this item is
    std::chrono::steady_clock::time_point stoppedSince{};  // when its player was seen stopped; empty while it plays
    bool unseen = false;  // a stretch played by a player that is not followed is being measured as stereo
    double currentDb = 0;
    double reportedDb = 0;
    int polls = 0;
};
Playing g_playing;

void Note(const std::string& message) {
    if (g_journal) g_journal->Write(platform::kJournalPrefix + message);
}

std::string Fixed(double value, int decimals) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.*f", decimals, value);
    return text;
}

template <typename T>
bool Resolve(const wchar_t* module, const char* name, T& out) {
    if (platform::FindExport(module, name, out)) return true;
    Note(std::string(name) + " not found: leveler inactive");
    return false;
}

bool ResolveApi() {
    constexpr const wchar_t* av = L"AVFoundationCF.dll";
    constexpr const wchar_t* cf = L"CoreFoundation.dll";
    return Resolve(av, "AVCFPlayerCopyCurrentItem", g_api.CopyCurrentItem) &&
           Resolve(av, "AVCFPlayerGetRate", g_api.GetRate) &&
           Resolve(av, "AVCFPlayerItemCopyTracks", g_api.CopyItemTracks) &&
           Resolve(av, "AVCFPlayerItemTrackCopyAssetTrack", g_api.CopyItemTrackAsset) &&
           Resolve(av, "AVCFAssetTrackCopyFormatDescriptions", g_api.CopyAssetFormats) &&
           Resolve(av, "AVCFPlayerItemCopyCurrentStableVariantID", g_api.CopyStableVariant) &&
           Resolve(cf, "CFCopyDescription", g_api.CFCopyDescription) && Resolve(cf, "CFRelease", g_api.CFRelease) &&
           Resolve(cf, "CFArrayGetCount", g_api.CFArrayGetCount) &&
           Resolve(cf, "CFArrayGetValueAtIndex", g_api.CFArrayGetValueAtIndex) &&
           Resolve(cf, "CFStringGetLength", g_api.CFStringGetLength) &&
           Resolve(cf, "CFStringGetBytes", g_api.CFStringGetBytes);
}

std::filesystem::path LevelsFile() {
    return ampify::win::KitDirectory() / kLevelsFileName;
}

// Reads what earlier runs measured into the leveler just made.
void LoadLevels() {
    std::ifstream file(LevelsFile(), std::ios::binary);
    if (!file) {
        Note("no saved level: the stereo target builds up as tracks play");
        return;
    }
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    SavedLevels saved = ParseLevels(text);

    std::lock_guard<std::mutex> guard(g_levelerLock);
    g_leveler->Restore(saved.atmos, saved.stereo);
    Note("levels read back: " + std::to_string(g_leveler->Store().Levels().size()) + " Atmos variant(s), " +
         std::to_string(g_leveler->StereoHistory().Size()) + " stereo track(s)");
}

// Writes the levels after a track changed them. A temporary file replaces the old one, so a crash mid-write
// leaves the previous file whole. Poll thread only.
void SaveLevels(const std::string& text) {
    std::filesystem::path file = LevelsFile();
    std::filesystem::path temporary = file;
    temporary += L".tmp";

    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out << text;
        if (!out.flush()) {
            Note("levels not saved: " + ampify::win::ToUtf8(temporary.wstring()) + " cannot be written");
            return;
        }
    }
    if (!MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        Note("levels not saved: replacing " + ampify::win::ToUtf8(file.wstring()) + " was refused (" +
             std::to_string(GetLastError()) + ")");
    }
}

// A new leveler on the current settings, the levels read back when asked.
void RenewLeveler(bool loadLevels) {
    {
        std::lock_guard<std::mutex> guard(g_levelerLock);
        g_leveler = std::make_unique<AtmosLeveler>(g_config, ampify::audio::kStartSampleRate,
                                                   ampify::audio::kStartSampleRate);
    }
    if (loadLevels) LoadLevels();
}

void UseConfig(const LevelerConfig& config) {
    std::lock_guard<std::mutex> guard(g_levelerLock);
    g_config = config;
}

// Describes one CoreFoundation object as text, such as "mediaType:'soun' mediaSubType:'qec3' ...".
std::string Describe(const Api& api, const void* object) {
    void* description = api.CFCopyDescription(object);
    if (!description) return {};

    unsigned char buffer[1024] = {};
    CFIndex_t used = 0;
    api.CFStringGetBytes(description, CFRange{0, api.CFStringGetLength(description)}, kUtf8, '?', 0, buffer,
                         static_cast<CFIndex_t>(sizeof(buffer) - 1), &used);
    api.CFRelease(description);
    CFIndex_t length = std::clamp<CFIndex_t>(used, 0, static_cast<CFIndex_t>(sizeof(buffer) - 1));
    return std::string(reinterpret_cast<const char*>(buffer), static_cast<size_t>(length));
}

// Format descriptions of every track of the item.
std::vector<std::string> DescriptionsOf(const Api& api, void* item) {
    std::vector<std::string> descriptions;
    void* tracks = api.CopyItemTracks(item);
    if (!tracks) return descriptions;

    for (CFIndex_t i = 0; i < api.CFArrayGetCount(tracks); i++) {
        void* asset = api.CopyItemTrackAsset(api.CFArrayGetValueAtIndex(tracks, i));
        if (!asset) continue;
        if (void* formats = api.CopyAssetFormats(asset)) {
            for (CFIndex_t j = 0; j < api.CFArrayGetCount(formats); j++) {
                descriptions.push_back(Describe(api, api.CFArrayGetValueAtIndex(formats, j)));
            }
            api.CFRelease(formats);
        }
        api.CFRelease(asset);
    }
    api.CFRelease(tracks);
    return descriptions;
}

std::string VariantOf(const Api& api, void* item) {
    void* variant = api.CopyStableVariant(item);
    if (!variant) return {};

    std::string id = VariantIdFromDescription(Describe(api, variant));
    api.CFRelease(variant);
    return id;
}

// Starts measuring the item once it is known to be Atmos or stereo. Poll thread only.
void StartIfKnown() {
    // No stable variant yet: the item is still loading (and fetching its key). Its tracks are left alone.
    if (g_playing.variant.empty()) return;

    TrackKind kind = ClassifyDescriptions(DescriptionsOf(g_api, g_playing.item));
    if (kind == TrackKind::Unknown) return;

    std::lock_guard<std::mutex> guard(g_levelerLock);
    g_leveler->StartTrack(g_playing.variant, kind == TrackKind::Atmos);
    g_playing.started = true;
    g_playing.currentDb = g_config.fallbackGainDb;  // a new track starts at the fallback gain, as the reference does
    g_playing.reportedDb = g_playing.currentDb;
    g_playing.polls = 0;
    Note(std::string(kind == TrackKind::Atmos ? "Dolby Atmos track" : "stereo track") + ", variant " +
         g_playing.variant +
         (kind == TrackKind::Atmos ? ", starting gain " + Fixed(g_playing.currentDb, 1) + " dB" : std::string()));
}

// Sound plays and no followed player plays it: a song on a player Apple Music set no volume on, so it is not
// followed. Its stream is stereo (an Atmos song there would play through the objects, which stay silent here), so
// it is measured as one stereo track. Starting it ends the followed song, and its gain with it. Poll thread only.
void MeasureUnseen() {
    if (g_playing.unseen) return;
    std::lock_guard<std::mutex> guard(g_levelerLock);
    g_leveler->StartTrack("none", false);
    g_playing.started = false;
    g_playing.unseen = true;
    Note("stereo track, variant none (a player the leveler does not follow)");
}

// Ends that stretch: a followed player plays again, or the stream went quiet between two songs.
void EndUnseen() {
    if (!g_playing.unseen) return;
    std::lock_guard<std::mutex> guard(g_levelerLock);
    g_leveler->EndTrack();
    g_playing.unseen = false;
}

// No followed player plays, for longer than a pause takes to silence the stream: what the stream carries now comes
// from a player that is not followed. True when that is the case. Poll thread only.
bool FollowUnseen(bool stopped) {
    auto now = std::chrono::steady_clock::now();
    if (!stopped) {
        g_playing.stoppedSince = {};
    } else if (g_playing.stoppedSince == std::chrono::steady_clock::time_point{}) {
        g_playing.stoppedSince = now;
    }
    if (!stopped || now - g_playing.stoppedSince < kTakeoverWait) return false;
    if (platform::SoundIsPlaying()) {
        MeasureUnseen();
    } else {
        EndUnseen();
    }
    return true;
}

// A new item, or the same item on another variant, is a new track. A variant that goes empty again while the item
// reloads changes nothing. Takes over the reference to `item`. Poll thread only.
void FollowItem(void* item) {
    std::string variant = VariantOf(g_api, item);
    bool newItem = item != g_playing.item;
    bool newVariant = !variant.empty() && variant != g_playing.variant;
    if (newItem) {
        if (g_playing.item) g_api.CFRelease(g_playing.item);
        g_playing.item = item;
    } else {
        g_api.CFRelease(item);
    }
    if (newItem || newVariant) {
        g_playing.variant = variant;
        if (g_playing.started) {
            // What plays from now on belongs to the next track: the one that ended is measured up to here.
            std::lock_guard<std::mutex> guard(g_levelerLock);
            g_leveler->EndTrack();
        }
        g_playing.started = false;
    }
    if (!g_playing.started) StartIfKnown();
}

// Follows the player's current item and its stable variant. Poll thread only.
void FollowTrack() {
    void* player = platform::RetainFollowedPlayer();
    void* item = player ? g_api.CopyCurrentItem(player) : nullptr;
    bool stopped = !item || g_api.GetRate(player) == 0;
    if (player) g_api.CFRelease(player);

    if (FollowUnseen(stopped)) {
        if (item) g_api.CFRelease(item);
        return;
    }
    if (!item) return;
    EndUnseen();
    FollowItem(item);
}

const char* PathName(AudioPath path) {
    return path == AudioPath::Objects ? "spatial objects" : "stereo stream";
}

// Logs a measure every kPollsPerReport polls of an Atmos track. Under g_levelerLock.
void ReportMeasure() {
    if (++g_playing.polls < kPollsPerReport) return;
    g_playing.polls = 0;
    std::optional<double> measured = g_leveler->MeasuredLufs();
    std::optional<double> target = g_leveler->StereoHistory().Target(g_config.minStereoTracks);
    if (measured) {
        Note("measuring: " + Fixed(*measured, 2) + " LUFS, stereo target " +
             (target ? Fixed(*target, 2) + " LUFS" : std::string("not known yet")));
    }
}

// Decides the gain for the audio threads, logs what changed and saves what a finished track taught. Returns the
// linear gain: the Atmos gain, or 1 for stereo. Poll thread only.
float UpdateGain() {
    std::string levels;  // the file's new text, written once the lock is released
    float gain = 1.0f;
    {
        std::lock_guard<std::mutex> guard(g_levelerLock);
        bool atmos = g_playing.started && g_leveler->IsAtmos();
        if (atmos) g_playing.currentDb = g_leveler->Poll(g_playing.currentDb);
        if (atmos) gain = static_cast<float>(DbToLinear(g_playing.currentDb));

        if (atmos && std::fabs(g_playing.currentDb - g_playing.reportedDb) >= 0.5) {
            Note("gain " + Fixed(g_playing.reportedDb, 1) + " -> " + Fixed(g_playing.currentDb, 1) + " dB");
            g_playing.reportedDb = g_playing.currentDb;
        }
        if (auto finished = g_leveler->TakeFinished()) {
            Note(std::string(finished->atmos ? "Atmos track" : "stereo track") + " measured (" +
                 PathName(finished->path) + "): " + Fixed(finished->lufs, 2) + " LUFS over " +
                 std::to_string(static_cast<int>(finished->seconds)) + " s" +
                 (finished->stored ? std::string(", saved") : std::string()));
            if (finished->stored) levels = FormatLevels(g_leveler->Store(), g_leveler->StereoHistory());
        }
        if (atmos) ReportMeasure();
    }
    if (!levels.empty()) SaveLevels(levels);
    return gain;
}

class LevelerPolicy final : public platform::GainPolicy {
public:
    bool Prepare() override {
        if (!ResolveApi()) return false;
        // One meter per path: the stereo stream (for example 96 kHz) and the Atmos objects (48 kHz). Each takes its
        // stream's rate once the stream is seen.
        RenewLeveler(true);
        return true;
    }

    void StreamRate(int rate) override {
        std::lock_guard<std::mutex> guard(g_levelerLock);
        if (g_leveler) g_leveler->SetStreamRate(rate);
        if (rate == g_journaledStreamRate) return;
        g_journaledStreamRate = rate;
        Note("stereo measurement at " + std::to_string(rate) + " Hz (IAudioRenderClient, after the player volume)");
    }

    void ObjectRate(int rate) override {
        std::lock_guard<std::mutex> guard(g_levelerLock);
        if (g_leveler) g_leveler->SetObjectRate(rate);
        if (rate == g_journaledObjectRate) return;
        g_journaledObjectRate = rate;
        Note("Atmos measurement at " + std::to_string(rate) + " Hz (spatial objects, before the slider volume)");
    }

    void FeedStream(const float* samples, size_t frames, float playerVolume) override {
        // After the player volume: the leveler divides it out.
        std::unique_lock<std::mutex> lock(g_levelerLock, std::try_to_lock);
        if (lock.owns_lock() && g_leveler) g_leveler->Feed(samples, frames, playerVolume);
    }

    void FeedObjects(const std::vector<double>& framePower, float peak) override {
        std::unique_lock<std::mutex> lock(g_levelerLock, std::try_to_lock);
        if (lock.owns_lock() && g_leveler) g_leveler->FeedPower(framePower, peak);
    }

    float Poll() override {
        FollowTrack();
        return UpdateGain();
    }
};

LevelerPolicy g_policy;

// Reads the settings the leveler takes. Missing keys keep the reference mod's defaults; the gains are brought
// into 0..kGainLimitDb.
LevelerConfig ConfigFrom(const ampify::Settings& settings) {
    LevelerConfig config;
    config.autoLevel = settings.GetBool("extensions.atmos-leveler.auto_level", config.autoLevel);
    config.fallbackGainDb = settings.GetDouble("extensions.atmos-leveler.fallback_db", config.fallbackGainDb);
    config.maxGainDb = settings.GetDouble("extensions.atmos-leveler.max_db", config.maxGainDb);
    return ClampGains(config);
}

bool StartLeveler(ampify::Journal& journal, const LevelerConfig& config) {
    UseConfig(config);
    switch (platform::Start(journal)) {
    case platform::StartResult::Placed:
        return true;
    case platform::StartResult::AlreadyPlaced:
        // The hooks went in for the sound alone: the leveler takes them over with its settings.
        RenewLeveler(true);
        Note("leveler started on the hooks already in place");
        return true;
    case platform::StartResult::Failed:
        break;
    }
    UseConfig(NeutralConfig());  // hooks placed later for the sound alone carry no gain
    return false;
}

// The hooks stay in place until the process exits, so nothing is left half-patched: switched off, the leveler goes
// neutral at once instead of keeping its gain until Apple Music restarts.
void StopLeveler() {
    UseConfig(NeutralConfig());
    if (!platform::Placed()) return;
    RenewLeveler(false);
    Note("leveler stopped: the sound goes through untouched");
}

}  // namespace

ampify::ExtensionInfo MakeAtmosLevelerExtension(ampify::Journal& journal, const ampify::Settings& settings) {
    LevelerConfig config = ConfigFrom(settings);
    g_journal = &journal;
    platform::SetPolicy(&g_policy);

    ampify::ExtensionInfo info;
    info.id = "atmos-leveler";
    info.name = "Atmos leveler";
    // Off by default (startsOn in facts/release.json) until a listen test on a real Dolby Atmos track confirms the
    // tuning (leveler.hpp).
    info.enabledByDefault = ampify::IsOnByDefault("extensions", info.id);
    info.start = [&journal, config] { return StartLeveler(journal, config); };
    info.stop = [] { StopLeveler(); };
    return info;
}

}  // namespace atmos::hooks
