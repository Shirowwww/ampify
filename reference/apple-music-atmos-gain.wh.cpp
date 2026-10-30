// ==WindhawkMod==
// @id              apple-music-atmos-gain
// @name            Apple Music Atmos gain
// @description     Levels Dolby Atmos tracks in Apple Music to the loudness of Sound Check-levelled stereo tracks
// @version         2.3
// @author          Shirow
// @include         AMPLibraryAgent.exe
// @compilerOptions -lole32
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Apple Music Atmos gain

Sound Check levels stereo tracks only: Apple Music sends no loudness value for
Dolby Atmos items, whose mixes sit around -18 LUFS, well below levelled stereo.

This mod measures the loudness (ITU-R BS.1770, LUFS) of the decoded audio Apple
Music hands to Windows, learns the level of your stereo tracks after Sound Check,
and raises each Dolby Atmos track (E-AC-3 JOC, `qec3`/`ec-3`) to that level:

- a track heard before gets its stored gain from the first second;
- a new track starts with the fallback gain, then converges smoothly once enough
  audio has been measured, and its value is stored for next time.

The gain is applied to the decoded samples just before they reach Windows, with a
smooth ramp and a soft knee under 0 dBFS. Apple Music's volume and its slider are
never touched, and the gain does not depend on where the slider sits.

Measurements stay in memory; only one loudness number per track is stored, in
`%LOCALAPPDATA%\AppleMusicAtmosLoudness.txt` (redirected into the Apple Music
package folder).
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- autoLevel: true
  $name: Automatic levelling
  $description: Measure each Atmos track and match it to your Sound Check-levelled stereo tracks
- atmosGainDb: 5
  $name: Fallback Atmos gain (dB)
  $description: Used before a track is measured, or always when automatic levelling is off (0 to 15)
- diagnosticLog: false
  $name: Diagnostic log
  $description: Writes decisions to %LOCALAPPDATA%\AppleMusicAtmosGain.log (redirected into the Apple Music package folder)
*/
// ==/WindhawkModSettings==

#include <windows.h>

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <spatialaudioclient.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <numbers>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Apple CoreFoundation / AVFoundationCF imports

using CFTypeRef = const void*;
using SetVolume_t = void(__cdecl*)(void* player, float volume);
using CopyCurrentItem_t = void*(__cdecl*)(void* player);
using CopyObject_t = CFTypeRef(__cdecl*)(CFTypeRef obj);
using CFRelease_t = void(__cdecl*)(CFTypeRef obj);
// CFIndex is 64-bit in Apple's Windows CoreFoundation; passing 64-bit is safe either way.
using CFIndex = long long;
struct CFRange {
    CFIndex location;
    CFIndex length;
};
using CFArrayGetCount_t = CFIndex(__cdecl*)(CFTypeRef array);
using CFArrayGetValueAtIndex_t = CFTypeRef(__cdecl*)(CFTypeRef array, CFIndex index);
using CFStringGetLength_t = CFIndex(__cdecl*)(CFTypeRef str);
using CFStringGetBytes_t = CFIndex(__cdecl*)(CFTypeRef str, CFRange range, unsigned encoding, unsigned char lossByte,
                                             unsigned char external, unsigned char* buf, CFIndex size, CFIndex* used);

static SetVolume_t SetVolume_Original;
static CopyCurrentItem_t AVCFPlayerCopyCurrentItem;
static CopyObject_t AVCFPlayerItemCopyTracks;
static CopyObject_t AVCFPlayerItemCopyCurrentStableVariantID;
static CopyObject_t AVCFPlayerItemTrackCopyAssetTrack;
static CopyObject_t AVCFAssetTrackCopyFormatDescriptions;
static CopyObject_t CFRetain;
static CopyObject_t CFCopyDescription;
static CFRelease_t CFRelease;
static CFArrayGetCount_t CFArrayGetCount;
static CFArrayGetValueAtIndex_t CFArrayGetValueAtIndex;
static CFStringGetLength_t CFStringGetLength;
static CFStringGetBytes_t CFStringGetBytes;

// ---------------------------------------------------------------------------
// Constants and shared state

constexpr int kMaxGainDb = 15;
constexpr DWORD kPollMs = 300;
constexpr size_t kMaxPlayers = 8;
constexpr unsigned kCFStringEncodingUTF8 = 0x08000100;
constexpr size_t kHopsPerSecond = 10;               // 100 ms hops
constexpr size_t kSettleHops = 8 * kHopsPerSecond;  // skip the crossfade at each track start
constexpr size_t kMinHopsToLevel = 10 * kHopsPerSecond;
constexpr size_t kMinHopsToStore = 30 * kHopsPerSecond;
constexpr size_t kStereoHistory = 30;
constexpr size_t kMinStereoTracks = 2;
constexpr float kSlewDbPerPoll = 0.15f;  // 0.5 dB/s while converging on a new track
constexpr float kPeakCeiling = 0.95f;    // keep measured peaks under about -0.5 dBFS

struct PlayerState {
    void* player;
    float userVolume;
    bool atmos;
    bool formatKnown;
    void* lastItem;
    char lastVariant[96];
};

static std::mutex g_lock;  // g_players and g_atmosGainDb
static std::vector<PlayerState> g_players;
static void* g_activePlayer;  // player Apple Music set a volume on last
static float g_atmosGainDb;
static std::atomic<float> g_fallbackGainDb{5.0f};
static std::atomic<bool> g_autoLevel{true};
static std::atomic<bool> g_log{false};
static std::atomic<float> g_lastEffective{1.0f};  // slider volume of the active player, for the meter
static std::atomic<float> g_sampleGain{1.0f};     // linear gain the audio thread applies to the samples
static HANDLE g_stopEvent;
static HANDLE g_thread;

static float DbToLin(float db) { return std::pow(10.0f, db / 20.0f); }
static float LinToDb(float lin) { return 20.0f * std::log10(lin); }

static bool DataPath(const wchar_t* name, WCHAR* path) {
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", path, MAX_PATH);
    if (n == 0 || n > MAX_PATH - 40) return false;
    wcscat_s(path, MAX_PATH, L"\\");
    wcscat_s(path, MAX_PATH, name);
    return true;
}

static void Log(const char* fmt, ...) {
    if (!g_log) return;
    char line[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    Wh_Log(L"%S", line);

    static std::mutex fileLock;
    std::lock_guard<std::mutex> guard(fileLock);
    WCHAR path[MAX_PATH];
    if (!DataPath(L"AppleMusicAtmosGain.log", path)) return;
    FILE* f = _wfopen(path, L"a");
    if (!f) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(f, "%02d:%02d:%02d.%03d %s\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, line);
    fclose(f);
}

// ---------------------------------------------------------------------------
// Audio meter: runs on Apple Music's audio thread, inside IAudioRenderClient::ReleaseBuffer.
// It only reads samples (never writes them) and hands 100 ms energy hops to the poll thread
// through a lock-free single-producer ring.

struct Hop {
    float meanSquare;  // K-weighted, channel-summed, divided by the player volume squared
    float peak;        // sample peak, divided by the player volume
};

constexpr uint32_t kRingSize = 4096;
static Hop g_ring[kRingSize];
static std::atomic<uint32_t> g_ringHead{0};
static std::atomic<uint32_t> g_ringTail{0};

static void PushHop(const Hop& hop) {
    uint32_t head = g_ringHead.load(std::memory_order_relaxed);
    if (head - g_ringTail.load(std::memory_order_acquire) >= kRingSize) return;  // reader stalled: drop
    g_ring[head % kRingSize] = hop;
    g_ringHead.store(head + 1, std::memory_order_release);
}

static std::vector<Hop> DrainHops() {
    std::vector<Hop> hops;
    uint32_t tail = g_ringTail.load(std::memory_order_relaxed);
    uint32_t head = g_ringHead.load(std::memory_order_acquire);
    for (; tail != head; tail++) hops.push_back(g_ring[tail % kRingSize]);
    g_ringTail.store(tail, std::memory_order_release);
    return hops;
}

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    double Process(double x) {
        double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// BS.1770 K-weighting re-derived for any sample rate (as in libebur128).
static void KWeighting(int fs, Biquad& shelf, Biquad& highPass) {
    const double k1 = std::tan(std::numbers::pi * 1681.974450955533 / fs), q1 = 0.7071752369554196;
    const double vh = std::pow(10.0, 3.999843853973347 / 20), vb = std::pow(vh, 0.4996667741545416);
    double a0 = 1 + k1 / q1 + k1 * k1;
    shelf = {(vh + vb * k1 / q1 + k1 * k1) / a0, 2 * (k1 * k1 - vh) / a0, (vh - vb * k1 / q1 + k1 * k1) / a0,
             2 * (k1 * k1 - 1) / a0, (1 - k1 / q1 + k1 * k1) / a0};
    const double k2 = std::tan(std::numbers::pi * 38.13547087602444 / fs), q2 = 0.5003270373238773;
    a0 = 1 + k2 / q2 + k2 * k2;
    highPass = {1, -2, 1, 2 * (k2 * k2 - 1) / a0, (1 - k2 / q2 + k2 * k2) / a0};
}

struct StreamFormat {
    int channels = 0;
    int sampleRate = 0;
};

constexpr int kMaxMeteredChannels = 16;

struct Meter {
    uint32_t version = 0;
    StreamFormat format;
    Biquad shelf[kMaxMeteredChannels], highPass[kMaxMeteredChannels];
    float weight[kMaxMeteredChannels] = {};
    int hopFrames = 0, frames = 0;
    double energy = 0;
    float peak = 0;
};

static std::atomic<void*> g_musicRender{nullptr};  // the render client being metered
static std::atomic<BYTE*> g_musicBuffer{nullptr};
static std::atomic<UINT32> g_musicBufferFrames{0};  // size GetBuffer handed out
static std::atomic<uint32_t> g_musicVersion{0};
static StreamFormat g_musicFormat;  // written before g_musicVersion is bumped
static Meter g_meter;               // only touched by the audio thread of the metered stream

static void ResetMeter(Meter& m, uint32_t version) {
    m = Meter{};
    m.version = version;
    m.format = g_musicFormat;
    m.hopFrames = std::max(1, m.format.sampleRate / static_cast<int>(kHopsPerSecond));
    int n = std::min(m.format.channels, kMaxMeteredChannels);
    for (int c = 0; c < n; c++) {
        KWeighting(m.format.sampleRate, m.shelf[c], m.highPass[c]);
        // BS.1770 weights for the WAVEFORMATEXTENSIBLE order: no LFE, rear/surround channels +1.5 dB.
        bool lfe = n >= 6 && c == 3;
        bool surround = (n == 4 && c >= 2) || (n >= 6 && c >= 4);
        m.weight[c] = lfe ? 0.0f : (surround ? 1.41f : 1.0f);
    }
}

static void MeterFrames(const float* samples, UINT32 frames, bool silent) {
    Meter& m = g_meter;
    uint32_t version = g_musicVersion.load(std::memory_order_acquire);
    if (m.version != version) ResetMeter(m, version);
    int channels = m.format.channels, n = std::min(channels, kMaxMeteredChannels);
    for (UINT32 f = 0; f < frames; f++) {
        for (int c = 0; c < n; c++) {
            double x = silent ? 0.0 : samples[f * channels + c];
            m.peak = std::max(m.peak, static_cast<float>(std::fabs(x)));
            double y = m.highPass[c].Process(m.shelf[c].Process(x));
            m.energy += m.weight[c] * y * y;
        }
        if (++m.frames < m.hopFrames) continue;
        float eff = g_lastEffective.load(std::memory_order_relaxed);
        if (eff > 1e-4f)
            PushHop({static_cast<float>(m.energy / m.frames) / (eff * eff), m.peak / eff});
        m.frames = 0;
        m.energy = 0;
        m.peak = 0;
    }
}

// Applies g_sampleGain to the decoded samples, easing towards it over about 0.3 s so
// track changes do not click, with a soft knee above -0.5 dBFS so a boost never clips.
static void GainFrames(float* samples, UINT32 frames) {
    static float current = 1.0f;  // audio thread only
    constexpr float kKnee = 0.94f;
    int channels = g_meter.format.channels, rate = g_meter.format.sampleRate;
    if (channels <= 0 || rate <= 0) return;
    float target = g_sampleGain.load(std::memory_order_relaxed);
    if (target == 1.0f && current == 1.0f) return;
    float step = 1.0f / (0.3f * rate);
    for (UINT32 f = 0; f < frames; f++) {
        current += (target - current) * step * 3.0f;
        if (std::fabs(target - current) < 1e-4f) current = target;
        for (int c = 0; c < channels; c++) {
            float x = samples[f * channels + c] * current, a = std::fabs(x);
            if (a > kKnee) x = std::copysign(kKnee + (1.0f - kKnee) * std::tanh((a - kKnee) / (1.0f - kKnee)), x);
            samples[f * channels + c] = x;
        }
    }
}

// ---------------------------------------------------------------------------
// WASAPI hooks: learn the format of each stream, meter the most recent float stream.

using Initialize_t = HRESULT(STDMETHODCALLTYPE*)(IAudioClient*, AUDCLNT_SHAREMODE, DWORD, REFERENCE_TIME,
                                                 REFERENCE_TIME, const WAVEFORMATEX*, LPCGUID);
using InitializeShared_t = HRESULT(STDMETHODCALLTYPE*)(IAudioClient*, DWORD, UINT32, const WAVEFORMATEX*, LPCGUID);
using GetService_t = HRESULT(STDMETHODCALLTYPE*)(IAudioClient*, REFIID, void**);
using GetBuffer_t = HRESULT(STDMETHODCALLTYPE*)(IAudioRenderClient*, UINT32, BYTE**);
using ReleaseBuffer_t = HRESULT(STDMETHODCALLTYPE*)(IAudioRenderClient*, UINT32, DWORD);

static Initialize_t Initialize_Original;
static InitializeShared_t InitializeShared_Original;
static GetService_t GetService_Original;
static GetBuffer_t GetBuffer_Original;
static ReleaseBuffer_t ReleaseBuffer_Original;

static const GUID kIidAudioRenderClient = {0xF294ACFC, 0x3146, 0x4483, {0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2}};
static const GUID kSubtypeFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};

static std::mutex g_streamLock;  // g_clientFormats
static std::map<void*, StreamFormat> g_clientFormats;

static bool IsFloat32(const WAVEFORMATEX* f) {
    if (!f || f->wBitsPerSample != 32) return false;
    if (f->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return true;
    if (f->wFormatTag != WAVE_FORMAT_EXTENSIBLE || f->cbSize < 22) return false;
    return reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(f)->SubFormat == kSubtypeFloat;
}

// Every (re)initialisation overwrites or erases the client's entry, so a client address
// reused by a stream in another format can never inherit a stale one.
static void RememberClientFormat(IAudioClient* client, const WAVEFORMATEX* format, DWORD flags, bool ok) {
    bool eligible = ok && !(flags & AUDCLNT_STREAMFLAGS_LOOPBACK) && IsFloat32(format) && format->nChannels >= 2;
    std::lock_guard<std::mutex> guard(g_streamLock);
    if (!eligible) {
        g_clientFormats.erase(client);
        return;
    }
    if (g_clientFormats.size() > 32) g_clientFormats.clear();
    g_clientFormats[client] = {format->nChannels, static_cast<int>(format->nSamplesPerSec)};
    Log("stream %p: float32, %d ch, %lu Hz", client, format->nChannels, format->nSamplesPerSec);
}

static HRESULT STDMETHODCALLTYPE Initialize_Hook(IAudioClient* self, AUDCLNT_SHAREMODE mode, DWORD flags,
                                                 REFERENCE_TIME duration, REFERENCE_TIME period,
                                                 const WAVEFORMATEX* format, LPCGUID session) {
    HRESULT hr = Initialize_Original(self, mode, flags, duration, period, format, session);
    RememberClientFormat(self, format, flags, SUCCEEDED(hr));
    return hr;
}

static HRESULT STDMETHODCALLTYPE InitializeShared_Hook(IAudioClient* self, DWORD flags, UINT32 period,
                                                       const WAVEFORMATEX* format, LPCGUID session) {
    HRESULT hr = InitializeShared_Original(self, flags, period, format, session);
    RememberClientFormat(self, format, flags, SUCCEEDED(hr));
    return hr;
}

// The newest render client of an eligible stream becomes the metered one; any other
// render client request stops metering rather than risk reading with a wrong format.
static HRESULT STDMETHODCALLTYPE GetService_Hook(IAudioClient* self, REFIID riid, void** service) {
    HRESULT hr = GetService_Original(self, riid, service);
    if (FAILED(hr) || !service || riid != kIidAudioRenderClient) return hr;
    std::lock_guard<std::mutex> guard(g_streamLock);
    auto it = g_clientFormats.find(self);
    g_musicRender.store(nullptr);
    if (it == g_clientFormats.end()) return hr;
    g_musicFormat = it->second;
    g_musicVersion.fetch_add(1, std::memory_order_release);
    g_musicRender.store(*service);
    return hr;
}

static HRESULT STDMETHODCALLTYPE GetBuffer_Hook(IAudioRenderClient* self, UINT32 frames, BYTE** data) {
    HRESULT hr = GetBuffer_Original(self, frames, data);
    if (SUCCEEDED(hr) && data && self == g_musicRender.load(std::memory_order_relaxed)) {
        g_musicBufferFrames.store(frames, std::memory_order_relaxed);
        g_musicBuffer.store(*data, std::memory_order_relaxed);
    }
    return hr;
}

// Measures the decoded samples, then applies the Atmos gain, before handing them to Windows.
static HRESULT STDMETHODCALLTYPE ReleaseBuffer_Hook(IAudioRenderClient* self, UINT32 frames, DWORD flags) {
    if (frames && self == g_musicRender.load(std::memory_order_relaxed)) {
        BYTE* data = g_musicBuffer.exchange(nullptr, std::memory_order_relaxed);
        UINT32 n = std::min(frames, g_musicBufferFrames.load(std::memory_order_relaxed));
        bool silent = flags & AUDCLNT_BUFFERFLAGS_SILENT;
        if (data) {
            MeterFrames(reinterpret_cast<const float*>(data), n, silent);
            if (!silent) GainFrames(reinterpret_cast<float*>(data), n);
        }
    }
    return ReleaseBuffer_Original(self, frames, flags);
}

// ---------------------------------------------------------------------------
// Spatial audio hooks: Dolby Atmos tracks reach Windows as spatial audio objects
// (ISpatialAudioObjectRenderStream, rendered by Dolby Atmos for Headphones), not through
// IAudioRenderClient. Each update pass, every object buffer is metered then gained.

using ActivateSpatial_t = HRESULT(STDMETHODCALLTYPE*)(ISpatialAudioClient*, const PROPVARIANT*, REFIID, void**);
using BeginUpdating_t = HRESULT(STDMETHODCALLTYPE*)(ISpatialAudioObjectRenderStream*, UINT32*, UINT32*);
using EndUpdating_t = HRESULT(STDMETHODCALLTYPE*)(ISpatialAudioObjectRenderStream*);
using ObjectGetBuffer_t = HRESULT(STDMETHODCALLTYPE*)(ISpatialAudioObject*, BYTE**, UINT32*);
using ObjectSetVolume_t = HRESULT(STDMETHODCALLTYPE*)(ISpatialAudioObject*, float);

static ActivateSpatial_t ActivateSpatial_Original;
static BeginUpdating_t BeginUpdating_Original;
static EndUpdating_t EndUpdating_Original;
static ObjectGetBuffer_t ObjectGetBuffer_Original;
static ObjectSetVolume_t ObjectSetVolume_Original;

constexpr int kMaxObjects = 256;

struct ObjectFilter {
    void* object = nullptr;
    Biquad shelf, highPass;
};

// Everything below is touched only by the audio thread of the spatial stream, between
// BeginUpdatingAudioObjects and EndUpdatingAudioObjects.
struct SpatialPass {
    void* stream = nullptr;
    UINT32 frames = 0;
    int count = 0;
    float* buffers[kMaxObjects];
    int filters[kMaxObjects];
};

struct SpatialMeter {
    int rate = 0, hopFrames = 0, frames = 0;
    double energy = 0;
    float peak = 0, gain = 1.0f;
};

static std::atomic<int> g_spatialRate{0};  // 0 until a float32 mono object stream is seen
static std::atomic<bool> g_objectVolumeUsed{false};
static ObjectFilter g_objectFilters[kMaxObjects];
static SpatialPass g_pass;
static SpatialMeter g_spatial;

// Filter state per object, found by open addressing on the object's address.
static int FilterFor(void* object, int rate) {
    size_t start = (reinterpret_cast<uintptr_t>(object) >> 4) % kMaxObjects;
    for (int i = 0; i < kMaxObjects; i++) {
        ObjectFilter& f = g_objectFilters[(start + i) % kMaxObjects];
        if (f.object == object) return static_cast<int>((start + i) % kMaxObjects);
        if (f.object) continue;
        f.object = object;
        KWeighting(rate, f.shelf, f.highPass);
        return static_cast<int>((start + i) % kMaxObjects);
    }
    for (auto& f : g_objectFilters) f = ObjectFilter{};  // table full: start over
    return FilterFor(object, rate);
}

static HRESULT STDMETHODCALLTYPE ActivateSpatial_Hook(ISpatialAudioClient* self, const PROPVARIANT* params,
                                                      REFIID riid, void** stream) {
    HRESULT hr = ActivateSpatial_Original(self, params, riid, stream);
    if (FAILED(hr) || !params || params->vt != VT_BLOB ||
        params->blob.cbSize < sizeof(SpatialAudioObjectRenderStreamActivationParams))
        return hr;
    auto* p = reinterpret_cast<const SpatialAudioObjectRenderStreamActivationParams*>(params->blob.pBlobData);
    const WAVEFORMATEX* f = p->ObjectFormat;
    bool usable = IsFloat32(f) && f->nChannels == 1;
    g_spatialRate = usable ? static_cast<int>(f->nSamplesPerSec) : 0;
    Log("spatial stream: iid %08lx, objects %s %lu Hz, static mask 0x%x, dynamic %u-%u", riid.Data1,
        usable ? "float32 mono" : "unsupported", f ? f->nSamplesPerSec : 0, p->StaticObjectTypeMask,
        p->MinDynamicObjectCount, p->MaxDynamicObjectCount);
    return hr;
}

static HRESULT STDMETHODCALLTYPE BeginUpdating_Hook(ISpatialAudioObjectRenderStream* self, UINT32* available,
                                                    UINT32* frames) {
    HRESULT hr = BeginUpdating_Original(self, available, frames);
    g_pass.stream = SUCCEEDED(hr) && frames && g_spatialRate ? self : nullptr;
    g_pass.frames = g_pass.stream ? *frames : 0;
    g_pass.count = 0;
    return hr;
}

static HRESULT STDMETHODCALLTYPE ObjectGetBuffer_Hook(ISpatialAudioObject* self, BYTE** buffer, UINT32* bytes) {
    HRESULT hr = ObjectGetBuffer_Original(self, buffer, bytes);
    if (SUCCEEDED(hr) && buffer && *buffer && bytes && g_pass.stream && g_pass.count < kMaxObjects &&
        *bytes >= g_pass.frames * sizeof(float)) {
        g_pass.buffers[g_pass.count] = reinterpret_cast<float*>(*buffer);
        g_pass.filters[g_pass.count] = FilterFor(self, g_spatialRate);
        g_pass.count++;
    }
    return hr;
}

// If Apple Music applies its volume per object, the buffers are pre-volume already.
static HRESULT STDMETHODCALLTYPE ObjectSetVolume_Hook(ISpatialAudioObject* self, float volume) {
    if (volume < 0.999f && !g_objectVolumeUsed.exchange(true)) Log("spatial objects carry the volume (%.3f)", volume);
    return ObjectSetVolume_Original(self, volume);
}

static void ProcessSpatialPass() {
    SpatialMeter& m = g_spatial;
    int rate = g_spatialRate;
    if (m.rate != rate) m = SpatialMeter{rate, std::max(1, rate / static_cast<int>(kHopsPerSecond))};
    UINT32 frames = g_pass.frames;
    float target = g_sampleGain.load(std::memory_order_relaxed);
    float start = m.gain, end = start + (target - start) * std::min(1.0f, frames / (0.3f * rate));
    for (int i = 0; i < g_pass.count; i++) {
        float* b = g_pass.buffers[i];
        ObjectFilter& filter = g_objectFilters[g_pass.filters[i]];
        for (UINT32 f = 0; f < frames; f++) {
            double y = filter.highPass.Process(filter.shelf.Process(b[f]));
            m.energy += y * y;  // channels add up in power, as in BS.1770
            m.peak = std::max(m.peak, std::fabs(b[f]));
            float x = b[f] * (start + (end - start) * f / frames), a = std::fabs(x);
            if (a > 0.94f) x = std::copysign(0.94f + 0.06f * std::tanh((a - 0.94f) / 0.06f), x);
            b[f] = x;
        }
    }
    m.gain = end;
    m.frames += frames;
    if (m.frames < m.hopFrames) return;
    float eff = g_objectVolumeUsed ? 1.0f : g_lastEffective.load(std::memory_order_relaxed);
    if (eff > 1e-4f) PushHop({static_cast<float>(m.energy / m.frames) / (eff * eff), m.peak / eff});
    m.frames = 0;
    m.energy = 0;
    m.peak = 0;
}

static HRESULT STDMETHODCALLTYPE EndUpdating_Hook(ISpatialAudioObjectRenderStream* self) {
    if (self == g_pass.stream) ProcessSpatialPass();
    g_pass.stream = nullptr;
    return EndUpdating_Original(self);
}

struct AudioVtables {
    void* initialize = nullptr;
    void* initializeShared = nullptr;
    void* getService = nullptr;
    void* getBuffer = nullptr;
    void* releaseBuffer = nullptr;
    void* activateSpatial = nullptr;
    void* beginUpdating = nullptr;
    void* endUpdating = nullptr;
    void* objectGetBuffer = nullptr;
    void* objectSetVolume = nullptr;
};

static void* Slot(void* com, int index) { return (*reinterpret_cast<void***>(com))[index]; }

// Same idea for the spatial classes: a throwaway stream with one static object.
static void ReadSpatialVtables(IMMDevice* device, AudioVtables& out) {
    ISpatialAudioClient* client = nullptr;
    IAudioFormatEnumerator* formats = nullptr;
    ISpatialAudioObjectRenderStream* stream = nullptr;
    ISpatialAudioObject* object = nullptr;
    WAVEFORMATEX* format = nullptr;
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (SUCCEEDED(device->Activate(__uuidof(ISpatialAudioClient), CLSCTX_ALL, nullptr, (void**)&client)) &&
        SUCCEEDED(client->GetSupportedAudioObjectFormatEnumerator(&formats)) &&
        SUCCEEDED(formats->GetFormat(0, &format))) {
        SpatialAudioObjectRenderStreamActivationParams p{};
        p.ObjectFormat = format;
        p.StaticObjectTypeMask = AudioObjectType_FrontLeft;
        p.Category = AudioCategory_Other;
        p.EventHandle = event;
        PROPVARIANT pv{};
        pv.vt = VT_BLOB;
        pv.blob.cbSize = sizeof(p);
        pv.blob.pBlobData = reinterpret_cast<BYTE*>(&p);
        if (SUCCEEDED(client->ActivateSpatialAudioStream(&pv, __uuidof(ISpatialAudioObjectRenderStream), (void**)&stream)) &&
            SUCCEEDED(stream->ActivateSpatialAudioObject(AudioObjectType_FrontLeft, &object))) {
            out.activateSpatial = Slot(client, 10);
            out.beginUpdating = Slot(stream, 8);
            out.endUpdating = Slot(stream, 9);
            out.objectGetBuffer = Slot(object, 3);
            out.objectSetVolume = Slot(object, 8);
        }
    }
    if (object) object->Release();
    if (stream) stream->Release();
    if (formats) formats->Release();
    if (client) client->Release();
    if (event) CloseHandle(event);
}

// Opens a throwaway shared stream on the default device to read the method addresses
// every IAudioClient / IAudioRenderClient in this process shares.
static void ReadAudioVtables(AudioVtables& out) {
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* client = nullptr;
    IAudioRenderClient* render = nullptr;
    WAVEFORMATEX* format = nullptr;
    const GUID clsid = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
    const GUID iidEnumerator = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
    const GUID iidClient = {0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
    const GUID iidClient3 = {0x7ED4EE07, 0x8E67, 0x4CD4, {0x8C, 0x1A, 0x2B, 0x7A, 0x59, 0x87, 0xAD, 0x42}};
    if (SUCCEEDED(CoCreateInstance(clsid, nullptr, CLSCTX_ALL, iidEnumerator, (void**)&enumerator)) &&
        SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device)) &&
        SUCCEEDED(device->Activate(iidClient, CLSCTX_ALL, nullptr, (void**)&client)) &&
        SUCCEEDED(client->GetMixFormat(&format)) &&
        SUCCEEDED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 1000000, 0, format, nullptr)) &&
        SUCCEEDED(client->GetService(kIidAudioRenderClient, (void**)&render))) {
        out.initialize = Slot(client, 3);
        out.getService = Slot(client, 14);
        out.getBuffer = Slot(render, 3);
        out.releaseBuffer = Slot(render, 4);
        IUnknown* client3 = nullptr;
        if (SUCCEEDED(client->QueryInterface(iidClient3, (void**)&client3))) {
            out.initializeShared = Slot(client3, 20);
            client3->Release();
        }
    }
    if (device) ReadSpatialVtables(device, out);
    if (render) render->Release();
    if (format) CoTaskMemFree(format);
    if (client) client->Release();
    if (device) device->Release();
    if (enumerator) enumerator->Release();
}

static void HookAudio() {
    AudioVtables v;
    // No helper thread (joining one from Wh_ModInit could wait on the loader lock): use this
    // thread's apartment if it has one, or a temporary one otherwise.
    HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(com) || com == RPC_E_CHANGED_MODE) ReadAudioVtables(v);
    if (SUCCEEDED(com)) CoUninitialize();
    if (!v.initialize || !v.getService || !v.getBuffer || !v.releaseBuffer) {
        Wh_Log(L"audio meter unavailable: could not read the WASAPI vtables");
        return;
    }
    Wh_SetFunctionHook(v.initialize, (void*)Initialize_Hook, (void**)&Initialize_Original);
    Wh_SetFunctionHook(v.getService, (void*)GetService_Hook, (void**)&GetService_Original);
    Wh_SetFunctionHook(v.getBuffer, (void*)GetBuffer_Hook, (void**)&GetBuffer_Original);
    Wh_SetFunctionHook(v.releaseBuffer, (void*)ReleaseBuffer_Hook, (void**)&ReleaseBuffer_Original);
    if (v.initializeShared)
        Wh_SetFunctionHook(v.initializeShared, (void*)InitializeShared_Hook, (void**)&InitializeShared_Original);
    if (!v.activateSpatial || !v.beginUpdating || !v.endUpdating || !v.objectGetBuffer || !v.objectSetVolume) {
        Wh_Log(L"spatial audio unavailable on the default device: Atmos tracks are not levelled");
        return;
    }
    Wh_SetFunctionHook(v.activateSpatial, (void*)ActivateSpatial_Hook, (void**)&ActivateSpatial_Original);
    Wh_SetFunctionHook(v.beginUpdating, (void*)BeginUpdating_Hook, (void**)&BeginUpdating_Original);
    Wh_SetFunctionHook(v.endUpdating, (void*)EndUpdating_Hook, (void**)&EndUpdating_Original);
    Wh_SetFunctionHook(v.objectGetBuffer, (void*)ObjectGetBuffer_Hook, (void**)&ObjectGetBuffer_Original);
    Wh_SetFunctionHook(v.objectSetVolume, (void*)ObjectSetVolume_Hook, (void**)&ObjectSetVolume_Original);
}

// ---------------------------------------------------------------------------
// Loudness store: one integrated loudness per Atmos variant, plus recent stereo tracks.

struct StoredLoudness {
    float lufs;
    size_t hops;  // how much audio the value was measured on: longer measurements win
};

static std::map<std::string, StoredLoudness> g_atmosLoudness;  // stable variant id -> LUFS before volume
static std::vector<float> g_stereoLoudness;                   // newest last

static void RememberStereo(float lufs) {
    g_stereoLoudness.push_back(lufs);
    if (g_stereoLoudness.size() > kStereoHistory) g_stereoLoudness.erase(g_stereoLoudness.begin());
}

static FILE* OpenStore(const wchar_t* mode) {
    WCHAR path[MAX_PATH];
    return DataPath(L"AppleMusicAtmosLoudness.txt", path) ? _wfopen(path, mode) : nullptr;
}

// Rewrites the store with one line per Atmos variant and the recent stereo values only.
static void CompactStore() {
    FILE* f = OpenStore(L"w");
    if (!f) return;
    for (const auto& [key, v] : g_atmosLoudness) fprintf(f, "A %s %.2f %zu\n", key.c_str(), v.lufs, v.hops);
    for (float lufs : g_stereoLoudness) fprintf(f, "S %.2f\n", lufs);
    fclose(f);
}

static void LoadStore() {
    FILE* f = OpenStore(L"r");
    if (!f) return;
    char kind[4], key[96], line[256];
    float lufs;
    size_t hops, lines = 0;
    while (fgets(line, sizeof(line), f)) {
        lines++;
        int n = sscanf(line, "%3s %95s %f %zu", kind, key, &lufs, &hops);
        if (n >= 3 && strcmp(kind, "A") == 0) {
            StoredLoudness value{lufs, n == 4 ? hops : 0};
            auto it = g_atmosLoudness.find(key);
            if (it == g_atmosLoudness.end() || it->second.hops <= value.hops) g_atmosLoudness[key] = value;
        } else if (sscanf(line, "%3s %f", kind, &lufs) == 2 && strcmp(kind, "S") == 0) {
            RememberStereo(lufs);
        }
    }
    fclose(f);
    if (lines > 2 * (g_atmosLoudness.size() + kStereoHistory)) CompactStore();
}

static void AppendStore(const char* line) {
    FILE* f = OpenStore(L"a");
    if (!f) return;
    fprintf(f, "%s\n", line);
    fclose(f);
}

// Median of the stereo tracks heard after Sound Check: the level Atmos tracks are matched to.
static bool StereoTarget(float& target) {
    if (g_stereoLoudness.size() < kMinStereoTracks) return false;
    std::vector<float> sorted = g_stereoLoudness;
    std::sort(sorted.begin(), sorted.end());
    target = sorted[sorted.size() / 2];
    return true;
}

// BS.1770 integrated loudness: 400 ms blocks (4 hops, 75 % overlap), -70 LUFS and -10 LU gates.
static bool IntegratedLufs(const std::vector<Hop>& hops, float& lufs) {
    std::vector<double> blocks;
    for (size_t i = 3; i < hops.size(); i++)
        blocks.push_back((hops[i].meanSquare + hops[i - 1].meanSquare + hops[i - 2].meanSquare + hops[i - 3].meanSquare) / 4);
    auto toLufs = [](double ms) { return -0.691 + 10 * std::log10(std::max(ms, 1e-12)); };
    double sum = 0;
    size_t count = 0;
    for (double b : blocks)
        if (toLufs(b) > -70) sum += b, count++;
    if (count == 0) return false;
    double relativeGate = toLufs(sum / count) - 10;
    sum = 0;
    count = 0;
    for (double b : blocks)
        if (toLufs(b) > -70 && toLufs(b) > relativeGate) sum += b, count++;
    lufs = static_cast<float>(toLufs(sum / count));
    return true;
}

// ---------------------------------------------------------------------------
// Current track (poll thread only)

struct TrackState {
    void* item = nullptr;  // retained: its address cannot be reused by the next item meanwhile
    bool atmos = false;
    std::vector<std::string> variants;  // every stable variant id seen for this item
    std::vector<Hop> hops;              // audible hops only, after the settle period
    size_t skipped = 0;
    float peak = 0;
    bool cached = false;
    float cachedLufs = 0;
    float lufs = 0;  // integrated loudness of `hops`, refreshed every second
    size_t lufsHops = 0;
    ULONGLONG startTick = 0;
    bool reportedSilence = false;
};

static TrackState g_track;
static std::atomic<bool> g_trackAtmos{false};  // copy of g_track.atmos for other threads

static void AddVariant(const std::string& variant) {
    if (variant.empty()) return;
    if (std::find(g_track.variants.begin(), g_track.variants.end(), variant) != g_track.variants.end()) return;
    g_track.variants.push_back(variant);
    auto it = g_atmosLoudness.find(variant);
    if (g_track.atmos && !g_track.cached && it != g_atmosLoudness.end()) {
        g_track.cached = true;
        g_track.cachedLufs = it->second.lufs;
    }
}

static void FinishTrack() {
    float lufs;
    size_t hops = g_track.hops.size();
    if (!g_track.item || hops < kMinHopsToStore || !IntegratedLufs(g_track.hops, lufs)) return;
    char line[160];
    if (g_track.atmos) {
        for (const auto& v : g_track.variants) {
            auto it = g_atmosLoudness.find(v);
            if (it != g_atmosLoudness.end() && it->second.hops >= hops) continue;  // keep the longer measurement
            g_atmosLoudness[v] = {lufs, hops};
            snprintf(line, sizeof(line), "A %s %.2f %zu", v.c_str(), lufs, hops);
            AppendStore(line);
        }
    } else {
        RememberStereo(lufs);
        snprintf(line, sizeof(line), "S %.2f", lufs);
        AppendStore(line);
    }
    Log("track done: %s %.2f LUFS over %zu s", g_track.atmos ? "atmos" : "stereo", lufs, hops / kHopsPerSecond);
}

// Takes ownership of one reference to `item`.
static void StartTrack(void* item, bool atmos) {
    FinishTrack();
    if (g_track.item) CFRelease(g_track.item);
    g_track = TrackState{};
    g_track.item = item;
    g_track.atmos = atmos;
    g_track.startTick = GetTickCount64();
    g_trackAtmos = atmos;
    DrainHops();  // whatever is buffered belongs to the previous track
}

static void CollectHops() {
    constexpr float kSilentMeanSquare = 1.17e-7f;  // -70 LUFS: pauses and silence are not measured
    for (const Hop& hop : DrainHops()) {
        if (!g_track.item || hop.meanSquare < kSilentMeanSquare) continue;
        if (g_track.skipped < kSettleHops) {
            g_track.skipped++;
            continue;
        }
        g_track.hops.push_back(hop);
        g_track.peak = std::max(g_track.peak, hop.peak);
    }
    if (g_track.hops.size() >= g_track.lufsHops + kHopsPerSecond && IntegratedLufs(g_track.hops, g_track.lufs))
        g_track.lufsHops = g_track.hops.size();
    bool quiet = g_track.atmos && g_track.hops.empty() && GetTickCount64() - g_track.startTick > 60000;
    if (quiet && !g_track.reportedSilence) {
        g_track.reportedSilence = true;
        Log("no audio measured for this atmos track after 60 s: keeping the fallback gain");
    }
}

// ---------------------------------------------------------------------------
// Volume

// Caller holds g_lock.
static PlayerState* Find(void* player) {
    for (auto& s : g_players)
        if (s.player == player) return &s;
    return nullptr;
}

// Hands the audio thread the gain for what is playing now (1 for stereo).
static void PublishGain() {
    std::lock_guard<std::mutex> guard(g_lock);
    g_sampleGain = g_trackAtmos ? DbToLin(g_atmosGainDb) : 1.0f;
}

// Apple Music creates a player per playback context; the oldest one is dropped, and the
// caller releases it once g_lock is free (its destructor may call back into our hooks).
// Caller holds g_lock.
static PlayerState* Track(void* player, float volume, void** evicted) {
    *evicted = nullptr;
    if (PlayerState* s = Find(player)) return s;
    if (g_players.size() >= kMaxPlayers) {
        *evicted = g_players.front().player;
        g_players.erase(g_players.begin());
    }
    CFRetain(player);
    g_players.push_back({player, volume, false, false, nullptr, ""});
    return &g_players.back();
}

static std::atomic<bool> g_stopping{false};

// Only observes: the slider volume goes to Apple unchanged, the meter needs it to measure
// tracks independently of where the slider is.
static void __cdecl SetVolume_Hook(void* player, float volume) {
    SetVolume_Original(player, volume);
    if (g_stopping) return;
    void* evicted;
    {
        std::lock_guard<std::mutex> guard(g_lock);
        Track(player, volume, &evicted)->userVolume = volume;
        g_activePlayer = player;
        g_lastEffective = volume;
    }
    if (evicted) CFRelease(evicted);
    Log("SetVolume player=%p volume=%.3f", player, volume);
}

// The gain the current Atmos track should get now, in dB. Poll thread, outside g_lock.
static float DesiredGainDb(float current, float userVolume) {
    float target;
    if (!g_autoLevel || !StereoTarget(target)) return g_fallbackGainDb;
    bool measured = g_track.lufsHops >= kMinHopsToLevel;
    if (!g_track.cached && !measured) return current;
    float lufs = g_track.cached ? g_track.cachedLufs : g_track.lufs;
    float desired = std::clamp(target - lufs, 0.0f, static_cast<float>(kMaxGainDb));
    if (g_track.peak > 0 && userVolume > 0)
        desired = std::min(desired, LinToDb(kPeakCeiling / (g_track.peak * userVolume)));
    desired = std::max(desired, 0.0f);
    // A stored track jumps straight to its gain before the music starts; later changes ease in.
    if (g_track.cached && g_track.hops.empty()) return desired;
    return std::clamp(desired, current - kSlewDbPerPoll, current + kSlewDbPerPoll);
}

static void UpdateGain() {
    float userVolume, before;
    {
        std::lock_guard<std::mutex> guard(g_lock);
        PlayerState* s = Find(g_activePlayer);
        if (!s || !g_track.atmos) return;
        userVolume = s->userVolume;
        before = g_atmosGainDb;
    }
    float after = DesiredGainDb(before, userVolume);
    if (std::fabs(after - before) < 0.05f) return;
    {
        std::lock_guard<std::mutex> guard(g_lock);
        g_atmosGainDb = after;
    }
    if (std::fabs(after - before) >= 0.5f || g_track.lufsHops == kMinHopsToLevel)
        Log("atmos gain %.1f -> %.1f dB (%s)", before, after, g_track.cached ? "stored" : "measuring");
    PublishGain();
}

// ---------------------------------------------------------------------------
// Atmos detection

// Copies as much of the object's description as fits (CFStringGetCString fails outright when it doesn't).
static void Describe(CFTypeRef obj, char* buf, size_t size) {
    buf[0] = 0;
    if (!obj || size < 2) return;
    CFTypeRef str = CFCopyDescription(obj);
    if (!str) return;
    CFIndex used = 0;
    CFStringGetBytes(str, {0, CFStringGetLength(str)}, kCFStringEncodingUTF8, '?', 0,
                     reinterpret_cast<unsigned char*>(buf), static_cast<CFIndex>(size - 1), &used);
    buf[used] = 0;
    CFRelease(str);
}

// The stable variant id is a hash string; keep only what is inside the quotes of its description.
static void VariantId(void* item, char* buf, size_t size) {
    buf[0] = 0;
    CFTypeRef variant = AVCFPlayerItemCopyCurrentStableVariantID(item);
    if (!variant) return;
    char desc[256];
    Describe(variant, desc, sizeof(desc));
    CFRelease(variant);
    const char* open = strstr(desc, "contents = \"");
    if (!open) return;
    open += strlen("contents = \"");
    const char* close = strchr(open, '"');
    if (!close || static_cast<size_t>(close - open) >= size) return;
    memcpy(buf, open, close - open);
    buf[close - open] = 0;
}

// Atmos streams are (encrypted) E-AC-3 with JOC: 'qec3'/'ec-3' carrying a 16-channel
// 'qc+3'/'ec+3' layer. Stereo is AAC ('qaac') or ALAC ('qalc'). -1 while not loaded yet.
static int IsAtmosItem(void* item) {
    static constexpr const char* kAtmosTypes[] = {"'qec3'", "'ec-3'", "'qc+3'", "'ec+3'"};
    int result = -1;
    CFTypeRef tracks = AVCFPlayerItemCopyTracks(item);
    if (!tracks) return result;
    for (CFIndex i = 0; i < CFArrayGetCount(tracks) && result != 1; i++) {
        CFTypeRef asset = AVCFPlayerItemTrackCopyAssetTrack(CFArrayGetValueAtIndex(tracks, i));
        if (!asset) continue;
        CFTypeRef formats = AVCFAssetTrackCopyFormatDescriptions(asset);
        if (formats) {
            for (CFIndex j = 0; j < CFArrayGetCount(formats) && result != 1; j++) {
                char desc[1024];  // the media type and subtype come first in the description
                Describe(CFArrayGetValueAtIndex(formats, j), desc, sizeof(desc));
                if (!strstr(desc, "mediaType:'soun'")) continue;
                result = 0;
                for (const char* type : kAtmosTypes)
                    if (strstr(desc, type)) result = 1;
            }
            CFRelease(formats);
        }
        CFRelease(asset);
    }
    CFRelease(tracks);
    return result;
}

// Follows the active player's current item every cycle (not only when that player changes
// item, since the active player itself can change): a new item starts a new measured track.
static void FollowActiveTrack() {
    PlayerState s;
    {
        std::lock_guard<std::mutex> guard(g_lock);
        PlayerState* active = Find(g_activePlayer);
        if (!active) return;
        s = *active;
        CFRetain(s.player);
    }
    void* item = AVCFPlayerCopyCurrentItem(s.player);  // +1, handed to StartTrack when new
    bool sameFormatItem = item == s.lastItem;           // s.atmos describes this very item
    if (item != g_track.item || (sameFormatItem && s.formatKnown && s.atmos != g_track.atmos)) {
        StartTrack(item, sameFormatItem && s.atmos);
        {
            std::lock_guard<std::mutex> guard(g_lock);
            g_atmosGainDb = g_fallbackGainDb;
        }
        PublishGain();
    } else if (item) {
        CFRelease(item);
    }
    if (sameFormatItem) AddVariant(s.lastVariant);
    CFRelease(s.player);
}

static void PollPlayer(const PlayerState& snapshot) {
    void* item = AVCFPlayerCopyCurrentItem(snapshot.player);
    char variant[sizeof(snapshot.lastVariant)] = "";
    if (item) VariantId(item, variant, sizeof(variant));
    bool changed = item != snapshot.lastItem || strcmp(variant, snapshot.lastVariant) != 0;
    if (!changed && snapshot.formatKnown) {
        if (item) CFRelease(item);
        return;
    }
    // No stable variant yet means the item is still loading (and requesting its FairPlay key):
    // leave its tracks alone until then.
    int atmos = !item ? 0 : (variant[0] ? IsAtmosItem(item) : -1);
    if (item) CFRelease(item);

    bool isAtmos;
    {
        std::lock_guard<std::mutex> guard(g_lock);
        PlayerState* s = Find(snapshot.player);
        if (!s) return;
        s->lastItem = item;
        strcpy_s(s->lastVariant, variant);
        s->formatKnown = atmos >= 0;
        // Keep the previous state while the new item's format is still loading.
        isAtmos = atmos < 0 ? s->atmos : atmos == 1;
        s->atmos = isAtmos;
    }
    Log("item %p atmos=%d variant %.12s", item, atmos, variant);
}

static DWORD WINAPI PollThread(LPVOID) {
    while (WaitForSingleObject(g_stopEvent, kPollMs) == WAIT_TIMEOUT) {
        std::vector<PlayerState> snapshot;
        {
            std::lock_guard<std::mutex> guard(g_lock);
            snapshot = g_players;
            for (const auto& s : snapshot) CFRetain(s.player);  // may be evicted while we use it
        }
        for (const auto& s : snapshot) PollPlayer(s);
        for (const auto& s : snapshot) CFRelease(s.player);
        FollowActiveTrack();
        CollectHops();
        UpdateGain();
    }
    StartTrack(nullptr, false);  // stores the current track and drops its item
    return 0;
}

// ---------------------------------------------------------------------------
// Windhawk entry points

static void LoadSettings() {
    int db = std::clamp(Wh_GetIntSetting(L"atmosGainDb"), 0, kMaxGainDb);
    g_fallbackGainDb = static_cast<float>(db);
    g_autoLevel = Wh_GetIntSetting(L"autoLevel") != 0;
    g_log = Wh_GetIntSetting(L"diagnosticLog") != 0;
    std::lock_guard<std::mutex> guard(g_lock);
    if (!g_autoLevel || !g_trackAtmos) g_atmosGainDb = static_cast<float>(db);
    Log("settings: auto %d, fallback %d dB", g_autoLevel.load(), db);
}

template <typename T>
static bool Resolve(HMODULE module, const char* name, T& out) {
    out = reinterpret_cast<T>(GetProcAddress(module, name));
    if (!out) Wh_Log(L"missing export %S", name);
    return out != nullptr;
}

static bool ResolveApple(HMODULE av, HMODULE cf) {
    return Resolve(av, "AVCFPlayerCopyCurrentItem", AVCFPlayerCopyCurrentItem) &&
           Resolve(av, "AVCFPlayerItemCopyTracks", AVCFPlayerItemCopyTracks) &&
           Resolve(av, "AVCFPlayerItemCopyCurrentStableVariantID", AVCFPlayerItemCopyCurrentStableVariantID) &&
           Resolve(av, "AVCFPlayerItemTrackCopyAssetTrack", AVCFPlayerItemTrackCopyAssetTrack) &&
           Resolve(av, "AVCFAssetTrackCopyFormatDescriptions", AVCFAssetTrackCopyFormatDescriptions) &&
           Resolve(cf, "CFRetain", CFRetain) && Resolve(cf, "CFRelease", CFRelease) &&
           Resolve(cf, "CFCopyDescription", CFCopyDescription) &&
           Resolve(cf, "CFArrayGetCount", CFArrayGetCount) &&
           Resolve(cf, "CFArrayGetValueAtIndex", CFArrayGetValueAtIndex) &&
           Resolve(cf, "CFStringGetLength", CFStringGetLength) &&
           Resolve(cf, "CFStringGetBytes", CFStringGetBytes);
}

BOOL Wh_ModInit() {
    LoadSettings();
    HMODULE av = LoadLibraryW(L"AVFoundationCF.dll");
    HMODULE cf = LoadLibraryW(L"CoreFoundation.dll");
    if (!av || !cf) {
        Wh_Log(L"AVFoundationCF/CoreFoundation not found");
        return FALSE;
    }
    void* setVolume = (void*)GetProcAddress(av, "AVCFPlayerSetVolume");
    if (!setVolume || !ResolveApple(av, cf)) return FALSE;

    LoadStore();
    Wh_SetFunctionHook(setVolume, (void*)SetVolume_Hook, (void**)&SetVolume_Original);
    HookAudio();
    return TRUE;
}

void Wh_ModAfterInit() {
    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_thread = CreateThread(nullptr, 0, PollThread, nullptr, 0, nullptr);
    Log("mod started: %zu stored atmos tracks, %zu stereo", g_atmosLoudness.size(), g_stereoLoudness.size());
}

void Wh_ModSettingsChanged() {
    LoadSettings();
    PublishGain();
}

void Wh_ModBeforeUninit() {
    g_stopping = true;  // SetVolume calls are no longer tracked
    g_sampleGain = 1.0f;
    if (g_thread) {
        SetEvent(g_stopEvent);
        WaitForSingleObject(g_thread, INFINITE);
        CloseHandle(g_thread);
        CloseHandle(g_stopEvent);
        g_thread = nullptr;
    }
    g_musicRender.store(nullptr);
    std::vector<PlayerState> players;
    {
        std::lock_guard<std::mutex> guard(g_lock);
        players.swap(g_players);
    }
    for (const auto& s : players) CFRelease(s.player);  // outside the lock, like everywhere else
}
