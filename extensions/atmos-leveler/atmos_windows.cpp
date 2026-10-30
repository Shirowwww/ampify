// The Atmos leveler's Windows side. Hooks meter and gain what Apple Music renders:
//  - stereo: the WASAPI render client (IAudioRenderClient), float32 samples;
//  - Dolby Atmos: the spatial audio objects (ISpatialAudioObjectRenderStream), one mono buffer per object.
// The slot numbers are the vtable positions of the Windows SDK interfaces, checked against the MinGW headers.
// The volume of the player is only read, never set: the gain goes into the samples, so the slider stays put.
// A poll thread follows the playing item and turns the leveler's decision into the gain the hooks apply.

#include "atmos_windows.hpp"

#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <spatialaudioclient.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "../../win/hook.hpp"
#include "format_detection.hpp"
#include "gain_ramp.hpp"
#include "leveler.hpp"
#include "object_pass.hpp"

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
constexpr size_t kBeginUpdating = 8;            // ISpatialAudioObjectRenderStream::BeginUpdatingAudioObjects
constexpr size_t kEndUpdating = 9;              // ISpatialAudioObjectRenderStream::EndUpdatingAudioObjects
constexpr size_t kObjectGetBuffer = 3;          // ISpatialAudioObject::GetBuffer
constexpr size_t kObjectSetVolume = 8;          // ISpatialAudioObject::SetVolume

constexpr auto kPollPeriod = 300ms;
constexpr int kPollsPerReport = 30;  // about every 9 s
constexpr auto kModuleWait = 60s;

// AVFoundationCF (the player) and CoreFoundation (the objects it returns), resolved by name.
using SetVolume_t = void(__cdecl*)(void* player, float volume);
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

// Everything the hooks read is set before they are installed, and the hooks live until the process exits.
amk::Journal* g_journal = nullptr;
std::mutex g_journalLock;
Api g_api;
LevelerConfig g_config;

SetVolume_t g_setVolumeOriginal = nullptr;

// Leveler and the gain it decides. The audio threads only try to lock: a block they miss is not measured.
std::mutex g_levelerLock;
std::unique_ptr<AtmosLeveler> g_leveler;
int g_meterRate = 0;  // sample rate the meter runs at: the first stream's. Other rates are not measured.
std::atomic<float> g_targetLinear{1.0f};  // gain the audio threads apply now: the Atmos gain, or 1 for stereo
std::atomic<float> g_playerVolume{1.0f};  // last volume Apple Music set on the player
std::atomic<bool> g_objectVolumeUsed{false};

// The player Apple Music set a volume on last. Retained, and changed only under g_playerLock.
std::mutex g_playerLock;
void* g_player = nullptr;

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
std::atomic<int> g_meteredRate{0};
GainRamp g_streamRamp(48000);
std::vector<float> g_streamCurve;  // audio thread only

// Spatial audio. The stream and the buffers of the pass in progress are used on the audio thread only,
// between BeginUpdatingAudioObjects and EndUpdatingAudioObjects.
using ActivateSpatial_t = HRESULT(STDMETHODCALLTYPE*)(ISpatialAudioClient*, const PROPVARIANT*, REFIID, void**);
using BeginUpdating_t = HRESULT(STDMETHODCALLTYPE*)(ISpatialAudioObjectRenderStream*, UINT32*, UINT32*);
using EndUpdating_t = HRESULT(STDMETHODCALLTYPE*)(ISpatialAudioObjectRenderStream*);
using ObjectGetBuffer_t = HRESULT(STDMETHODCALLTYPE*)(ISpatialAudioObject*, BYTE**, UINT32*);
using ObjectSetVolume_t = HRESULT(STDMETHODCALLTYPE*)(ISpatialAudioObject*, float);

ActivateSpatial_t g_activateSpatialOriginal = nullptr;
BeginUpdating_t g_beginUpdatingOriginal = nullptr;
EndUpdating_t g_endUpdatingOriginal = nullptr;
ObjectGetBuffer_t g_objectGetBufferOriginal = nullptr;
ObjectSetVolume_t g_objectSetVolumeOriginal = nullptr;

std::atomic<int> g_spatialRate{0};  // 0 until a float32 mono object stream is active
std::mutex g_passLock;              // g_objectPass, replaced when a stream is activated
std::unique_ptr<ObjectPass> g_objectPass;

struct SpatialPass {
    const void* stream = nullptr;
    UINT32 frames = 0;
    std::vector<ObjectBuffer> objects;
};
SpatialPass g_pass;

// Poll thread only: the item being followed, and the gain of its current Atmos track.
struct Playing {
    void* item = nullptr;  // retained
    bool started = false;  // the leveler knows what this item is
    double currentDb = 0;
    double reportedDb = 0;
    int polls = 0;
};
Playing g_playing;

void Note(const std::string& message) {
    std::lock_guard<std::mutex> guard(g_journalLock);
    if (g_journal) g_journal->Write("Atmos : " + message);
}

std::string FileNameOf(const std::string& path) {
    return std::filesystem::path(path).filename().string();
}

// A number for the journal, with a fixed number of decimals.
std::string Fixed(double value, int decimals) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.*f", decimals, value);
    return text;
}

// Hooks one vtable entry and journals the module it lands in. Returns false when it is refused.
bool HookEntry(void** entry, void* replacement, void** original, const char* name) {
    std::string why;
    if (!amk::win::HookVtableEntry(entry, replacement, original, why)) {
        Note(std::string("hook ") + name + " refusé : " + why);
        return false;
    }
    Note("hook " + std::string(name) + " posé dans " + FileNameOf(amk::win::ModuleOf(*original)));
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
        Note(std::string(name) + " introuvable : nivellement inactif");
        return false;
    }
    std::memcpy(&out, &address, sizeof(out));  // a function address of another type: copied, not cast
    return true;
}

bool ResolveApi() {
    constexpr const wchar_t* av = L"AVFoundationCF.dll";
    constexpr const wchar_t* cf = L"CoreFoundation.dll";
    return Resolve(av, "AVCFPlayerCopyCurrentItem", g_api.CopyCurrentItem) &&
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

// The leveler's meter runs at one sample rate. The first stream sets it; a stream at another rate is
// not measured (its gain is still applied). Caller holds g_levelerLock.
bool LockMeterRate(int sampleRate, const char* what) {
    if (g_meterRate == 0) {
        g_meterRate = sampleRate;
        g_leveler->SetSampleRate(sampleRate);
        Note("mesure à " + std::to_string(sampleRate) + " Hz, débit du premier flux (" + what + ")");
    } else if (sampleRate != g_meterRate) {
        Note(std::string(what) + " à " + std::to_string(sampleRate) + " Hz : pas mesuré, le débit de mesure est " +
             std::to_string(g_meterRate) + " Hz");
        return false;
    }
    return true;
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
    TrackKind kind = ClassifyDescriptions(DescriptionsOf(g_api, g_playing.item));
    if (kind == TrackKind::Unknown) return;

    std::string variant = VariantOf(g_api, g_playing.item);
    std::lock_guard<std::mutex> guard(g_levelerLock);
    g_leveler->StartTrack(variant, kind == TrackKind::Atmos);
    g_playing.started = true;
    g_playing.currentDb = g_config.fallbackGainDb;  // a new track starts at the fallback gain, as the reference does
    g_playing.reportedDb = g_playing.currentDb;
    g_playing.polls = 0;
    Note(std::string(kind == TrackKind::Atmos ? "piste Dolby Atmos" : "piste stéréo") + ", variante " +
         (variant.empty() ? std::string("inconnue") : variant) +
         (kind == TrackKind::Atmos ? ", gain de départ " + Fixed(g_playing.currentDb, 1) + " dB" : std::string()));
}

// Follows the player's current item. Poll thread only.
void FollowTrack() {
    void* player = nullptr;
    {
        std::lock_guard<std::mutex> guard(g_playerLock);
        player = g_player;
        if (player) g_api.CFRetain(player);
    }
    if (!player) return;

    void* item = g_api.CopyCurrentItem(player);
    g_api.CFRelease(player);
    if (!item) return;

    if (item != g_playing.item) {
        if (g_playing.item) g_api.CFRelease(g_playing.item);
        g_playing.item = item;
        g_playing.started = false;
    } else {
        g_api.CFRelease(item);
    }
    if (!g_playing.started) StartIfKnown();
}

// Decides the gain for the audio threads and journals what changed. Poll thread only.
void UpdateGain() {
    std::lock_guard<std::mutex> guard(g_levelerLock);
    bool atmos = g_playing.started && g_leveler->IsAtmos();
    if (atmos) g_playing.currentDb = g_leveler->Poll(g_playing.currentDb);
    g_targetLinear = atmos ? static_cast<float>(DbToLinear(g_playing.currentDb)) : 1.0f;

    if (atmos && std::fabs(g_playing.currentDb - g_playing.reportedDb) >= 0.5) {
        Note("gain " + Fixed(g_playing.reportedDb, 1) + " -> " + Fixed(g_playing.currentDb, 1) + " dB");
        g_playing.reportedDb = g_playing.currentDb;
    }
    if (auto finished = g_leveler->TakeFinished()) {
        Note(std::string(finished->atmos ? "morceau Atmos" : "morceau stéréo") + " mesuré : " +
             Fixed(finished->lufs, 2) + " LUFS sur " + std::to_string(static_cast<int>(finished->seconds)) + " s");
    }
    if (atmos && ++g_playing.polls >= kPollsPerReport) {
        g_playing.polls = 0;
        std::optional<double> measured = g_leveler->MeasuredLufs();
        std::optional<double> target = g_leveler->StereoHistory().Target(g_config.minStereoTracks);
        if (measured) {
            Note("mesure en cours : " + Fixed(*measured, 2) + " LUFS, cible stéréo " +
                 (target ? Fixed(*target, 2) + " LUFS" : std::string("pas encore connue")));
        }
    }
}

void PollLoop() {
    while (true) {
        std::this_thread::sleep_for(kPollPeriod);
        try {
            FollowTrack();
            UpdateGain();
        } catch (...) {
            Note("erreur dans la boucle de suivi : le nivellement continue sans ce passage");
        }
    }
}

// --- Volume of the player ------------------------------------------------------------------------

// Observes the volume Apple Music sets on its player. The volume itself is passed through unchanged, so
// the slider does not move.
void __cdecl SetVolume_Hook(void* player, float volume) {
    g_setVolumeOriginal(player, volume);

    g_playerVolume = volume;
    void* old = nullptr;
    {
        std::lock_guard<std::mutex> guard(g_playerLock);
        if (player != g_player) {
            g_api.CFRetain(player);
            old = g_player;
            g_player = player;
        }
    }
    if (old) g_api.CFRelease(old);
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

    {
        std::lock_guard<std::mutex> levelerGuard(g_levelerLock);
        LockMeterRate(it->second.sampleRate, "flux stéréo");
    }
    g_streamRamp.SetSampleRate(it->second.sampleRate);
    g_meteredChannels = it->second.channels;
    g_meteredRate = it->second.sampleRate;
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

// Measures the decoded samples, then applies the gain, before Windows gets them.
HRESULT STDMETHODCALLTYPE ReleaseBuffer_Hook(IAudioRenderClient* self, UINT32 frames, DWORD flags) {
    if (frames && self == g_meteredRender.load(std::memory_order_relaxed)) {
        BYTE* data = g_meteredBuffer.exchange(nullptr, std::memory_order_relaxed);
        UINT32 count = std::min(frames, g_meteredBufferFrames.load(std::memory_order_relaxed));
        bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
        if (data && !silent && count > 0 && g_meteredChannels.load() == 2) {
            float* samples = reinterpret_cast<float*>(data);
            int rate = g_meteredRate.load();

            {
                std::unique_lock<std::mutex> lock(g_levelerLock, std::try_to_lock);
                if (lock.owns_lock() && g_leveler && rate == g_meterRate) {
                    g_leveler->Feed(samples, count, g_playerVolume.load());
                }
            }

            bool unity = g_streamRamp.Curve(count, g_targetLinear.load(), g_streamCurve);
            if (!unity) ApplyCurve(samples, count, 2, g_streamCurve);
        }
    }
    return g_releaseBufferOriginal(self, frames, flags);
}

// --- WASAPI: spatial objects (Dolby Atmos) --------------------------------------------------------

HRESULT STDMETHODCALLTYPE ActivateSpatial_Hook(ISpatialAudioClient* self, const PROPVARIANT* params, REFIID riid,
                                               void** stream) {
    HRESULT hr = g_activateSpatialOriginal(self, params, riid, stream);
    if (FAILED(hr) || !params || params->vt != VT_BLOB ||
        params->blob.cbSize < sizeof(SpatialAudioObjectRenderStreamActivationParams)) {
        return hr;
    }

    const auto* activation =
        reinterpret_cast<const SpatialAudioObjectRenderStreamActivationParams*>(params->blob.pBlobData);
    const WAVEFORMATEX* format = activation->ObjectFormat;
    if (!IsFloat32(format) || format->nChannels != 1) {
        Note("flux spatial en un format non pris en charge : pas de nivellement sur ce flux");
        return hr;
    }

    int rate = static_cast<int>(format->nSamplesPerSec);
    {
        std::lock_guard<std::mutex> guard(g_passLock);
        g_objectPass = std::make_unique<ObjectPass>(rate);
    }
    {
        std::lock_guard<std::mutex> guard(g_levelerLock);
        LockMeterRate(rate, "objets spatiaux");
    }
    g_spatialRate = rate;
    Note("flux spatial : objets float32 mono à " + std::to_string(rate) + " Hz");
    return hr;
}

HRESULT STDMETHODCALLTYPE BeginUpdating_Hook(ISpatialAudioObjectRenderStream* self, UINT32* available,
                                             UINT32* frames) {
    HRESULT hr = g_beginUpdatingOriginal(self, available, frames);
    bool active = SUCCEEDED(hr) && frames && g_spatialRate.load() != 0;
    g_pass.stream = active ? self : nullptr;
    g_pass.frames = active ? *frames : 0;
    g_pass.objects.clear();
    return hr;
}

HRESULT STDMETHODCALLTYPE ObjectGetBuffer_Hook(ISpatialAudioObject* self, BYTE** buffer, UINT32* bytes) {
    HRESULT hr = g_objectGetBufferOriginal(self, buffer, bytes);
    if (SUCCEEDED(hr) && buffer && *buffer && bytes && g_pass.stream &&
        g_pass.objects.size() < ObjectPass::kMaxObjects && *bytes >= g_pass.frames * sizeof(float)) {
        g_pass.objects.push_back({self, reinterpret_cast<float*>(*buffer)});
    }
    return hr;
}

// If Apple Music applies its volume to each object, the buffers are not scaled by the player volume.
HRESULT STDMETHODCALLTYPE ObjectSetVolume_Hook(ISpatialAudioObject* self, float volume) {
    if (volume < 0.999f && !g_objectVolumeUsed.exchange(true)) {
        Note("volume par objet observé (" + Fixed(volume, 3) + ") : les tampons sont lus sans volume");
    }
    return g_objectSetVolumeOriginal(self, volume);
}

// Meters the objects and applies the gain, once per update pass.
void ProcessSpatialPass() {
    std::lock_guard<std::mutex> passGuard(g_passLock);
    if (!g_objectPass || g_pass.frames == 0) return;

    const PassLevels& levels = g_objectPass->Process(g_pass.objects, g_pass.frames, g_targetLinear.load());

    float volume = g_objectVolumeUsed ? 1.0f : g_playerVolume.load();
    std::unique_lock<std::mutex> lock(g_levelerLock, std::try_to_lock);
    if (lock.owns_lock() && g_leveler && g_spatialRate.load() == g_meterRate) {
        g_leveler->FeedPower(levels.framePower, levels.peak, volume);
    }
}

HRESULT STDMETHODCALLTYPE EndUpdating_Hook(ISpatialAudioObjectRenderStream* self) {
    if (self == g_pass.stream) ProcessSpatialPass();
    g_pass.stream = nullptr;
    return g_endUpdatingOriginal(self);
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

    if (SUCCEEDED(device->Activate(__uuidof(ISpatialAudioClient), CLSCTX_ALL, nullptr,
                                   reinterpret_cast<void**>(&client))) &&
        SUCCEEDED(client->GetSupportedAudioObjectFormatEnumerator(&formats)) &&
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
            out.activateSpatial = amk::win::VtableEntry(client, kActivateSpatial);
            out.beginUpdating = amk::win::VtableEntry(stream, kBeginUpdating);
            out.endUpdating = amk::win::VtableEntry(stream, kEndUpdating);
            out.objectGetBuffer = amk::win::VtableEntry(object, kObjectGetBuffer);
            out.objectSetVolume = amk::win::VtableEntry(object, kObjectSetVolume);
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
        out.initialize.push_back(amk::win::VtableEntry(client, kInitialize));
        out.getService.push_back(amk::win::VtableEntry(client, kGetService));
        out.getBuffer = amk::win::VtableEntry(render, kGetBuffer);
        out.releaseBuffer = amk::win::VtableEntry(render, kReleaseBuffer);

        IAudioClient3* client3 = nullptr;
        if (SUCCEEDED(client->QueryInterface(iidClient3, reinterpret_cast<void**>(&client3)))) {
            out.initialize.push_back(amk::win::VtableEntry(client3, kInitialize));
            out.getService.push_back(amk::win::VtableEntry(client3, kGetService));
            out.initializeShared = amk::win::VtableEntry(client3, kInitializeSharedStream);
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
    // Each entry lands in its own vtable, so each one is hooked and journaled on its own.
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

    if (!entries.activateSpatial || !entries.beginUpdating || !entries.endUpdating || !entries.objectGetBuffer ||
        !entries.objectSetVolume) {
        Note("objets spatiaux indisponibles sur le périphérique par défaut : Dolby Atmos non nivelé");
        return installed;
    }
    installed += HookEntry(entries.activateSpatial, AddressOf(ActivateSpatial_Hook),
                           reinterpret_cast<void**>(&g_activateSpatialOriginal),
                           "ISpatialAudioClient::ActivateSpatialAudioStream");
    installed += HookEntry(entries.beginUpdating, AddressOf(BeginUpdating_Hook),
                           reinterpret_cast<void**>(&g_beginUpdatingOriginal),
                           "ISpatialAudioObjectRenderStream::BeginUpdatingAudioObjects");
    installed += HookEntry(entries.endUpdating, AddressOf(EndUpdating_Hook),
                           reinterpret_cast<void**>(&g_endUpdatingOriginal),
                           "ISpatialAudioObjectRenderStream::EndUpdatingAudioObjects");
    installed += HookEntry(entries.objectGetBuffer, AddressOf(ObjectGetBuffer_Hook),
                           reinterpret_cast<void**>(&g_objectGetBufferOriginal), "ISpatialAudioObject::GetBuffer");
    installed += HookEntry(entries.objectSetVolume, AddressOf(ObjectSetVolume_Hook),
                           reinterpret_cast<void**>(&g_objectSetVolumeOriginal), "ISpatialAudioObject::SetVolume");
    return installed;
}

// Starts the leveler: waits for the Apple Music modules, hooks the player volume and the audio, then
// runs the poll thread. Called once, from the kernel's thread for this process.
bool StartLeveler(amk::Journal& journal, const LevelerConfig& config) {
    {
        std::lock_guard<std::mutex> guard(g_journalLock);
        g_journal = &journal;
    }
    g_config = config;

    if (!WaitForModules({L"AVFoundationCF.dll", L"CoreFoundation.dll"})) {
        Note("AVFoundationCF.dll ou CoreFoundation.dll absent après 60 s : nivellement inactif");
        return false;
    }
    if (!ResolveApi()) return false;

    {
        std::lock_guard<std::mutex> guard(g_levelerLock);
        g_leveler = std::make_unique<AtmosLeveler>(config, 48000, 2);
    }

    // The player's volume is only read: the hook passes it on unchanged, so the slider does not move.
    size_t volumeImports = amk::win::HookImportEverywhere(
        "AVFoundationCF.dll", "AVCFPlayerSetVolume", AddressOf(SetVolume_Hook),
        reinterpret_cast<void**>(&g_setVolumeOriginal));
    if (volumeImports == 0) {
        Note("AVCFPlayerSetVolume n'est importée par aucun module : volume non suivi, nivellement inactif");
        return false;
    }
    Note("hook AVCFPlayerSetVolume posé dans " + std::to_string(volumeImports) + " import(s)");

    // The kernel's thread may not be in a COM apartment yet. The throwaway objects need one.
    HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    AudioEntries entries;
    bool readable = (SUCCEEDED(com) || com == RPC_E_CHANGED_MODE) && ReadAudioEntries(entries);
    if (SUCCEEDED(com)) CoUninitialize();
    if (!readable) {
        Note("vtables audio illisibles sur le périphérique par défaut : aucun gain posé");
        return false;
    }
    int installed = InstallAudioHooks(entries);
    if (installed == 0) return false;

    std::thread(PollLoop).detach();
    Note("nivellement démarré : " + std::to_string(installed) + " hook(s) audio");
    return true;
}

// Reads the settings the leveler takes. Missing keys keep the reference mod's defaults.
LevelerConfig ConfigFrom(const amk::Settings& settings) {
    LevelerConfig config;
    config.autoLevel = settings.GetBool("extensions.atmos-leveler.auto_level", config.autoLevel);
    config.fallbackGainDb = settings.GetDouble("extensions.atmos-leveler.fallback_db", config.fallbackGainDb);
    config.maxGainDb = settings.GetDouble("extensions.atmos-leveler.max_db", config.maxGainDb);
    return config;
}

}  // namespace

amk::ExtensionInfo MakeAtmosLevelerExtension(amk::Journal& journal, const amk::Settings& settings) {
    LevelerConfig config = ConfigFrom(settings);

    amk::ExtensionInfo info;
    info.id = "atmos-leveler";
    info.name = "Nivellement Atmos";
    info.enabledByDefault = false;  // opt-in until it is tested on a real Dolby Atmos track
    info.start = [&journal, config] { return StartLeveler(journal, config); };
    // No stop: the hooks stay in place until the process exits, so nothing is left half-patched.
    return info;
}

}  // namespace atmos::hooks
