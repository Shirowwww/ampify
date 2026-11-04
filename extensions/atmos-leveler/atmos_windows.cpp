// The Atmos leveler's Windows side. Hooks meter and gain what Apple Music renders:
//  - stereo: the WASAPI render client (IAudioRenderClient), float32 samples, after the player volume;
//  - Dolby Atmos: the spatial audio objects, one mono buffer per object, before the slider volume. Apple Music
//    renders them through ISpatialAudioObjectRenderStreamForMetadata, hooked on the fly when it is activated;
//    the plain ISpatialAudioObjectRenderStream is hooked too, from a throwaway stream read at start.
// The slot numbers are the vtable positions of the Windows SDK interfaces, checked against the SDK headers (and
// spatial_metadata.hpp for the metadata stream).
// The volume of the player is only read, never set: the gain goes into the samples, so the slider stays put.
// A poll thread follows the playing item and turns the leveler's decision into the gain the hooks apply.

#include "atmos_windows.hpp"

#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <spatialaudioclient.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "../../src/core/audio/audio_engine.hpp"
#include "../../src/core/gain_board.hpp"
#include "../../src/win/hook.hpp"
#include "../../src/win/win_common.hpp"
#include "format_detection.hpp"
#include "gain_ramp.hpp"
#include "kit_audio.hpp"
#include "level_file.hpp"
#include "leveler.hpp"
#include "object_pass.hpp"
#include "spatial_metadata.hpp"

namespace atmos::hooks {

namespace {

using namespace std::chrono_literals;

// Vtable positions in the Windows SDK interfaces.
constexpr size_t kInitialize = 3;               // IAudioClient::Initialize
constexpr size_t kInitializeSharedStream = 20;  // IAudioClient3::InitializeSharedAudioStream
constexpr size_t kGetService = 14;              // IAudioClient::GetService
constexpr size_t kGetBuffer = 3;                // IAudioRenderClient::GetBuffer
constexpr size_t kReleaseBuffer = 4;            // IAudioRenderClient::ReleaseBuffer
constexpr size_t kActivateSpatial = 10;         // ISpatialAudioClient::ActivateSpatialAudioStream
constexpr size_t kBeginUpdating = 8;            // ISpatialAudioObjectRenderStreamBase::BeginUpdatingAudioObjects
constexpr size_t kEndUpdating = 9;              // ISpatialAudioObjectRenderStreamBase::EndUpdatingAudioObjects
constexpr size_t kActivateForCommands = 10;     // ...ForMetadata::ActivateSpatialAudioObjectForMetadataCommands
constexpr size_t kActivateForItems = 11;        // ...ForMetadata::ActivateSpatialAudioObjectForMetadataItems
constexpr size_t kObjectGetBuffer = 3;          // ISpatialAudioObjectBase::GetBuffer
constexpr size_t kObjectSetVolume = 8;          // ISpatialAudioObject::SetVolume

constexpr auto kPollPeriod = 300ms;
constexpr auto kTakeoverWait = 1500ms;  // longer than a pause takes to silence the stream
constexpr int kPollsPerReport = 30;  // about every 9 s
constexpr auto kModuleWait = 60s;
constexpr const wchar_t* kLevelsFileName = L"atmos-levels.txt";

// AVFoundationCF (the player) and CoreFoundation (the objects it returns), resolved by name.
using SetVolume_t = void(__cdecl*)(void* player, float volume);
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

using CFTypeID_t = unsigned long long;
using CFGetTypeID_t = CFTypeID_t(__cdecl*)(const void* object);
using CFNumberGetTypeID_t = CFTypeID_t(__cdecl*)();
using CFNumberGetValue_t = unsigned char(__cdecl*)(const void* number, CFIndex_t type, void* value);
using CFStringCreateWithCString_t = const void*(__cdecl*)(const void* allocator, const char* text, unsigned encoding);
using CFPreferencesCopyAppValue_t = const void*(__cdecl*)(const void* key, const void* application);

constexpr unsigned kUtf8 = 0x08000100;
constexpr CFIndex_t kCFNumberDoubleType = 13;

struct Api {
    CopyObject_t CopyCurrentItem = nullptr;        // AVCFPlayerCopyCurrentItem
    GetRate_t GetRate = nullptr;                   // AVCFPlayerGetRate
    CopyObject_t CopyItemTracks = nullptr;         // AVCFPlayerItemCopyTracks
    CopyObject_t CopyItemTrackAsset = nullptr;     // AVCFPlayerItemTrackCopyAssetTrack
    CopyObject_t CopyAssetFormats = nullptr;       // AVCFAssetTrackCopyFormatDescriptions
    CopyObject_t CopyStableVariant = nullptr;      // AVCFPlayerItemCopyCurrentStableVariantID
    CopyObject_t CFRetain = nullptr;
    CopyObject_t CFCopyDescription = nullptr;
    CFRelease_t CFRelease = nullptr;
    CFArrayGetCount_t CFArrayGetCount = nullptr;
    CFArrayGetValueAtIndex_t CFArrayGetValueAtIndex = nullptr;
    CFStringGetLength_t CFStringGetLength = nullptr;
    CFStringGetBytes_t CFStringGetBytes = nullptr;
};

// The address of a function as a plain pointer, without a cast the compiler would warn about.
template <typename Fn>
void* AddressOf(Fn fn) {
    void* address = nullptr;
    std::memcpy(&address, &fn, sizeof(address));
    return address;
}

// The other way round: a plain pointer called as a function of type Fn.
template <typename Fn>
Fn FromAddress(void* address) {
    Fn fn = nullptr;
    std::memcpy(&fn, &address, sizeof(fn));
    return fn;
}

// Everything the hooks read is set before they are installed, and the hooks live until the process exits.
ampify::Journal* g_journal = nullptr;
std::mutex g_journalLock;
Api g_api;
LevelerConfig g_config;

SetVolume_t g_setVolumeOriginal = nullptr;

// Leveler and the gain it decides. The audio threads only try to lock: a block they miss is not measured.
std::mutex g_levelerLock;
std::unique_ptr<AtmosLeveler> g_leveler;
int g_journaledStreamRate = 0;  // rates last written to the log, under g_levelerLock
int g_journaledObjectRate = 0;
std::atomic<float> g_targetLinear{1.0f};  // gain the audio threads apply now: the Atmos gain, or 1 for stereo
std::atomic<float> g_playerVolume{1.0f};  // last volume Apple Music set on the player
std::atomic<bool> g_playerVolumeSeen{false};  // once true, the player's volume replaces the stored one
std::atomic<bool> g_objectVolumeSeen{false};

// The player Apple Music set a volume on last. Retained, and changed only under g_playerLock.
std::mutex g_playerLock;
void* g_player = nullptr;
std::atomic<long long> g_lastSoundTicks{0};  // steady_clock ticks of the stereo stream's last audible block

// True while the stereo stream carried sound in the last half second.
bool SoundIsPlaying() {
    long long last = g_lastSoundTicks.load(std::memory_order_relaxed);
    auto recent = std::chrono::duration_cast<std::chrono::steady_clock::duration>(500ms).count();
    return last != 0 && std::chrono::steady_clock::now().time_since_epoch().count() - last < recent;
}

// Stereo stream. The entries are read from IAudioClient and IAudioClient3, which can have separate vtables.
using Initialize_t = HRESULT(STDMETHODCALLTYPE*)(IAudioClient*, AUDCLNT_SHAREMODE, DWORD, REFERENCE_TIME,
                                                 REFERENCE_TIME, const WAVEFORMATEX*, LPCGUID);
using InitializeShared_t = HRESULT(STDMETHODCALLTYPE*)(IAudioClient*, DWORD, UINT32, const WAVEFORMATEX*, LPCGUID);
using GetService_t = HRESULT(STDMETHODCALLTYPE*)(IAudioClient*, REFIID, void**);
using GetBuffer_t = HRESULT(STDMETHODCALLTYPE*)(IAudioRenderClient*, UINT32, BYTE**);
using ReleaseBuffer_t = HRESULT(STDMETHODCALLTYPE*)(IAudioRenderClient*, UINT32, DWORD);

Initialize_t g_initializeOriginal = nullptr;
InitializeShared_t g_initializeSharedOriginal = nullptr;
GetService_t g_getServiceOriginal = nullptr;
GetBuffer_t g_getBufferOriginal = nullptr;
ReleaseBuffer_t g_releaseBufferOriginal = nullptr;

const GUID kIidAudioRenderClient = {0xF294ACFC, 0x3146, 0x4483, {0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2}};
const GUID kSubtypeFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};

struct StreamFormat {
    int channels = 0;
    int sampleRate = 0;
};

std::mutex g_streamLock;  // g_clientFormats and the metered render client
std::map<void*, StreamFormat> g_clientFormats;
std::atomic<void*> g_meteredRender{nullptr};
std::atomic<BYTE*> g_meteredBuffer{nullptr};
std::atomic<UINT32> g_meteredBufferFrames{0};
std::atomic<int> g_meteredChannels{0};
std::atomic<int> g_meteredRate{48000};
GainRamp g_streamRamp(48000);
std::vector<float> g_streamCurve;  // audio thread only

// Spatial audio. The stream and the buffers of the pass in progress are used on the audio thread only,
// between BeginUpdatingAudioObjects and EndUpdatingAudioObjects.
using ActivateSpatial_t = HRESULT(STDMETHODCALLTYPE*)(ISpatialAudioClient*, const PROPVARIANT*, REFIID, void**);
using BeginUpdating_t = HRESULT(STDMETHODCALLTYPE*)(void* stream, UINT32*, UINT32*);
using EndUpdating_t = HRESULT(STDMETHODCALLTYPE*)(void* stream);
using ActivateObject_t = HRESULT(STDMETHODCALLTYPE*)(void* stream, AudioObjectType, void** object);
using ObjectGetBuffer_t = HRESULT(STDMETHODCALLTYPE*)(void* object, BYTE**, UINT32*);
using ObjectSetVolume_t = HRESULT(STDMETHODCALLTYPE*)(ISpatialAudioObject*, float);

ActivateSpatial_t g_activateSpatialOriginal = nullptr;
ObjectSetVolume_t g_objectSetVolumeOriginal = nullptr;

// Originals of the spatial stream and object methods, by vtable entry. The plain stream and the metadata stream
// (and their two kinds of objects) each have their own vtable, and can be live at once: one global per method
// would call the other class's original. Entries are only added, so the audio threads read without a lock.
class OriginalTable {
public:
    // The method the entry held before it was hooked. Null when this table did not hook it.
    void* Find(void** entry) const {
        size_t count = m_count.load(std::memory_order_acquire);
        for (size_t i = 0; i < count; i++) {
            if (m_slots[i].entry.load(std::memory_order_relaxed) == entry) {
                return m_slots[i].original.load(std::memory_order_acquire);
            }
        }
        return nullptr;
    }

    // Hooks the entry once. The original is published before the entry is replaced, so a call that lands in
    // the replacement at once still finds it. Returns false when the entry is refused or the table is full.
    bool Hook(void** entry, void* replacement, const char* name);

private:
    struct Slot {
        std::atomic<void**> entry{nullptr};
        std::atomic<void*> original{nullptr};
    };

    std::mutex m_lock;  // writers only
    std::array<Slot, 32> m_slots;
    std::atomic<size_t> m_count{0};
};

OriginalTable g_spatialOriginals;

std::atomic<int> g_spatialRate{0};  // 0 until a float32 mono object stream is active
std::mutex g_passLock;              // g_objectPass, replaced when a stream is activated
std::unique_ptr<ObjectPass> g_objectPass;

struct SpatialPass {
    const void* stream = nullptr;
    UINT32 frames = 0;
    std::vector<ObjectBuffer> objects;
};
SpatialPass g_pass;

// Ampify.audio (kit_audio.hpp): the equalizer, live loudness and analysis on these same buffers. It only adds steps
// to the paths below; with a flat equalizer and no element listening, the samples are untouched by it.
KitAudio g_kitAudio(ampify::audio::ProcessAudio());

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
    std::lock_guard<std::mutex> guard(g_journalLock);
    if (g_journal) g_journal->Write("Atmos: " + message);
}

std::string FileNameOf(const std::string& path) {
    return std::filesystem::path(path).filename().string();
}

// A number for the log, with a fixed number of decimals.
std::string Fixed(double value, int decimals) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.*f", decimals, value);
    return text;
}

std::string GuidText(const GUID& guid) {
    char text[40];
    std::snprintf(text, sizeof(text), "%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
                  static_cast<unsigned long>(guid.Data1), guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1],
                  guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    return text;
}

// Hooks one vtable entry and logs the module it lands in. Returns false when it is refused.
bool HookEntry(void** entry, void* replacement, void** original, const char* name) {
    std::string why;
    if (!ampify::win::HookVtableEntry(entry, replacement, original, why)) {
        Note(std::string("hook ") + name + " refused: " + why);
        return false;
    }
    Note("hook " + std::string(name) + " placed in " + FileNameOf(ampify::win::ModuleOf(*original)));
    return true;
}

bool OriginalTable::Hook(void** entry, void* replacement, const char* name) {
    std::lock_guard<std::mutex> guard(m_lock);
    size_t count = m_count.load(std::memory_order_relaxed);
    for (size_t i = 0; i < count; i++) {
        if (m_slots[i].entry.load(std::memory_order_relaxed) == entry) return true;  // hooked already
    }
    if (count == m_slots.size()) {
        Note(std::string("hook ") + name + " refused: too many spatial vtables");
        return false;
    }

    // Published first, with the method the entry holds now, which is what HookVtableEntry hands back.
    m_slots[count].original.store(*entry, std::memory_order_relaxed);
    m_slots[count].entry.store(entry, std::memory_order_relaxed);
    m_count.store(count + 1, std::memory_order_release);

    void* original = nullptr;
    if (!HookEntry(entry, replacement, &original, name)) return false;  // the slot stays, never looked up
    m_slots[count].original.store(original, std::memory_order_release);
    return true;
}

// Waits for the modules that hold the player and the objects. The kernel starts before they are loaded.
bool WaitForModules(std::initializer_list<const wchar_t*> names) {
    auto deadline = std::chrono::steady_clock::now() + kModuleWait;
    while (std::chrono::steady_clock::now() < deadline) {
        bool all = true;
        for (const wchar_t* name : names) all = all && GetModuleHandleW(name) != nullptr;
        if (all) return true;
        std::this_thread::sleep_for(200ms);
    }
    return false;
}

template <typename T>
bool Resolve(const wchar_t* module, const char* name, T& out) {
    HMODULE handle = GetModuleHandleW(module);
    FARPROC address = handle ? GetProcAddress(handle, name) : nullptr;
    if (!address) {
        Note(std::string(name) + " not found: leveler inactive");
        return false;
    }
    std::memcpy(&out, &address, sizeof(out));  // a function address of another type: copied, not cast
    return true;
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
           Resolve(cf, "CFRetain", g_api.CFRetain) && Resolve(cf, "CFCopyDescription", g_api.CFCopyDescription) &&
           Resolve(cf, "CFRelease", g_api.CFRelease) && Resolve(cf, "CFArrayGetCount", g_api.CFArrayGetCount) &&
           Resolve(cf, "CFArrayGetValueAtIndex", g_api.CFArrayGetValueAtIndex) &&
           Resolve(cf, "CFStringGetLength", g_api.CFStringGetLength) &&
           Resolve(cf, "CFStringGetBytes", g_api.CFStringGetBytes);
}

// --- Levels kept across runs ---------------------------------------------------------------------

std::filesystem::path LevelsFile() {
    return ampify::win::KitDirectory() / kLevelsFileName;
}

// Reads what earlier runs measured. Called once, before the hooks are installed.
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

// --- Following the playing item ------------------------------------------------------------------

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

// Follows the player's current item and its stable variant. Poll thread only.
void FollowTrack() {
    void* player = nullptr;
    {
        std::lock_guard<std::mutex> guard(g_playerLock);
        player = g_player;
        if (player) g_api.CFRetain(player);
    }
    void* item = player ? g_api.CopyCurrentItem(player) : nullptr;
    bool stopped = !item || g_api.GetRate(player) == 0;
    if (player) g_api.CFRelease(player);

    // No followed player plays, for longer than a pause takes to silence the stream: what the stream carries now
    // comes from a player that is not followed.
    auto now = std::chrono::steady_clock::now();
    if (!stopped) {
        g_playing.stoppedSince = {};
    } else if (g_playing.stoppedSince == std::chrono::steady_clock::time_point{}) {
        g_playing.stoppedSince = now;
    }
    if (stopped && now - g_playing.stoppedSince >= kTakeoverWait) {
        if (item) g_api.CFRelease(item);
        if (SoundIsPlaying()) {
            MeasureUnseen();
        } else {
            EndUnseen();
        }
        return;
    }
    if (!item) return;
    EndUnseen();

    // A new item, or the same item on another variant, is a new track. A variant that goes empty again while
    // the item reloads changes nothing.
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

const char* PathName(AudioPath path) {
    return path == AudioPath::Objects ? "spatial objects" : "stereo stream";
}

// Decides the gain for the audio threads, logs what changed and saves what a finished track taught.
// Poll thread only.
void UpdateGain() {
    std::string levels;  // the file's new text, written once the lock is released
    {
        std::lock_guard<std::mutex> guard(g_levelerLock);
        bool atmos = g_playing.started && g_leveler->IsAtmos();
        if (atmos) g_playing.currentDb = g_leveler->Poll(g_playing.currentDb);
        g_targetLinear = atmos ? static_cast<float>(DbToLinear(g_playing.currentDb)) : 1.0f;

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
        if (atmos && ++g_playing.polls >= kPollsPerReport) {
            g_playing.polls = 0;
            std::optional<double> measured = g_leveler->MeasuredLufs();
            std::optional<double> target = g_leveler->StereoHistory().Target(g_config.minStereoTracks);
            if (measured) {
                Note("measuring: " + Fixed(*measured, 2) + " LUFS, stereo target " +
                     (target ? Fixed(*target, 2) + " LUFS" : std::string("not known yet")));
            }
        }
    }
    if (!levels.empty()) SaveLevels(levels);
}

// --- Volume stored in Apple Music's preferences ---------------------------------------------------

// Apple Music sets no volume on a player until a song starts, but keeps the slider's gain in its preferences
// (volumeWSG, AMPLibraryAgent.exe.plist): Ampify.audio.volume() reads it until the player has one.
struct StoredVolume {
    CFGetTypeID_t GetTypeID = nullptr;
    CFNumberGetTypeID_t NumberTypeID = nullptr;
    CFNumberGetValue_t NumberGetValue = nullptr;
    CFPreferencesCopyAppValue_t CopyAppValue = nullptr;
    const void* key = nullptr;          // "volumeWSG", kept for the life of the process
    const void* application = nullptr;  // kCFPreferencesCurrentApplication
};
StoredVolume g_stored;  // poll thread only

// Optional: the leveler works without it, so a missing export is noted, never fatal.
bool ResolveStoredVolume() {
    HMODULE cf = GetModuleHandleW(L"CoreFoundation.dll");
    auto find = [cf](const char* name) { return cf ? reinterpret_cast<void*>(GetProcAddress(cf, name)) : nullptr; };
    void* application = find("kCFPreferencesCurrentApplication");
    auto createString = FromAddress<CFStringCreateWithCString_t>(find("CFStringCreateWithCString"));
    g_stored.GetTypeID = FromAddress<CFGetTypeID_t>(find("CFGetTypeID"));
    g_stored.NumberTypeID = FromAddress<CFNumberGetTypeID_t>(find("CFNumberGetTypeID"));
    g_stored.NumberGetValue = FromAddress<CFNumberGetValue_t>(find("CFNumberGetValue"));
    g_stored.CopyAppValue = FromAddress<CFPreferencesCopyAppValue_t>(find("CFPreferencesCopyAppValue"));
    if (!application || !createString || !g_stored.GetTypeID || !g_stored.NumberTypeID || !g_stored.NumberGetValue ||
        !g_stored.CopyAppValue) {
        Note("CoreFoundation preferences not found: the volume is known once a song starts");
        return false;
    }
    g_stored.application = *static_cast<const void* const*>(application);
    g_stored.key = createString(nullptr, "volumeWSG", kUtf8);
    return g_stored.application && g_stored.key;
}

void PublishStoredVolume() {
    static bool resolved = ResolveStoredVolume();
    static bool noted = false;
    if (!resolved || g_playerVolumeSeen.load()) return;
    const void* value = g_stored.CopyAppValue(g_stored.key, g_stored.application);
    if (!value) return;
    double gain = 0;
    bool number = g_stored.GetTypeID(value) == g_stored.NumberTypeID() &&
                  g_stored.NumberGetValue(value, kCFNumberDoubleType, &gain) != 0;
    g_api.CFRelease(value);
    if (!number || g_playerVolumeSeen.load()) return;
    ampify::audio::ProcessAudio().PublishPlayerVolume(gain);
    if (!noted) Note("volume read from Apple Music's preferences: " + Fixed(gain, 3));
    noted = true;
}

void PollLoop() {
    while (true) {
        std::this_thread::sleep_for(kPollPeriod);
        try {
            PublishStoredVolume();
            FollowTrack();
            UpdateGain();
        } catch (...) {
            Note("error in the follow loop: the leveler carries on without this pass");
        }
    }
}

// --- Volume of the player ------------------------------------------------------------------------

// Observes the volume Apple Music sets on its player. The volume itself is passed through unchanged, so
// the slider does not move.
void __cdecl SetVolume_Hook(void* player, float volume) {
    g_setVolumeOriginal(player, volume);

    g_playerVolume = volume;
    g_playerVolumeSeen = true;
    ampify::audio::ProcessAudio().PublishPlayerVolume(volume);  // Ampify.audio.volume(): read only, like the slider
    void* old = nullptr;
    {
        std::lock_guard<std::mutex> guard(g_playerLock);
        if (player != g_player) {
            g_api.CFRetain(player);
            old = g_player;
            g_player = player;
        }
    }
    if (old) {
        g_api.CFRelease(old);
        Note("following another player");
    }
}

// --- WASAPI: stereo stream -----------------------------------------------------------------------

bool IsFloat32(const WAVEFORMATEX* format) {
    if (!format || format->wBitsPerSample != 32) return false;
    if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return true;
    if (format->wFormatTag != WAVE_FORMAT_EXTENSIBLE || format->cbSize < 22) return false;
    return reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format)->SubFormat == kSubtypeFloat;
}

// Every (re)initialisation overwrites or erases the client's entry, so a client address reused by a
// stream in another format never inherits a stale one.
void RememberClientFormat(IAudioClient* client, const WAVEFORMATEX* format, DWORD flags, bool ok) {
    bool eligible = ok && !(flags & AUDCLNT_STREAMFLAGS_LOOPBACK) && IsFloat32(format) && format->nChannels == 2;
    std::lock_guard<std::mutex> guard(g_streamLock);
    if (!eligible) {
        g_clientFormats.erase(client);
        return;
    }
    if (g_clientFormats.size() > 32) g_clientFormats.clear();
    g_clientFormats[client] = {format->nChannels, static_cast<int>(format->nSamplesPerSec)};
}

HRESULT STDMETHODCALLTYPE Initialize_Hook(IAudioClient* self, AUDCLNT_SHAREMODE mode, DWORD flags,
                                          REFERENCE_TIME duration, REFERENCE_TIME period, const WAVEFORMATEX* format,
                                          LPCGUID session) {
    HRESULT hr = g_initializeOriginal(self, mode, flags, duration, period, format, session);
    RememberClientFormat(self, format, flags, SUCCEEDED(hr));
    return hr;
}

HRESULT STDMETHODCALLTYPE InitializeShared_Hook(IAudioClient* self, DWORD flags, UINT32 period,
                                                const WAVEFORMATEX* format, LPCGUID session) {
    HRESULT hr = g_initializeSharedOriginal(self, flags, period, format, session);
    RememberClientFormat(self, format, flags, SUCCEEDED(hr));
    return hr;
}

// The newest render client of an eligible stream becomes the metered one. Any other render client
// request stops metering rather than risk reading with the wrong format.
HRESULT STDMETHODCALLTYPE GetService_Hook(IAudioClient* self, REFIID riid, void** service) {
    HRESULT hr = g_getServiceOriginal(self, riid, service);
    if (FAILED(hr) || !service || riid != kIidAudioRenderClient) return hr;

    std::lock_guard<std::mutex> guard(g_streamLock);
    g_meteredRender.store(nullptr);
    auto it = g_clientFormats.find(self);
    if (it == g_clientFormats.end()) return hr;

    int rate = it->second.sampleRate;
    {
        // The stereo meter follows the stream's rate. The Atmos meter keeps its own.
        std::lock_guard<std::mutex> levelerGuard(g_levelerLock);
        g_leveler->SetStreamRate(rate);
        if (rate != g_journaledStreamRate) {
            g_journaledStreamRate = rate;
            Note("stereo measurement at " + std::to_string(rate) + " Hz (IAudioRenderClient, after the player volume)");
        }
    }
    g_streamRamp.SetSampleRate(rate);
    g_meteredChannels = it->second.channels;
    g_meteredRate = rate;
    g_meteredRender.store(*service);
    return hr;
}

HRESULT STDMETHODCALLTYPE GetBuffer_Hook(IAudioRenderClient* self, UINT32 frames, BYTE** data) {
    HRESULT hr = g_getBufferOriginal(self, frames, data);
    if (SUCCEEDED(hr) && data && self == g_meteredRender.load(std::memory_order_relaxed)) {
        g_meteredBufferFrames.store(frames, std::memory_order_relaxed);
        g_meteredBuffer.store(*data, std::memory_order_relaxed);
    }
    return hr;
}

// The gain the audio threads apply now: the leveler's target, times what the elements asked for with
// Ampify.audio.setGainDb. Both are applied to the samples after they are measured, never to the volume.
float AppliedGain() { return g_targetLinear.load() * ampify::ElementGains().Linear(); }

// Measures the decoded samples, then applies the gain, before Windows gets them.
HRESULT STDMETHODCALLTYPE ReleaseBuffer_Hook(IAudioRenderClient* self, UINT32 frames, DWORD flags) {
    if (frames && self == g_meteredRender.load(std::memory_order_relaxed)) {
        BYTE* data = g_meteredBuffer.exchange(nullptr, std::memory_order_relaxed);
        UINT32 count = std::min(frames, g_meteredBufferFrames.load(std::memory_order_relaxed));
        bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
        if (data && !silent && count > 0 && g_meteredChannels.load() == 2) {
            float* samples = reinterpret_cast<float*>(data);
            if (std::any_of(samples, samples + count * 2, [](float s) { return std::fabs(s) > 1e-4f; })) {
                g_lastSoundTicks.store(std::chrono::steady_clock::now().time_since_epoch().count(),
                                       std::memory_order_relaxed);
            }

            {
                // After the player volume: the leveler divides it out.
                std::unique_lock<std::mutex> lock(g_levelerLock, std::try_to_lock);
                if (lock.owns_lock() && g_leveler) g_leveler->Feed(samples, count, g_playerVolume.load());
            }

            // Ampify.audio: the loudness before the gain; the equalizer after the gain and before the knee (inside
            // ApplyCurve, inactive while flat); the analysis on what leaves for Windows.
            const int rate = g_meteredRate.load();
            const float volume = g_playerVolume.load();
            g_kitAudio.BeforeStream(samples, count, 2, rate, volume);
            bool unity = g_streamRamp.Curve(count, AppliedGain(), g_streamCurve);
            ApplyCurve(samples, count, 2, g_streamCurve, unity, g_kitAudio.StreamStage(rate), self);
            g_kitAudio.AfterStream(samples, count, 2, rate, volume);
        }
    }
    return g_releaseBufferOriginal(self, frames, flags);
}

// --- WASAPI: spatial objects (Dolby Atmos) --------------------------------------------------------

// The original behind the slot `self` was called through. Every spatial stream and object method is hooked
// through g_spatialOriginals, so it is always there.
template <typename Fn>
Fn SpatialOriginal(void* self, size_t slot) {
    return FromAddress<Fn>(g_spatialOriginals.Find(ampify::win::VtableEntry(self, slot)));
}

HRESULT STDMETHODCALLTYPE BeginUpdating_Hook(void* self, UINT32* available, UINT32* frames) {
    auto original = SpatialOriginal<BeginUpdating_t>(self, kBeginUpdating);
    if (!original) return E_UNEXPECTED;

    HRESULT hr = original(self, available, frames);
    bool active = SUCCEEDED(hr) && frames && g_spatialRate.load() != 0;
    g_pass.stream = active ? self : nullptr;
    g_pass.frames = active ? *frames : 0;
    g_pass.objects.clear();
    return hr;
}

HRESULT STDMETHODCALLTYPE ObjectGetBuffer_Hook(void* self, BYTE** buffer, UINT32* bytes) {
    auto original = SpatialOriginal<ObjectGetBuffer_t>(self, kObjectGetBuffer);
    if (!original) return E_UNEXPECTED;

    HRESULT hr = original(self, buffer, bytes);
    if (SUCCEEDED(hr) && buffer && *buffer && bytes && g_pass.stream &&
        g_pass.objects.size() < ObjectPass::kMaxObjects && *bytes >= g_pass.frames * sizeof(float)) {
        g_pass.objects.push_back({self, reinterpret_cast<float*>(*buffer)});
    }
    return hr;
}

// Meters the objects and applies the gain, once per update pass.
void ProcessSpatialPass() {
    std::lock_guard<std::mutex> passGuard(g_passLock);
    if (!g_objectPass || g_pass.frames == 0) return;

    // Ampify.audio: the equalizer runs on each object between the gain and the knee; loudness and analysis after.
    const int rate = g_spatialRate.load();
    const PassLevels& levels =
        g_objectPass->Process(g_pass.objects, g_pass.frames, AppliedGain(), g_kitAudio.ObjectStage(rate));
    g_kitAudio.AfterObjects(g_pass.objects, g_pass.frames, rate, levels);

    // Before the slider volume: measured as they are, nothing to divide out.
    std::unique_lock<std::mutex> lock(g_levelerLock, std::try_to_lock);
    if (lock.owns_lock() && g_leveler) g_leveler->FeedPower(levels.framePower, levels.peak);
}

HRESULT STDMETHODCALLTYPE EndUpdating_Hook(void* self) {
    auto original = SpatialOriginal<EndUpdating_t>(self, kEndUpdating);
    if (!original) return E_UNEXPECTED;

    if (self == g_pass.stream) ProcessSpatialPass();
    g_pass.stream = nullptr;
    return original(self);
}

// The metadata objects come from the stream's own activations: their GetBuffer is hooked on the first one.
HRESULT ActivateObject(void* self, size_t slot, AudioObjectType type, void** object, const char* name) {
    auto original = SpatialOriginal<ActivateObject_t>(self, slot);
    if (!original) return E_UNEXPECTED;

    HRESULT hr = original(self, type, object);
    if (SUCCEEDED(hr) && object && *object) {
        g_spatialOriginals.Hook(ampify::win::VtableEntry(*object, kObjectGetBuffer), AddressOf(ObjectGetBuffer_Hook),
                                name);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE ActivateForCommands_Hook(void* self, AudioObjectType type, void** object) {
    return ActivateObject(self, kActivateForCommands, type, object, "ISpatialAudioObjectForMetadataCommands::GetBuffer");
}

HRESULT STDMETHODCALLTYPE ActivateForItems_Hook(void* self, AudioObjectType type, void** object) {
    return ActivateObject(self, kActivateForItems, type, object, "ISpatialAudioObjectForMetadataItems::GetBuffer");
}

// Apple Music's Atmos stream: hooks its update pass and its object activations, once per vtable.
void HookMetadataStream(void* stream) {
    g_spatialOriginals.Hook(ampify::win::VtableEntry(stream, kBeginUpdating), AddressOf(BeginUpdating_Hook),
                            "ISpatialAudioObjectRenderStreamForMetadata::BeginUpdatingAudioObjects");
    g_spatialOriginals.Hook(ampify::win::VtableEntry(stream, kEndUpdating), AddressOf(EndUpdating_Hook),
                            "ISpatialAudioObjectRenderStreamForMetadata::EndUpdatingAudioObjects");
    g_spatialOriginals.Hook(ampify::win::VtableEntry(stream, kActivateForCommands), AddressOf(ActivateForCommands_Hook),
                            "ISpatialAudioObjectRenderStreamForMetadata::ActivateSpatialAudioObjectForMetadataCommands");
    g_spatialOriginals.Hook(ampify::win::VtableEntry(stream, kActivateForItems), AddressOf(ActivateForItems_Hook),
                            "ISpatialAudioObjectRenderStreamForMetadata::ActivateSpatialAudioObjectForMetadataItems");
}

// What ActivateSpatialAudioStream returned: checks the object format, sets the Atmos meter's rate, and hooks a
// metadata stream. The plain stream's methods were hooked at start.
void OnSpatialStreamActivated(const PROPVARIANT* params, REFIID riid, void* stream) {
    if (!params || params->vt != VT_BLOB ||
        params->blob.cbSize < sizeof(SpatialAudioObjectRenderStreamActivationParams)) {
        return;
    }

    // Both activation blobs start with the object format.
    const auto* activation =
        reinterpret_cast<const SpatialAudioObjectRenderStreamActivationParams*>(params->blob.pBlobData);
    const WAVEFORMATEX* format = activation->ObjectFormat;
    bool metadata = riid.Data1 == kIidRenderStreamForMetadata.Data1;
    Note(std::string("spatial stream activated: ") +
         (metadata ? "ISpatialAudioObjectRenderStreamForMetadata" : "ISpatialAudioObjectRenderStream") + " (IID " +
         GuidText(riid) + (metadata && riid != kIidRenderStreamForMetadata ? ", different from the SDK one" : "") + ")");
    if (!IsFloat32(format) || format->nChannels != 1) {
        Note("spatial stream in an unsupported format: no leveling on this stream");
        return;
    }

    int rate = static_cast<int>(format->nSamplesPerSec);
    {
        std::lock_guard<std::mutex> guard(g_passLock);
        g_objectPass = std::make_unique<ObjectPass>(rate);
    }
    {
        // The Atmos meter follows the objects' rate. The stereo meter keeps its own.
        std::lock_guard<std::mutex> guard(g_levelerLock);
        g_leveler->SetObjectRate(rate);
        if (rate != g_journaledObjectRate) {
            g_journaledObjectRate = rate;
            Note("Atmos measurement at " + std::to_string(rate) + " Hz (spatial objects, before the slider volume)");
        }
    }
    g_spatialRate = rate;
    if (metadata && stream) HookMetadataStream(stream);
}

HRESULT STDMETHODCALLTYPE ActivateSpatial_Hook(ISpatialAudioClient* self, const PROPVARIANT* params, REFIID riid,
                                               void** stream) {
    HRESULT hr = g_activateSpatialOriginal(self, params, riid, stream);
    if (SUCCEEDED(hr) && stream) OnSpatialStreamActivated(params, riid, *stream);
    return hr;
}

// Logs once whether Apple Music sets a volume per object. The objects are measured before any volume
// either way, so it changes nothing to the measurement.
HRESULT STDMETHODCALLTYPE ObjectSetVolume_Hook(ISpatialAudioObject* self, float volume) {
    if (volume < 0.999f && !g_objectVolumeSeen.exchange(true)) {
        Note("per-object volume seen (" + Fixed(volume, 3) + "): the measurement reads the buffers before this volume");
    }
    return g_objectSetVolumeOriginal(self, volume);
}

// --- Vtable entries, read from throwaway objects of this process ---------------------------------

// The entries to hook. The stereo lists hold one entry per interface pointer that can call the method.
struct AudioEntries {
    std::vector<void**> initialize;
    std::vector<void**> getService;
    void** initializeShared = nullptr;
    void** getBuffer = nullptr;
    void** releaseBuffer = nullptr;
    void** activateSpatial = nullptr;
    void** beginUpdating = nullptr;
    void** endUpdating = nullptr;
    void** objectGetBuffer = nullptr;
    void** objectSetVolume = nullptr;
};

// The spatial classes: a throwaway stream with one static object on the default device.
void ReadSpatialEntries(IMMDevice* device, AudioEntries& out) {
    ISpatialAudioClient* client = nullptr;
    IAudioFormatEnumerator* formats = nullptr;
    ISpatialAudioObjectRenderStream* stream = nullptr;
    ISpatialAudioObject* object = nullptr;
    WAVEFORMATEX* format = nullptr;
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    bool activated = SUCCEEDED(device->Activate(__uuidof(ISpatialAudioClient), CLSCTX_ALL, nullptr,
                                                reinterpret_cast<void**>(&client)));
    // The activation is hooked whenever there is a client: Apple Music's metadata stream is found through it,
    // even where the throwaway plain stream below cannot be made.
    if (activated) out.activateSpatial = ampify::win::VtableEntry(client, kActivateSpatial);
    if (activated && SUCCEEDED(client->GetSupportedAudioObjectFormatEnumerator(&formats)) &&
        SUCCEEDED(formats->GetFormat(0, &format))) {
        SpatialAudioObjectRenderStreamActivationParams activation{};
        activation.ObjectFormat = format;
        activation.StaticObjectTypeMask = AudioObjectType_FrontLeft;
        activation.Category = AudioCategory_Other;
        activation.EventHandle = event;

        PROPVARIANT variant{};
        variant.vt = VT_BLOB;
        variant.blob.cbSize = sizeof(activation);
        variant.blob.pBlobData = reinterpret_cast<BYTE*>(&activation);
        if (SUCCEEDED(client->ActivateSpatialAudioStream(&variant, __uuidof(ISpatialAudioObjectRenderStream),
                                                         reinterpret_cast<void**>(&stream))) &&
            SUCCEEDED(stream->ActivateSpatialAudioObject(AudioObjectType_FrontLeft, &object))) {
            out.beginUpdating = ampify::win::VtableEntry(stream, kBeginUpdating);
            out.endUpdating = ampify::win::VtableEntry(stream, kEndUpdating);
            out.objectGetBuffer = ampify::win::VtableEntry(object, kObjectGetBuffer);
            out.objectSetVolume = ampify::win::VtableEntry(object, kObjectSetVolume);
        }
    }

    if (object) object->Release();
    if (stream) stream->Release();
    if (formats) formats->Release();
    if (client) client->Release();
    if (format) CoTaskMemFree(format);
    if (event) CloseHandle(event);
}

// The stereo classes: a throwaway shared stream on the default device. Returns false when it cannot be read.
bool ReadAudioEntries(AudioEntries& out) {
    const GUID clsid = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
    const GUID iidEnumerator = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
    const GUID iidClient = {0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
    const GUID iidClient3 = {0x7ED4EE07, 0x8E67, 0x4CD4, {0x8C, 0x1A, 0x2B, 0x7A, 0x59, 0x87, 0xAD, 0x42}};

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* client = nullptr;
    IAudioRenderClient* render = nullptr;
    WAVEFORMATEX* format = nullptr;

    bool ok = SUCCEEDED(CoCreateInstance(clsid, nullptr, CLSCTX_ALL, iidEnumerator,
                                         reinterpret_cast<void**>(&enumerator))) &&
              SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device)) &&
              SUCCEEDED(device->Activate(iidClient, CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client))) &&
              SUCCEEDED(client->GetMixFormat(&format)) &&
              SUCCEEDED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 1000000, 0, format, nullptr)) &&
              SUCCEEDED(client->GetService(kIidAudioRenderClient, reinterpret_cast<void**>(&render)));
    if (ok) {
        out.initialize.push_back(ampify::win::VtableEntry(client, kInitialize));
        out.getService.push_back(ampify::win::VtableEntry(client, kGetService));
        out.getBuffer = ampify::win::VtableEntry(render, kGetBuffer);
        out.releaseBuffer = ampify::win::VtableEntry(render, kReleaseBuffer);

        IAudioClient3* client3 = nullptr;
        if (SUCCEEDED(client->QueryInterface(iidClient3, reinterpret_cast<void**>(&client3)))) {
            out.initialize.push_back(ampify::win::VtableEntry(client3, kInitialize));
            out.getService.push_back(ampify::win::VtableEntry(client3, kGetService));
            out.initializeShared = ampify::win::VtableEntry(client3, kInitializeSharedStream);
            client3->Release();
        }
        ReadSpatialEntries(device, out);
    }

    if (render) render->Release();
    if (format) CoTaskMemFree(format);
    if (client) client->Release();
    if (device) device->Release();
    if (enumerator) enumerator->Release();
    return ok && !out.initialize.empty() && !out.getService.empty() && out.getBuffer && out.releaseBuffer;
}

// Installs every hook whose entry could be read. Returns how many were installed.
int InstallAudioHooks(AudioEntries& entries) {
    int installed = 0;
    // Each entry lands in its own vtable, so each one is hooked and logged on its own.
    for (void** entry : entries.initialize) {
        installed += HookEntry(entry, AddressOf(Initialize_Hook), reinterpret_cast<void**>(&g_initializeOriginal),
                               "IAudioClient::Initialize");
    }
    if (entries.initializeShared) {
        installed += HookEntry(entries.initializeShared, AddressOf(InitializeShared_Hook),
                               reinterpret_cast<void**>(&g_initializeSharedOriginal),
                               "IAudioClient3::InitializeSharedAudioStream");
    }
    for (void** entry : entries.getService) {
        installed += HookEntry(entry, AddressOf(GetService_Hook), reinterpret_cast<void**>(&g_getServiceOriginal),
                               "IAudioClient::GetService");
    }
    installed += HookEntry(entries.getBuffer, AddressOf(GetBuffer_Hook), reinterpret_cast<void**>(&g_getBufferOriginal),
                           "IAudioRenderClient::GetBuffer");
    installed += HookEntry(entries.releaseBuffer, AddressOf(ReleaseBuffer_Hook),
                           reinterpret_cast<void**>(&g_releaseBufferOriginal), "IAudioRenderClient::ReleaseBuffer");

    if (!entries.activateSpatial) {
        Note("spatial objects not available on the default device: Dolby Atmos not leveled");
        return installed;
    }
    // The activation hook is what finds Apple Music's metadata stream; the plain stream's classes come
    // from the throwaway stream.
    installed += HookEntry(entries.activateSpatial, AddressOf(ActivateSpatial_Hook),
                           reinterpret_cast<void**>(&g_activateSpatialOriginal),
                           "ISpatialAudioClient::ActivateSpatialAudioStream");
    if (entries.beginUpdating && entries.endUpdating && entries.objectGetBuffer && entries.objectSetVolume) {
        installed += g_spatialOriginals.Hook(entries.beginUpdating, AddressOf(BeginUpdating_Hook),
                                             "ISpatialAudioObjectRenderStream::BeginUpdatingAudioObjects");
        installed += g_spatialOriginals.Hook(entries.endUpdating, AddressOf(EndUpdating_Hook),
                                             "ISpatialAudioObjectRenderStream::EndUpdatingAudioObjects");
        installed += g_spatialOriginals.Hook(entries.objectGetBuffer, AddressOf(ObjectGetBuffer_Hook),
                                             "ISpatialAudioObject::GetBuffer");
        installed += HookEntry(entries.objectSetVolume, AddressOf(ObjectSetVolume_Hook),
                               reinterpret_cast<void**>(&g_objectSetVolumeOriginal), "ISpatialAudioObject::SetVolume");
    }
    return installed;
}

// Starts the leveler: waits for the Apple Music modules, hooks the player volume and the audio, then
// runs the poll thread. Called once, from the kernel's thread for this process.
std::mutex g_startLock;  // StartLeveler runs once; a second call only changes the settings
bool g_hooksPlaced = false;

// soundOnly: the hooks are wanted for the sound alone (StartSoundHooks), and settings already in place stay.
bool StartLeveler(ampify::Journal& journal, const LevelerConfig& config, bool soundOnly = false) {
    std::lock_guard<std::mutex> start(g_startLock);
    if (soundOnly && g_hooksPlaced) return true;
    {
        std::lock_guard<std::mutex> guard(g_journalLock);
        g_journal = &journal;
    }
    if (soundOnly) Note("the sound is wanted with the leveler off: the hooks go in with no gain");
    if (g_hooksPlaced) {
        // The hooks went in for the sound alone (StartSoundHooks): the leveler takes them over with its settings.
        {
            std::lock_guard<std::mutex> guard(g_levelerLock);
            g_config = config;
            g_leveler = std::make_unique<AtmosLeveler>(config, 48000, 48000);
        }
        LoadLevels();
        Note("leveler started on the hooks already in place");
        return true;
    }
    g_config = config;

    if (!WaitForModules({L"AVFoundationCF.dll", L"CoreFoundation.dll"})) {
        Note("AVFoundationCF.dll or CoreFoundation.dll missing after 60 s: leveler inactive");
        return false;
    }
    if (!ResolveApi()) return false;

    {
        // One meter per path: the stereo stream (for example 96 kHz) and the Atmos objects (48 kHz).
        // Each takes its stream's rate once the stream is seen.
        std::lock_guard<std::mutex> guard(g_levelerLock);
        g_leveler = std::make_unique<AtmosLeveler>(config, 48000, 48000);
    }
    LoadLevels();

    // The player's volume is only read: the hook passes it on unchanged, so the slider does not move.
    size_t volumeImports = ampify::win::HookImportEverywhere(
        "AVFoundationCF.dll", "AVCFPlayerSetVolume", AddressOf(SetVolume_Hook),
        reinterpret_cast<void**>(&g_setVolumeOriginal));
    if (volumeImports == 0) {
        Note("AVCFPlayerSetVolume is imported by no module: volume not followed, leveler inactive");
        return false;
    }
    Note("hook AVCFPlayerSetVolume placed in " + std::to_string(volumeImports) + " import(s)");

    // The kernel's thread may not be in a COM apartment yet. The throwaway objects need one.
    HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    AudioEntries entries;
    bool readable = (SUCCEEDED(com) || com == RPC_E_CHANGED_MODE) && ReadAudioEntries(entries);
    if (SUCCEEDED(com)) CoUninitialize();
    if (!readable) {
        Note("audio vtables unreadable on the default device: no gain applied");
        return false;
    }
    int installed = InstallAudioHooks(entries);
    if (installed == 0) return false;

    std::thread(PollLoop).detach();
    g_hooksPlaced = true;
    Note("leveler started: " + std::to_string(installed) + " audio hook(s)");
    return true;
}

// No gain at all: no auto-level, 0 dB for every track. The samples go through untouched.
LevelerConfig NeutralConfig() {
    LevelerConfig neutral;
    neutral.autoLevel = false;
    neutral.fallbackGainDb = 0;
    neutral.maxGainDb = 0;
    return neutral;
}

void StopLeveler() {
    std::lock_guard<std::mutex> start(g_startLock);
    if (!g_hooksPlaced) return;
    {
        std::lock_guard<std::mutex> guard(g_levelerLock);
        g_config = NeutralConfig();
        g_leveler = std::make_unique<AtmosLeveler>(g_config, 48000, 48000);
    }
    Note("leveler stopped: the sound goes through untouched");
}

// Reads the settings the leveler takes. Missing keys keep the reference mod's defaults; the gains are brought
// into 0..15 dB.
LevelerConfig ConfigFrom(const ampify::Settings& settings) {
    LevelerConfig config;
    config.autoLevel = settings.GetBool("extensions.atmos-leveler.auto_level", config.autoLevel);
    config.fallbackGainDb = settings.GetDouble("extensions.atmos-leveler.fallback_db", config.fallbackGainDb);
    config.maxGainDb = settings.GetDouble("extensions.atmos-leveler.max_db", config.maxGainDb);
    return ClampGains(config);
}

}  // namespace

ampify::ExtensionInfo MakeAtmosLevelerExtension(ampify::Journal& journal, const ampify::Settings& settings) {
    LevelerConfig config = ConfigFrom(settings);

    ampify::ExtensionInfo info;
    info.id = "atmos-leveler";
    info.name = "Atmos leveler";
    info.enabledByDefault = false;  // opt-in until it is tested on a real Dolby Atmos track
    info.start = [&journal, config] { return StartLeveler(journal, config); };
    // The hooks stay in place until the process exits, so nothing is left half-patched: switched off, the leveler
    // goes neutral at once instead of keeping its gain until Apple Music restarts.
    info.stop = [] { StopLeveler(); };
    return info;
}

bool StartSoundHooks(ampify::Journal& journal) {
    return StartLeveler(journal, NeutralConfig(), true);
}

}  // namespace atmos::hooks
