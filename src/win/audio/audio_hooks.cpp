// The render hooks of the audio platform (audio_hooks.hpp). Apple Music renders Dolby Atmos through
// ISpatialAudioObjectRenderStreamForMetadata, hooked on the fly when it is activated; the plain
// ISpatialAudioObjectRenderStream is hooked too, from a throwaway stream read at start. The slot numbers are the
// vtable positions of the Windows SDK interfaces, checked against the SDK headers (and spatial_metadata.hpp for the
// metadata stream).

#include "audio_hooks.hpp"

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
#include <filesystem>
#include <initializer_list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../../core/audio/audio_engine.hpp"
#include "../../core/audio/gain_ramp.hpp"
#include "../../core/audio/object_pass.hpp"
#include "../../core/audio/sample_path.hpp"
#include "../../core/gain_board.hpp"
#include "../hook.hpp"
#include "../win_common.hpp"
#include "hooks_detail.hpp"
#include "spatial_metadata.hpp"

namespace ampify::win::audio_hooks {

namespace {

using namespace std::chrono_literals;
using detail::AddressOf;
using detail::Fixed;
using detail::FromAddress;
using detail::Note;

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
constexpr auto kModuleWait = 60s;

ampify::Journal* g_journal = nullptr;
std::mutex g_journalLock;

// Set before the hooks are installed; the policy and the hooks live until the process exits.
std::atomic<GainPolicy*> g_policy{nullptr};
std::atomic<float> g_policyGain{1.0f};  // the policy's gain the audio threads apply now
std::atomic<bool> g_objectVolumeSeen{false};
std::atomic<long long> g_lastSoundTicks{0};  // steady_clock ticks of the stereo stream's last audible block

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
std::atomic<IAudioClient*> g_meteredClient{nullptr};  // the client g_meteredRender came from
std::atomic<BYTE*> g_meteredBuffer{nullptr};
std::atomic<UINT32> g_meteredBufferFrames{0};
std::atomic<int> g_meteredChannels{0};
std::atomic<int> g_meteredRate{static_cast<int>(audio::kStartSampleRate)};
// Each render client gets the next number: AutoMix tells streams apart by it, since a new client often lives at the
// address of the one just released.
std::uint64_t g_streamCount = 0;  // under g_streamLock
std::atomic<std::uint64_t> g_meteredNumber{0};
audio::GainRamp g_streamRamp(audio::kStartSampleRate);
std::vector<float> g_streamCurve;  // audio thread only

// AutoMix's second player renders into streams of its own (hooks_detail.hpp). A slot is written under g_streamLock
// and read by that stream's audio thread.
struct TailStream {
    std::atomic<void*> render{nullptr};
    std::atomic<IAudioClient*> client{nullptr};
    std::atomic<BYTE*> buffer{nullptr};
    std::atomic<UINT32> frames{0};
    std::atomic<int> rate{0};
    std::atomic<std::uint64_t> number{0};
};
std::array<TailStream, detail::kTailStreamSlots> g_tailStreams;
std::atomic<int> g_tailCount{0};        // slots in use: none outside a mix, so the hooks look no further
std::atomic<bool> g_tailExpected{false};
std::atomic<bool> g_tailMissed{false};  // a stream came that could not be taken: the second player must stay silent

// What goes out of the second player's stream gets the gain and the equalizer of Apple's, with a state of its own.
audio::GainRamp g_tailRamp(audio::kStartSampleRate);
std::vector<float> g_tailCurve;  // the second player's audio thread only
float g_tailGain = 1.0f;         // the gain when the old song was last unheard: it keeps its own song's

// Frames written to a stream that Windows has not taken yet. Called from that stream's own write, where its
// client is in use.
UINT32 QueuedFrames(IAudioClient* client) {
    UINT32 queued = 0;
    if (!client || FAILED(client->GetCurrentPadding(&queued))) return 0;
    return queued;
}

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
std::unique_ptr<audio::ObjectPass> g_objectPass;

struct SpatialPass {
    const void* stream = nullptr;
    UINT32 frames = 0;
    std::vector<audio::ObjectBuffer> objects;
};
SpatialPass g_pass;

// Ampify.audio: the equalizer, live loudness and analysis on these same buffers. It only adds steps to the paths
// below; with a flat equalizer and no element listening, the samples are untouched by it.
audio::SamplePath g_samplePath(audio::ProcessAudio());

std::string FileNameOf(const std::string& path) {
    return std::filesystem::path(path).filename().string();
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
    if (!HookVtableEntry(entry, replacement, original, why)) {
        Note(std::string("hook ") + name + " refused: " + why);
        return false;
    }
    Note("hook " + std::string(name) + " placed in " + FileNameOf(ModuleOf(*original)));
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

void PollLoop() {
    while (true) {
        std::this_thread::sleep_for(kPollPeriod);
        try {
            detail::PublishStoredVolume();
            detail::PollRecording();
            if (GainPolicy* policy = g_policy.load()) g_policyGain = policy->Poll();
        } catch (...) {
            Note("error in the follow loop: the leveler carries on without this pass");
        }
    }
}

bool IsFloat32(const WAVEFORMATEX* format) {
    if (!format || format->wBitsPerSample != 32) return false;
    if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return true;
    if (format->wFormatTag != WAVE_FORMAT_EXTENSIBLE || format->cbSize < 22) return false;
    return reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format)->SubFormat == kSubtypeFloat;
}

// One line per distinct stream format, so a stream the hooks cannot follow (exclusive mode, 24-bit samples, more
// than two channels) shows in the log instead of failing silently. Called under g_streamLock.
void JournalStreamFormat(AUDCLNT_SHAREMODE mode, const WAVEFORMATEX* format, DWORD flags, bool ok, bool eligible) {
    if (!format) return;
    static std::string last;
    std::string text = std::string(mode == AUDCLNT_SHAREMODE_EXCLUSIVE ? "exclusive" : "shared") + ", " +
                       std::to_string(format->nChannels) + " ch, " + std::to_string(format->nSamplesPerSec) +
                       " Hz, " + std::to_string(format->wBitsPerSample) + " bit, tag " +
                       std::to_string(format->wFormatTag) + (ok ? "" : ", refused") +
                       ((flags & AUDCLNT_STREAMFLAGS_LOOPBACK) ? ", loopback" : "") +
                       (eligible ? ": followed" : ": not followed");
    if (text == last) return;
    last = text;
    Note("audio stream " + text);
}

// Every (re)initialisation overwrites or erases the client's entry, so a client address reused by a
// stream in another format never inherits a stale one.
void RememberClientFormat(IAudioClient* client, AUDCLNT_SHAREMODE mode, const WAVEFORMATEX* format, DWORD flags,
                          bool ok) {
    bool eligible = ok && !(flags & AUDCLNT_STREAMFLAGS_LOOPBACK) && IsFloat32(format) && format->nChannels == 2;
    std::lock_guard<std::mutex> guard(g_streamLock);
    JournalStreamFormat(mode, format, flags, ok, eligible);
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
    RememberClientFormat(self, mode, format, flags, SUCCEEDED(hr));
    return hr;
}

HRESULT STDMETHODCALLTYPE InitializeShared_Hook(IAudioClient* self, DWORD flags, UINT32 period,
                                                const WAVEFORMATEX* format, LPCGUID session) {
    HRESULT hr = g_initializeSharedOriginal(self, flags, period, format, session);
    RememberClientFormat(self, AUDCLNT_SHAREMODE_SHARED, format, flags, SUCCEEDED(hr));
    return hr;
}

// The newest render client of an eligible stream becomes the metered one. Any other render client
// request stops metering rather than risk reading with the wrong format.
HRESULT STDMETHODCALLTYPE GetService_Hook(IAudioClient* self, REFIID riid, void** service) {
    HRESULT hr = g_getServiceOriginal(self, riid, service);
    if (FAILED(hr) || !service || riid != kIidAudioRenderClient) return hr;

    std::lock_guard<std::mutex> guard(g_streamLock);
    auto it = g_clientFormats.find(self);
    if (g_tailExpected.load()) {
        // The second player makes a stream when it starts and at each seek; Apple's player does the same, and its
        // seeks call the expectation off first (automix.cpp).
        const bool usable = it != g_clientFormats.end() && it->second.sampleRate == g_meteredRate.load() &&
                            it->second.channels == 2 && g_meteredRender.load() != nullptr;
        const int slot = g_tailCount.load();
        if (!usable || slot >= static_cast<int>(g_tailStreams.size())) {
            g_tailMissed = true;
            return hr;
        }
        g_tailRamp.SetSampleRate(it->second.sampleRate);
        g_tailStreams[slot].rate = it->second.sampleRate;
        g_tailStreams[slot].buffer = nullptr;
        g_tailStreams[slot].client = self;
        g_tailStreams[slot].number = ++g_streamCount;
        g_tailStreams[slot].render = *service;
        g_tailCount = slot + 1;
        return hr;
    }
    g_meteredRender.store(nullptr);
    g_meteredClient.store(nullptr);
    if (it == g_clientFormats.end()) return hr;

    int rate = it->second.sampleRate;
    if (GainPolicy* policy = g_policy.load()) policy->StreamRate(rate);
    g_streamRamp.SetSampleRate(rate);
    g_meteredChannels = it->second.channels;
    g_meteredRate = rate;
    g_meteredNumber = ++g_streamCount;
    g_meteredClient.store(self);
    g_meteredRender.store(*service);
    return hr;
}

// The slot of a stream of AutoMix's second player, or null: outside a mix, one load and no more.
TailStream* TailStreamOf(void* render) {
    // The newest first: a stream may live at the address of an older one of the same mix.
    for (int i = g_tailCount.load(std::memory_order_relaxed) - 1; i >= 0; i--) {
        if (g_tailStreams[i].render.load(std::memory_order_relaxed) == render) return &g_tailStreams[i];
    }
    return nullptr;
}

HRESULT STDMETHODCALLTYPE GetBuffer_Hook(IAudioRenderClient* self, UINT32 frames, BYTE** data) {
    HRESULT hr = g_getBufferOriginal(self, frames, data);
    if (FAILED(hr) || !data) return hr;
    if (self == g_meteredRender.load(std::memory_order_relaxed)) {
        g_meteredBufferFrames.store(frames, std::memory_order_relaxed);
        g_meteredBuffer.store(*data, std::memory_order_relaxed);
    } else if (TailStream* tail = TailStreamOf(self)) {
        tail->frames.store(frames, std::memory_order_relaxed);
        tail->buffer.store(*data, std::memory_order_relaxed);
    }
    return hr;
}

// The gain the audio threads apply now: the policy's, times what the elements asked for with Ampify.audio.setGain.
// Both are applied to the samples after they are measured, never to the volume.
float AppliedGain() { return g_policyGain.load() * ElementGains().Linear(); }

// The stereo samples of one block: measured, then gained, equalized and limited in place.
void ProcessStream(IAudioRenderClient* self, float* samples, UINT32 count) {
    // AutoMix first: the leveler, the equalizer and the analysis then see this stream as it is heard.
    if (detail::AutoMixOpen()) {
        detail::AutoMixMain(samples, count, g_meteredRate.load(), g_meteredNumber.load(std::memory_order_relaxed),
                            QueuedFrames(g_meteredClient.load()));
    }
    if (std::any_of(samples, samples + count * 2, [](float s) { return std::fabs(s) > 1e-4f; })) {
        g_lastSoundTicks.store(std::chrono::steady_clock::now().time_since_epoch().count(), std::memory_order_relaxed);
    }
    const float volume = detail::LastPlayerVolume();
    if (GainPolicy* policy = g_policy.load()) policy->FeedStream(samples, count, volume);

    // Ampify.audio: the loudness before the gain; the equalizer after the gain and before the knee (inside
    // ApplyCurve, inactive while flat); the analysis on what leaves for Windows.
    const int rate = g_meteredRate.load();
    g_samplePath.BeforeStream(samples, count, 2, rate, volume);
    bool unity = g_streamRamp.Curve(count, AppliedGain(), g_streamCurve);
    audio::ApplyCurve(samples, count, 2, g_streamCurve, unity, g_samplePath.StreamStage(rate), self);
    g_samplePath.AfterStream(samples, count, 2, rate, volume);
    detail::RecordOutput(samples, count, rate);
}

// One block of the second player's stream: its sound goes to the deck, and what the deck gives back (the old song as
// it must be heard, or nothing) leaves in its place, with the gain and the equalizer Apple's stream gets.
DWORD ProcessTailStream(IAudioRenderClient* self, TailStream& tail, UINT32 frames, DWORD flags) {
    BYTE* data = tail.buffer.exchange(nullptr, std::memory_order_relaxed);
    if (frames > tail.frames.load(std::memory_order_relaxed)) data = nullptr;
    auto* samples = reinterpret_cast<float*>(data);
    const int rate = tail.rate.load(std::memory_order_relaxed);
    const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
    const bool heard = detail::AutoMixTail(samples, frames, rate, silent, tail.number.load(std::memory_order_relaxed),
                                           QueuedFrames(tail.client.load()));
    if (!heard) {
        g_tailGain = AppliedGain();
        g_tailRamp.Curve(frames, g_tailGain, g_tailCurve);
        return flags | AUDCLNT_BUFFERFLAGS_SILENT;
    }
    const bool unity = g_tailRamp.Curve(frames, g_tailGain, g_tailCurve);
    audio::ApplyCurve(samples, frames, 2, g_tailCurve, unity, g_samplePath.SecondStreamStage(rate), self);
    return flags & ~static_cast<DWORD>(AUDCLNT_BUFFERFLAGS_SILENT);
}

HRESULT STDMETHODCALLTYPE ReleaseBuffer_Hook(IAudioRenderClient* self, UINT32 frames, DWORD flags) {
    if (frames && self == g_meteredRender.load(std::memory_order_relaxed)) {
        BYTE* data = g_meteredBuffer.exchange(nullptr, std::memory_order_relaxed);
        UINT32 count = std::min(frames, g_meteredBufferFrames.load(std::memory_order_relaxed));
        bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
        if (silent && data && count == frames && g_meteredChannels.load() == 2 && detail::AutoMixOpen()) {
            // Apple's player says nothing: during a mix its silence is counted like any other block, so that the
            // deck keeps knowing where this stream is.
            std::fill_n(reinterpret_cast<float*>(data), static_cast<size_t>(count) * 2, 0.0f);
            flags &= ~static_cast<DWORD>(AUDCLNT_BUFFERFLAGS_SILENT);
            silent = false;
        }
        if (data && !silent && count > 0 && g_meteredChannels.load() == 2) {
            ProcessStream(self, reinterpret_cast<float*>(data), count);
        }
    } else if (TailStream* tail = TailStreamOf(self)) {
        if (frames) flags = ProcessTailStream(self, *tail, frames, flags);
    }
    return g_releaseBufferOriginal(self, frames, flags);
}

// The original behind the slot `self` was called through. Every spatial stream and object method is hooked
// through g_spatialOriginals, so it is always there.
template <typename Fn>
Fn SpatialOriginal(void* self, size_t slot) {
    return FromAddress<Fn>(g_spatialOriginals.Find(VtableEntry(self, slot)));
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
        g_pass.objects.size() < audio::ObjectPass::kMaxObjects && *bytes >= g_pass.frames * sizeof(float)) {
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
    const audio::PassLevels& levels =
        g_objectPass->Process(g_pass.objects, g_pass.frames, AppliedGain(), g_samplePath.ObjectStage(rate));
    g_samplePath.AfterObjects(g_pass.objects, g_pass.frames, rate, levels);

    // Before the slider volume: measured as they are, nothing to divide out.
    if (GainPolicy* policy = g_policy.load()) policy->FeedObjects(levels.framePower, levels.peak);
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
        g_spatialOriginals.Hook(VtableEntry(*object, kObjectGetBuffer), AddressOf(ObjectGetBuffer_Hook), name);
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
    g_spatialOriginals.Hook(VtableEntry(stream, kBeginUpdating), AddressOf(BeginUpdating_Hook),
                            "ISpatialAudioObjectRenderStreamForMetadata::BeginUpdatingAudioObjects");
    g_spatialOriginals.Hook(VtableEntry(stream, kEndUpdating), AddressOf(EndUpdating_Hook),
                            "ISpatialAudioObjectRenderStreamForMetadata::EndUpdatingAudioObjects");
    g_spatialOriginals.Hook(VtableEntry(stream, kActivateForCommands), AddressOf(ActivateForCommands_Hook),
                            "ISpatialAudioObjectRenderStreamForMetadata::ActivateSpatialAudioObjectForMetadataCommands");
    g_spatialOriginals.Hook(VtableEntry(stream, kActivateForItems), AddressOf(ActivateForItems_Hook),
                            "ISpatialAudioObjectRenderStreamForMetadata::ActivateSpatialAudioObjectForMetadataItems");
}

// What ActivateSpatialAudioStream returned: checks the object format, sets the objects' rate, and hooks a
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
        g_objectPass = std::make_unique<audio::ObjectPass>(rate);
    }
    if (GainPolicy* policy = g_policy.load()) policy->ObjectRate(rate);
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

// The plain stream's entries, from a throwaway stream with one static object on the client's device.
void ReadPlainSpatialEntries(ISpatialAudioClient* client, HANDLE event, AudioEntries& out) {
    IAudioFormatEnumerator* formats = nullptr;
    ISpatialAudioObjectRenderStream* stream = nullptr;
    ISpatialAudioObject* object = nullptr;
    WAVEFORMATEX* format = nullptr;
    if (SUCCEEDED(client->GetSupportedAudioObjectFormatEnumerator(&formats)) &&
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
            out.beginUpdating = VtableEntry(stream, kBeginUpdating);
            out.endUpdating = VtableEntry(stream, kEndUpdating);
            out.objectGetBuffer = VtableEntry(object, kObjectGetBuffer);
            out.objectSetVolume = VtableEntry(object, kObjectSetVolume);
        }
    }
    if (object) object->Release();
    if (stream) stream->Release();
    if (formats) formats->Release();
    if (format) CoTaskMemFree(format);
}

// The spatial classes of the default device.
void ReadSpatialEntries(IMMDevice* device, AudioEntries& out) {
    ISpatialAudioClient* client = nullptr;
    if (FAILED(device->Activate(__uuidof(ISpatialAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client)))) {
        return;
    }
    // The activation is hooked whenever there is a client: Apple Music's metadata stream is found through it,
    // even where the throwaway plain stream cannot be made.
    out.activateSpatial = VtableEntry(client, kActivateSpatial);
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ReadPlainSpatialEntries(client, event, out);
    client->Release();
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
        out.initialize.push_back(VtableEntry(client, kInitialize));
        out.getService.push_back(VtableEntry(client, kGetService));
        out.getBuffer = VtableEntry(render, kGetBuffer);
        out.releaseBuffer = VtableEntry(render, kReleaseBuffer);

        IAudioClient3* client3 = nullptr;
        if (SUCCEEDED(client->QueryInterface(iidClient3, reinterpret_cast<void**>(&client3)))) {
            out.initialize.push_back(VtableEntry(client3, kInitialize));
            out.getService.push_back(VtableEntry(client3, kGetService));
            out.initializeShared = VtableEntry(client3, kInitializeSharedStream);
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

// The stereo hooks. Each entry lands in its own vtable, so each one is hooked and logged on its own.
int InstallStreamHooks(const AudioEntries& entries) {
    int installed = 0;
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
    return installed;
}

// The spatial hooks. The activation hook is what finds Apple Music's metadata stream; the plain stream's classes
// come from the throwaway stream.
int InstallSpatialHooks(const AudioEntries& entries) {
    if (!entries.activateSpatial) {
        Note("spatial objects not available on the default device: Dolby Atmos not leveled");
        return 0;
    }
    int installed = HookEntry(entries.activateSpatial, AddressOf(ActivateSpatial_Hook),
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

// Reads the entries from throwaway objects, then hooks them. Returns how many hooks went in.
int InstallAudioHooks() {
    // The calling thread may not be in a COM apartment yet. The throwaway objects need one.
    HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    AudioEntries entries;
    bool readable = (SUCCEEDED(com) || com == RPC_E_CHANGED_MODE) && ReadAudioEntries(entries);
    if (SUCCEEDED(com)) CoUninitialize();
    if (!readable) {
        Note("audio vtables unreadable on the default device: no gain applied");
        return 0;
    }
    return InstallStreamHooks(entries) + InstallSpatialHooks(entries);
}

std::mutex g_startLock;  // Start runs its body once at a time; the hooks go in once
bool g_hooksPlaced = false;

// Under g_startLock, when the hooks are not in place yet. `why`, when given, is logged first.
StartResult PlaceHooks(const char* why) {
    if (why) Note(why);
    if (!WaitForModules({L"AVFoundationCF.dll", L"CoreFoundation.dll"})) {
        Note("AVFoundationCF.dll or CoreFoundation.dll missing after 60 s: leveler inactive");
        return StartResult::Failed;
    }
    if (!detail::ResolvePlayerApi()) return StartResult::Failed;
    // The policy is ready before the hooks can feed it.
    if (GainPolicy* policy = g_policy.load(); policy && !policy->Prepare()) g_policy = nullptr;
    if (!detail::HookPlayerVolume()) return StartResult::Failed;
    detail::StartAutoMixWatch();

    int installed = InstallAudioHooks();
    if (installed == 0) return StartResult::Failed;

    std::thread(PollLoop).detach();
    g_hooksPlaced = true;
    Note("leveler started: " + std::to_string(installed) + " audio hook(s)");
    return StartResult::Placed;
}

StartResult StartWith(Journal& journal, const char* why) {
    std::lock_guard<std::mutex> start(g_startLock);
    detail::SetJournal(journal);
    if (g_hooksPlaced) return StartResult::AlreadyPlaced;
    return PlaceHooks(why);
}

}  // namespace

namespace detail {

void SetJournal(Journal& journal) {
    std::lock_guard<std::mutex> guard(g_journalLock);
    g_journal = &journal;
}

void Note(const std::string& message) {
    std::lock_guard<std::mutex> guard(g_journalLock);
    if (g_journal) g_journal->Write(kJournalPrefix + message);
}

std::string Fixed(double value, int decimals) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.*f", decimals, value);
    return text;
}

void ExpectTailStreams(bool expected) {
    std::lock_guard<std::mutex> guard(g_streamLock);
    g_tailExpected = expected;
}

void ForgetTailStreams() {
    std::lock_guard<std::mutex> guard(g_streamLock);
    g_tailExpected = false;
    g_tailMissed = false;
    g_tailCount = 0;  // a render client's address may serve again: none is remembered past its mix
    for (TailStream& stream : g_tailStreams) {
        stream.render = nullptr;
        stream.client = nullptr;
    }
}

bool TailStreamsClaimed() { return g_tailCount.load() > 0 && !g_tailMissed.load(); }

int MainStreamRate() {
    return g_meteredRender.load() != nullptr && g_meteredChannels.load() == 2 ? g_meteredRate.load() : 0;
}

}  // namespace detail

void SetPolicy(GainPolicy* policy) { g_policy = policy; }

StartResult Start(Journal& journal) { return StartWith(journal, nullptr); }

bool StartSoundHooks(Journal& journal) {
    return StartWith(journal, "the sound is wanted with the leveler off: the hooks go in with no gain") !=
           StartResult::Failed;
}

bool Placed() {
    std::lock_guard<std::mutex> start(g_startLock);
    return g_hooksPlaced;
}

bool SoundIsPlaying() {
    long long last = g_lastSoundTicks.load(std::memory_order_relaxed);
    auto recent = std::chrono::duration_cast<std::chrono::steady_clock::duration>(500ms).count();
    return last != 0 && std::chrono::steady_clock::now().time_since_epoch().count() - last < recent;
}

}  // namespace ampify::win::audio_hooks
