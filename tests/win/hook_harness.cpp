// Wine harness for the Windows hook layer and the Atmos glue. Built only with -DAMPIFY_BUILD_HARNESS=ON.
// Run it from the build folder:  ampify_hook_harness.exe            (hook tests)
//                                ampify_hook_harness.exe Glue       (leveler start, its own process)
// The glue hooks the same import the import test uses, so the two never share a process.

#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#include "../../core/audio/audio_engine.hpp"
#include "../../core/journal.hpp"
#include "../../core/settings.hpp"
#include "../../extensions/atmos-leveler/atmos_windows.hpp"
#include "../../extensions/atmos-leveler/spatial_metadata.hpp"
#include "../../win/hook.hpp"
#include "../test.hpp"

extern "C" __declspec(dllimport) void __cdecl AVCFPlayerSetVolume(void* player, float volume);
extern "C" __declspec(dllimport) int __cdecl ampify_fake_volume_calls();
extern "C" __declspec(dllimport) void __cdecl ampify_fake_set_variant_known(int known);
extern "C" __declspec(dllimport) int __cdecl ampify_fake_track_reads();

namespace {

using Method = int (*)(void* self, int value);
using SetVolume = void (*)(void* player, float volume);

// A function address of any type, as a plain pointer. Copied rather than cast, so no warning about function types.
template <typename Fn>
void* AddressOf(Fn fn) {
    void* address = nullptr;
    std::memcpy(&address, &fn, sizeof(address));
    return address;
}

template <typename Fn>
Fn FromAddress(void* address) {
    Fn fn = nullptr;
    std::memcpy(&fn, &address, sizeof(fn));
    return fn;
}

std::filesystem::path FakePath() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(GetModuleHandleW(L"AVFoundationCF.dll"), path, MAX_PATH);
    return path;
}

// A copy of the fake under another name, loaded. The guard decides by the name the module was loaded under.
HMODULE LoadCopy(const wchar_t* name) {
    std::filesystem::path copy = FakePath().parent_path() / name;
    std::error_code error;
    std::filesystem::remove(copy, error);  // a copy left by an earlier run must not be reused
    std::filesystem::copy_file(FakePath(), copy, error);
    if (error) {
        std::cerr << "cannot copy the fake to " << copy.string() << ": " << error.message() << '\n';
        return nullptr;
    }
    return LoadLibraryW(copy.c_str());
}

// --- Vtable hooks ----------------------------------------------------------------------------------

int g_originalCalls = 0;
int g_replacementCalls = 0;
void* g_original = nullptr;

int Original(void*, int value) {
    g_originalCalls++;
    return value * 2;
}

int Replacement(void* self, int value) {
    g_replacementCalls++;
    return FromAddress<Method>(g_original)(self, value) + 1;
}

struct FakeObject {
    void** vtable;
};

TEST_CASE(Vtable_ReplacementRunsAndCanCallTheOriginal) {
    void* table[2] = {AddressOf(Original), AddressOf(Original)};
    FakeObject object{table};
    void** entry = ampify::win::VtableEntry(&object, 1);

    std::string why;
    void* original = nullptr;
    CHECK(ampify::win::HookVtableEntry(entry, AddressOf(Replacement), &original, why));
    CHECK(original == AddressOf(Original));
    g_original = original;

    // The object calls through its table, so it reaches the replacement, which calls the original.
    int result = FromAddress<Method>(*entry)(&object, 5);
    CHECK(result == 11);
    CHECK(g_replacementCalls == 1);
    CHECK(g_originalCalls == 1);

    // A second hook on the same entry hands back the first original and keeps the replacement.
    void* again = nullptr;
    CHECK(ampify::win::HookVtableEntry(entry, AddressOf(Replacement), &again, why));
    CHECK(again == AddressOf(Original));

    // The table is local to this test: put the entry back before it goes, or a later restore writes into this stack.
    ampify::win::RestoreHooks();
    CHECK(*entry == AddressOf(Original));
}

TEST_CASE(Vtable_RefusesMethodsOfProtectedModules) {
    // CoreFP and a FairPlay-named module: neither may be hooked, and the entry must stay as it was.
    for (const wchar_t* name : {L"CoreFP.dll", L"AppleFairPlayHelper.dll"}) {
        HMODULE module = LoadCopy(name);
        CHECK(module != nullptr);
        if (!module) continue;

        void* method = AddressOf(GetProcAddress(module, "ampify_fake_protected_method"));
        void* table[1] = {method};
        FakeObject object{table};

        std::string why;
        void* original = nullptr;
        CHECK(!ampify::win::HookVtableEntry(ampify::win::VtableEntry(&object, 0), AddressOf(Replacement), &original, why));
        CHECK(why.find("protected") != std::string::npos);
        CHECK(table[0] == method);
    }
}

// --- Import table hooks ----------------------------------------------------------------------------

SetVolume g_importOriginal = nullptr;
int g_importReplacementCalls = 0;

void __cdecl ImportReplacement(void* player, float volume) {
    g_importReplacementCalls++;
    g_importOriginal(player, volume);
}

// Each call reads the import table afresh. Without this, the compiler may keep the import's address across the
// hook call below, since it cannot see that HookImportEverywhere changed it.
__attribute__((noinline)) void CallImportedVolume(float volume) {
    AVCFPlayerSetVolume(nullptr, volume);
}

TEST_CASE(Import_ReplacesCallsMadeThroughTheImportTable) {
    int before = ampify_fake_volume_calls();
    CallImportedVolume(0.5f);
    CHECK(ampify_fake_volume_calls() == before + 1);

    void* original = nullptr;
    size_t replaced = ampify::win::HookImportEverywhere("AVFoundationCF.dll", "AVCFPlayerSetVolume",
                                                     AddressOf(ImportReplacement), &original);
    CHECK(replaced >= 1);

    void* real = AddressOf(GetProcAddress(GetModuleHandleW(L"AVFoundationCF.dll"), "AVCFPlayerSetVolume"));
    CHECK(original == real);
    g_importOriginal = FromAddress<SetVolume>(original);

    CallImportedVolume(0.25f);
    CHECK(g_importReplacementCalls == 1);
    CHECK(ampify_fake_volume_calls() == before + 2);  // the replacement passed the call on

    ampify::win::RestoreHooks();
    CallImportedVolume(0.1f);
    CHECK(g_importReplacementCalls == 1);  // back to the import: the replacement is not called again
}

// --- Atmos glue (own process) ----------------------------------------------------------------------

// About one second of a 1 kHz tone through a shared stream on the default device, as Apple Music plays.
// The hooks installed by the glue see this stream: its render client is the one they meter.
void PlayToneThroughHookedStream(bool& float32) {
    const GUID clsid = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
    const GUID iidEnumerator = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
    const GUID iidClient = {0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
    const GUID iidRender = {0xF294ACFC, 0x3146, 0x4483, {0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2}};

    HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
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
              SUCCEEDED(client->GetService(iidRender, reinterpret_cast<void**>(&render)));
    CHECK(ok);
    float32 = ok && format->wBitsPerSample == 32;
    if (ok && float32) {
        UINT32 bufferFrames = 0;
        client->GetBufferSize(&bufferFrames);
        const int channels = format->nChannels;
        const double rate = format->nSamplesPerSec;
        const double pi = 3.14159265358979323846;

        client->Start();
        double phase = 0;
        UINT32 played = 0;
        while (played < format->nSamplesPerSec) {
            UINT32 padding = 0;
            client->GetCurrentPadding(&padding);
            UINT32 frames = bufferFrames - padding;
            if (frames == 0) {
                Sleep(5);
                continue;
            }

            BYTE* data = nullptr;
            if (FAILED(render->GetBuffer(frames, &data))) break;
            float* samples = reinterpret_cast<float*>(data);
            for (UINT32 frame = 0; frame < frames; frame++) {
                float value = static_cast<float>(0.1 * std::sin(2.0 * pi * 1000.0 * phase / rate));
                phase += 1.0;
                for (int channel = 0; channel < channels; channel++) samples[frame * channels + channel] = value;
            }
            render->ReleaseBuffer(frames, 0);
            played += frames;
        }
        client->Stop();
    }

    if (render) render->Release();
    if (client) client->Release();
    if (device) device->Release();
    if (enumerator) enumerator->Release();
    if (format) CoTaskMemFree(format);
    if (SUCCEEDED(com)) CoUninitialize();
}

// The log of the glue, shared by the Glue tests: they run in this order, in one process, and the leveler
// starts once.
std::ostringstream g_glueText;
ampify::Journal g_glueJournal(g_glueText, [] { return std::string("00:00:00.000"); });
bool g_glueStarted = false;

bool Journaled(const std::string& line) {
    return g_glueText.str().find(line) != std::string::npos;
}

TEST_CASE(Glue_StartsOnWineAndMetersTheStereoStream) {
    LoadCopy(L"CoreFoundation.dll");

    // An earlier run measured two stereo tracks at -16 LUFS and the fake's Atmos variant at -20 LUFS.
    std::filesystem::path levels = atmos::hooks::testing::LevelsFile();
    std::error_code error;
    std::filesystem::create_directories(levels.parent_path(), error);
    std::ofstream(levels, std::ios::binary | std::ios::trunc) << "stereo -16.0\nstereo -16.0\natmos abc123 -20.0 300.0\n";

    // The item is still loading: no stable variant yet.
    ampify_fake_set_variant_known(0);

    ampify::Settings settings;
    ampify::ExtensionInfo info = atmos::hooks::MakeAtmosLevelerExtension(g_glueJournal, settings);
    CHECK(info.id == "atmos-leveler");
    CHECK(!info.enabledByDefault);

    g_glueStarted = info.start();
    CHECK(g_glueStarted);
    CHECK(Journaled("hook AVCFPlayerSetVolume placed"));
    CHECK(Journaled("levels read back: 1 Atmos variant(s), 2 stereo track(s)"));

    // Apple Music sets the player's volume: the hook sees the player, and the poll thread finds its item.
    static char player;
    int volumeCalls = ampify_fake_volume_calls();
    AVCFPlayerSetVolume(&player, 0.5f);
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Without a variant the item is left alone: its tracks are not even read.
    CHECK(ampify_fake_track_reads() == 0);
    CHECK(!Journaled("Dolby Atmos track"));

    ampify_fake_set_variant_known(1);
    bool float32 = false;
    PlayToneThroughHookedStream(float32);
    CHECK(float32);
    std::this_thread::sleep_for(std::chrono::seconds(1));  // a few poll cycles after the item is known

    // The stereo render client was identified, and the stereo meter took its rate.
    CHECK(Journaled("stereo measurement at "));
    // The fake item is Dolby Atmos: it starts at the fallback gain, then jumps to its saved level, 4 dB under
    // the saved stereo target.
    CHECK(Journaled("Dolby Atmos track, variant abc123, starting gain 5.0 dB"));
    CHECK(Journaled("gain 5.0 -> 4.0 dB"));
    // The slider was never moved: the only call to AVCFPlayerSetVolume is the one above.
    CHECK(ampify_fake_volume_calls() == volumeCalls + 1);
    CHECK(!Journaled("CoreFP"));
}

// --- Fake Atmos stream: ISpatialAudioObjectRenderStreamForMetadata ----------------------------------

constexpr UINT32 kPassFrames = 480;  // 10 ms at 48 kHz

// One metadata object: hands out its buffer from GetBuffer (slot 3). `Kind` gives each object type its own
// vtable, as the commands and items objects have.
template <int Kind>
class FakeMetadataObject : public ISpatialAudioObjectBase {
public:
    float samples[kPassFrames] = {};
    int getBufferCalls = 0;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** object) override {
        *object = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE GetBuffer(BYTE** buffer, UINT32* bytes) override {
        getBufferCalls++;
        *buffer = reinterpret_cast<BYTE*>(samples);
        *bytes = sizeof(samples);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetEndOfStream(UINT32) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE IsActive(WINBOOL* active) override {
        *active = TRUE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetAudioObjectType(AudioObjectType* type) override {
        *type = AudioObjectType_Dynamic;
        return S_OK;
    }
};

using FakeCommandsObject = FakeMetadataObject<1>;
using FakeItemsObject = FakeMetadataObject<2>;
using MetadataStream = atmos::hooks::ISpatialAudioObjectRenderStreamForMetadataLite;

// The stream, declared through the interface of spatial_metadata.hpp: the compiler lays out its vtable, so the
// hooks on slots 8, 9, 10 and 11 land on these methods only if the leveler's slot numbers match the declaration.
class FakeMetadataStream : public MetadataStream {
public:
    FakeCommandsObject commands;
    FakeItemsObject items;
    int beginCalls = 0;
    int endCalls = 0;
    int activations = 0;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** object) override {
        *object = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE GetAvailableDynamicObjectCount(UINT32* count) override {
        *count = 16;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetService(REFIID, void**) override { return E_NOINTERFACE; }
    HRESULT STDMETHODCALLTYPE Start() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Stop() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Reset() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE BeginUpdatingAudioObjects(UINT32* count, UINT32* frames) override {
        beginCalls++;
        *count = 16;
        *frames = kPassFrames;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EndUpdatingAudioObjects() override {
        endCalls++;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ActivateSpatialAudioObjectForMetadataCommands(AudioObjectType,
                                                                            ISpatialAudioObjectBase** object) override {
        activations++;
        *object = &commands;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ActivateSpatialAudioObjectForMetadataItems(AudioObjectType,
                                                                         ISpatialAudioObjectBase** object) override {
        activations++;
        *object = &items;
        return S_OK;
    }
};

// The pointer as the compiler cannot see through it, so every call goes through the vtable, as Apple Music's do.
template <typename T>
__attribute__((noinline)) T* Opaque(T* pointer) {
    asm volatile("" : "+r"(pointer));
    return pointer;
}

TEST_CASE(Glue_HooksTheMetadataStreamAndGainsItsObjects) {
    CHECK(g_glueStarted);
    if (!g_glueStarted) return;

    // Apple Music activates its Atmos stream: float32 mono objects at 48 kHz.
    static FakeMetadataStream fake;
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    format.nChannels = 1;
    format.nSamplesPerSec = 48000;
    format.wBitsPerSample = 32;
    format.nBlockAlign = 4;
    format.nAvgBytesPerSec = 48000 * 4;

    atmos::hooks::SpatialAudioObjectRenderStreamForMetadataActivationParamsLite activation{};
    activation.ObjectFormat = &format;
    activation.MaxDynamicObjectCount = 16;
    activation.Category = AudioCategory_Media;
    PROPVARIANT params{};
    params.vt = VT_BLOB;
    params.blob.cbSize = sizeof(activation);
    params.blob.pBlobData = reinterpret_cast<BYTE*>(&activation);
    atmos::hooks::testing::SpatialStreamActivated(&params, atmos::hooks::kIidRenderStreamForMetadata, &fake);

    CHECK(Journaled("spatial stream activated: ISpatialAudioObjectRenderStreamForMetadata "
                    "(IID BBC9C907-48D5-4A2E-A0C7-F7F0D67C1FB1)"));
    CHECK(Journaled("Atmos measurement at 48000 Hz"));
    CHECK(Journaled("hook ISpatialAudioObjectRenderStreamForMetadata::BeginUpdatingAudioObjects placed"));
    CHECK(Journaled("hook ISpatialAudioObjectRenderStreamForMetadata::EndUpdatingAudioObjects placed"));
    CHECK(Journaled("hook ISpatialAudioObjectRenderStreamForMetadata::ActivateSpatialAudioObjectForMetadataCommands placed"));
    CHECK(Journaled("hook ISpatialAudioObjectRenderStreamForMetadata::ActivateSpatialAudioObjectForMetadataItems placed"));

    MetadataStream* stream = Opaque<MetadataStream>(&fake);
    ISpatialAudioObjectBase* commands = nullptr;
    ISpatialAudioObjectBase* items = nullptr;
    CHECK(SUCCEEDED(stream->ActivateSpatialAudioObjectForMetadataCommands(AudioObjectType_Dynamic, &commands)));
    CHECK(SUCCEEDED(stream->ActivateSpatialAudioObjectForMetadataItems(AudioObjectType_Dynamic, &items)));
    CHECK(fake.activations == 2);  // the hooks passed the calls on
    CHECK(Journaled("hook ISpatialAudioObjectForMetadataCommands::GetBuffer placed"));
    CHECK(Journaled("hook ISpatialAudioObjectForMetadataItems::GetBuffer placed"));
    if (!commands || !items) return;
    commands = Opaque(commands);
    items = Opaque(items);

    // Two seconds of passes, as Apple Music renders them: open the pass, fill each object's buffer, close it.
    constexpr int kPasses = 200;
    constexpr float kSample = 0.1f;
    for (int pass = 0; pass < kPasses; pass++) {
        UINT32 count = 0;
        UINT32 frames = 0;
        stream->BeginUpdatingAudioObjects(&count, &frames);
        for (ISpatialAudioObjectBase* object : {commands, items}) {
            BYTE* buffer = nullptr;
            UINT32 bytes = 0;
            object->GetBuffer(&buffer, &bytes);
            float* samples = reinterpret_cast<float*>(buffer);
            for (UINT32 frame = 0; frame < frames; frame++) samples[frame] = kSample;
        }
        stream->EndUpdatingAudioObjects();
    }
    CHECK(fake.beginCalls == kPasses);
    CHECK(fake.endCalls == kPasses);
    CHECK(fake.commands.getBufferCalls == kPasses);
    CHECK(fake.items.getBufferCalls == kPasses);

    // The saved level asks for 4 dB: the ramp has converged, and the objects carry it.
    double expected = kSample * std::pow(10.0, 4.0 / 20.0);
    CHECK_NEAR(fake.items.samples[kPassFrames - 1], expected, 1e-3);
    CHECK_NEAR(fake.commands.samples[kPassFrames - 1], expected, 1e-3);

    std::cout << g_glueText.str();
}

// Ampify.audio on the same hooks: the analysis and the equalizer see the stereo stream that the leveler meters.
// The tone is played twice, flat then with +6 dB on the 1 kHz band: the analysed 1 kHz band rises by 6 dB.
TEST_CASE(Glue_AnalysisAndEqualizerSeeTheStereoStream) {
    CHECK(g_glueStarted);
    if (!g_glueStarted) return;
    ampify::audio::AudioEngine& engine = ampify::audio::ProcessAudio();
    engine.Analyze("harness");  // starts the analysis
    CHECK(engine.Eq().IsFlat());

    auto band1k = [&engine]() -> double {
        ampify::AudioAnalysisResult result = engine.Analyze("harness");
        if (!result.frame || result.frame->bandsDb.size() <= 8) return -200;
        return result.frame->bandsDb[8];  // the band of 800 Hz to 1.16 kHz
    };

    bool float32 = false;
    PlayToneThroughHookedStream(float32);
    double flat = band1k();
    CHECK(engine.Loudness().momentary.has_value());  // the live loudness of the stream
    CHECK_NEAR(engine.PlayerVolume().value_or(0), 0.5, 1e-6);  // read from the volume hook, never written

    CHECK(engine.SetEqBand("harness", 5, 6.0));
    PlayToneThroughHookedStream(float32);
    double boosted = band1k();
    std::cout << "1 kHz band: " << flat << " dB flat, " << boosted << " dB with +6 dB\n";
    CHECK(flat > -30);
    CHECK_NEAR(boosted - flat, 6.0, 0.5);

    engine.Release("harness");  // the equalizer goes back to flat
    CHECK(engine.Eq().IsFlat());

    // The levels file belongs to this run only.
    std::error_code error;
    std::filesystem::remove(atmos::hooks::testing::LevelsFile(), error);
}

}  // namespace

int main(int argc, char** argv) {
    std::string prefix = argc > 1 ? argv[1] : "";
    int ran = 0;
    int passed = 0;
    for (const ampify::test::Case& test : ampify::test::Cases()) {
        bool glue = test.name.rfind("Glue", 0) == 0;
        bool selected = prefix.empty() ? !glue : test.name.rfind(prefix, 0) == 0;
        if (!selected) continue;

        int before = ampify::test::Failures();
        try {
            test.run();
        } catch (const std::exception& error) {
            ampify::test::ReportFailure(test.name.c_str(), 0, std::string("exception: ") + error.what());
        } catch (...) {
            ampify::test::ReportFailure(test.name.c_str(), 0, "exception");
        }
        ran++;
        if (ampify::test::Failures() == before) {
            passed++;
        } else {
            std::cerr << "FAILED: " << test.name << '\n';
        }
    }

    std::cout << passed << " / " << ran << " harness tests passed\n";
    return ampify::test::Failures() == 0 ? 0 : 1;
}
