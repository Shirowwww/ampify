// Wine harness for the Windows hook layer and the Atmos glue. Built only with -DAMK_BUILD_HARNESS=ON.
// Run it from the build folder:  amk_hook_harness.exe            (hook tests)
//                                amk_hook_harness.exe Glue       (leveler start, its own process)
// The glue hooks the same import the import test uses, so the two never share a process.

#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#include "../../core/journal.hpp"
#include "../../core/settings.hpp"
#include "../../extensions/atmos-leveler/atmos_windows.hpp"
#include "../../win/hook.hpp"
#include "../test.hpp"

extern "C" __declspec(dllimport) void __cdecl AVCFPlayerSetVolume(void* player, float volume);
extern "C" __declspec(dllimport) int __cdecl amk_fake_volume_calls();

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
    void** entry = amk::win::VtableEntry(&object, 1);

    std::string why;
    void* original = nullptr;
    CHECK(amk::win::HookVtableEntry(entry, AddressOf(Replacement), &original, why));
    CHECK(original == AddressOf(Original));
    g_original = original;

    // The object calls through its table, so it reaches the replacement, which calls the original.
    int result = FromAddress<Method>(*entry)(&object, 5);
    CHECK(result == 11);
    CHECK(g_replacementCalls == 1);
    CHECK(g_originalCalls == 1);

    // A second hook on the same entry hands back the first original and keeps the replacement.
    void* again = nullptr;
    CHECK(amk::win::HookVtableEntry(entry, AddressOf(Replacement), &again, why));
    CHECK(again == AddressOf(Original));

    // The table is local to this test: put the entry back before it goes, or a later restore writes into this stack.
    amk::win::RestoreHooks();
    CHECK(*entry == AddressOf(Original));
}

TEST_CASE(Vtable_RefusesMethodsOfProtectedModules) {
    // CoreFP and a FairPlay-named module: neither may be hooked, and the entry must stay as it was.
    for (const wchar_t* name : {L"CoreFP.dll", L"AppleFairPlayHelper.dll"}) {
        HMODULE module = LoadCopy(name);
        CHECK(module != nullptr);
        if (!module) continue;

        void* method = AddressOf(GetProcAddress(module, "amk_fake_protected_method"));
        void* table[1] = {method};
        FakeObject object{table};

        std::string why;
        void* original = nullptr;
        CHECK(!amk::win::HookVtableEntry(amk::win::VtableEntry(&object, 0), AddressOf(Replacement), &original, why));
        CHECK(why.find("protégé") != std::string::npos);
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
    int before = amk_fake_volume_calls();
    CallImportedVolume(0.5f);
    CHECK(amk_fake_volume_calls() == before + 1);

    void* original = nullptr;
    size_t replaced = amk::win::HookImportEverywhere("AVFoundationCF.dll", "AVCFPlayerSetVolume",
                                                     AddressOf(ImportReplacement), &original);
    CHECK(replaced >= 1);

    void* real = AddressOf(GetProcAddress(GetModuleHandleW(L"AVFoundationCF.dll"), "AVCFPlayerSetVolume"));
    CHECK(original == real);
    g_importOriginal = FromAddress<SetVolume>(original);

    CallImportedVolume(0.25f);
    CHECK(g_importReplacementCalls == 1);
    CHECK(amk_fake_volume_calls() == before + 2);  // the replacement passed the call on

    amk::win::RestoreHooks();
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

TEST_CASE(Glue_StartsOnWineAndMetersTheStereoStream) {
    static std::ostringstream text;
    static amk::Journal journal(text, [] { return std::string("00:00:00.000"); });

    LoadCopy(L"CoreFoundation.dll");
    amk::Settings settings;
    amk::ExtensionInfo info = atmos::hooks::MakeAtmosLevelerExtension(journal, settings);
    CHECK(info.id == "atmos-leveler");
    CHECK(!info.enabledByDefault);

    bool started = info.start();
    CHECK(started);
    CHECK(text.str().find("hook AVCFPlayerSetVolume posé") != std::string::npos);

    // Apple Music sets the player's volume: the hook sees the player, and the poll thread finds its item.
    static char player;
    AVCFPlayerSetVolume(&player, 0.5f);

    bool float32 = false;
    PlayToneThroughHookedStream(float32);
    CHECK(float32);
    std::this_thread::sleep_for(std::chrono::seconds(1));  // a few poll cycles after the item is known

    std::cout << text.str();
    std::cout << "start() returned " << (started ? "true" : "false") << '\n';

    // The first stream sets the meter's rate: the stereo render client was identified and metered.
    CHECK(text.str().find("débit du premier flux (flux stéréo)") != std::string::npos);
    // The fake item is Dolby Atmos: the leveler starts it at the fallback gain, before any target is known.
    CHECK(text.str().find("piste Dolby Atmos, variante abc123, gain de départ 5.0 dB") != std::string::npos);
    CHECK(text.str().find("CoreFP") == std::string::npos);
}

}  // namespace

int main(int argc, char** argv) {
    std::string prefix = argc > 1 ? argv[1] : "";
    int ran = 0;
    int passed = 0;
    for (const amk::test::Case& test : amk::test::Cases()) {
        bool glue = test.name.rfind("Glue", 0) == 0;
        bool selected = prefix.empty() ? !glue : test.name.rfind(prefix, 0) == 0;
        if (!selected) continue;

        int before = amk::test::Failures();
        try {
            test.run();
        } catch (const std::exception& error) {
            amk::test::ReportFailure(test.name.c_str(), 0, std::string("exception: ") + error.what());
        } catch (...) {
            amk::test::ReportFailure(test.name.c_str(), 0, "exception");
        }
        ran++;
        if (amk::test::Failures() == before) {
            passed++;
        } else {
            std::cerr << "FAILED: " << test.name << '\n';
        }
    }

    std::cout << passed << " / " << ran << " harness tests passed\n";
    return amk::test::Failures() == 0 ? 0 : 1;
}
