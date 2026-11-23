// Windows only: placing a hook never deadlocks against the loader.
#include <windows.h>

#include <atomic>
#include <iostream>
#include <thread>

#include "../../src/win/hook.hpp"
#include "../test.hpp"

namespace {

int Answer() { return 1; }
int Replacement() { return 2; }
void* g_table[] = {reinterpret_cast<void*>(&Answer)};

constexpr int kRounds = 1000;
constexpr DWORD kDeadlineMs = 60000;

// A System32 DLL this process has not loaded, so each LoadLibrary maps it again and notifies the loader's listeners.
const wchar_t* UnloadedSystemDll() {
    for (const wchar_t* name : {L"msimg32.dll", L"wtsapi32.dll", L"pdh.dll", L"mscms.dll", L"dhcpcsvc6.dll"}) {
        if (!GetModuleHandleW(name)) return name;
    }
    return nullptr;
}

// A deadlock cannot be undone: the run is reported and the process ends, since exiting would wait for the loader.
void FailOnDeadlock(std::thread& hooker) {
    if (WaitForSingleObject(hooker.native_handle(), kDeadlineMs) == WAIT_OBJECT_0) return;
    std::cerr << "hook_lock_tests: HookVtableEntry deadlocked with a loading DLL\n";
    TerminateProcess(GetCurrentProcess(), 3);
}

}  // namespace

TEST_CASE(Hook_VtableHookNeverDeadlocksWithALoadingDll) {
    const wchar_t* dll = UnloadedSystemDll();
    CHECK(dll != nullptr);
    if (!dll) return;
    // A hook for later modules registers the loader notification, which takes the hook lock under the loader lock.
    void* unused = nullptr;
    ampify::win::HookImportFromNowOn("ampify-test-none.dll", "None", reinterpret_cast<void*>(&Replacement), &unused);

    std::atomic<bool> hooking{true};
    std::thread loader([&] {
        while (hooking.load()) {
            if (HMODULE module = LoadLibraryExW(dll, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) FreeLibrary(module);
        }
    });
    int placed = 0;
    void* original = nullptr;
    std::thread hooker([&] {
        for (int round = 0; round < kRounds; round++) {
            std::string why;
            if (ampify::win::HookVtableEntry(&g_table[0], reinterpret_cast<void*>(&Replacement), &original, why)) {
                placed++;
            }
        }
    });
    FailOnDeadlock(hooker);
    hooker.join();
    hooking = false;
    loader.join();

    CHECK(placed == kRounds);
    CHECK(original == reinterpret_cast<void*>(&Answer));
}

// Windows' audio stack frees codecs while the kernel starts: one freed after the modules were listed is left out
// instead of read (reading it crashed AMPLibraryAgent), and the modules held meanwhile are let go afterwards.
TEST_CASE(Hook_HeldModulesLeaveOutAModuleFreedAfterTheListing) {
    const wchar_t* dll = UnloadedSystemDll();
    CHECK(dll != nullptr);
    if (!dll) return;
    HMODULE freed = LoadLibraryExW(dll, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    CHECK(freed != nullptr);
    if (!freed) return;
    FreeLibrary(freed);
    CHECK(GetModuleHandleW(dll) == nullptr);
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");

    const ampify::win::HeldModules held({freed, kernel});

    CHECK(held.modules() == std::vector<HMODULE>{kernel});
}

TEST_CASE(Hook_HeldModulesLetTheirModulesGo) {
    const wchar_t* dll = UnloadedSystemDll();
    CHECK(dll != nullptr);
    if (!dll) return;
    HMODULE module = LoadLibraryExW(dll, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    CHECK(module != nullptr);
    if (!module) return;

    {
        const ampify::win::HeldModules held({module});
        CHECK(held.modules().size() == 1);
        FreeLibrary(module);
        CHECK(GetModuleHandleW(dll) == module);  // still held
    }

    CHECK(GetModuleHandleW(dll) == nullptr);
}
