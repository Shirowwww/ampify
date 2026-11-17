// Apple's agent plays local files with a player of its own, not AVFoundationCF's: it decodes them itself and sends
// them to a CoreAudioToolbox output unit (seen on 17/11/2026). Its controls show only there: the unit started to play,
// reset when what it queued is dropped (a pause, a skip, a move in the song), stopped a second after a pause.
#include <windows.h>
#include <intrin.h>

#include <string>

#include "../hook.hpp"
#include "hooks_detail.hpp"

namespace ampify::win::audio_hooks::detail {
namespace {

using UnitCall_t = long(__cdecl*)(void* unit);
using UnitReset_t = long(__cdecl*)(void* unit, unsigned scope, unsigned element);

UnitCall_t g_start = nullptr;
UnitCall_t g_stop = nullptr;
UnitReset_t g_reset = nullptr;

// Only the agent's own calls: its local file player lives in its executable.
bool FromAgent(void* returnAddress) {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       static_cast<LPCWSTR>(returnAddress), &module);
    return module && module == GetModuleHandleW(nullptr);
}

long __cdecl Start_Hook(void* unit) {
    if (FromAgent(_ReturnAddress())) SingLocalPlays();
    return g_start(unit);
}

long __cdecl Stop_Hook(void* unit) {
    const long result = g_stop(unit);
    if (FromAgent(_ReturnAddress())) SingLocalStopped();
    return result;
}

long __cdecl Reset_Hook(void* unit, unsigned scope, unsigned element) {
    const long result = g_reset(unit, scope, element);
    if (FromAgent(_ReturnAddress())) SingLocalReset();
    return result;
}

}  // namespace

void StartLocalPlayerWatch() {
    const char* dll = "CoreAudioToolbox.dll";
    const size_t start = HookImportFromNowOn(dll, "AudioOutputUnitStart", AddressOf(&Start_Hook),
                                             reinterpret_cast<void**>(&g_start));
    const size_t stop = HookImportFromNowOn(dll, "AudioOutputUnitStop", AddressOf(&Stop_Hook),
                                            reinterpret_cast<void**>(&g_stop));
    const size_t reset = HookImportFromNowOn(dll, "AudioUnitReset", AddressOf(&Reset_Hook),
                                             reinterpret_cast<void**>(&g_reset));
    if (start == 0 || stop == 0 || reset == 0) {
        Note("Sing: Apple's local file player not followed, studio mode may play a local song's end after a skip");
    }
}

}  // namespace ampify::win::audio_hooks::detail
