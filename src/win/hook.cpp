#include "hook.hpp"

#include <psapi.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>

#include "../core/drm_guard.hpp"
#include "win_common.hpp"

namespace ampify::win {

namespace {

std::mutex g_lock;
std::map<void**, void*> g_replaced;  // entry address -> the address it held before the first hook

std::string PathOfModule(HMODULE module) {
    std::wstring path(MAX_PATH, L'\0');
    DWORD length = 0;
    while (true) {
        length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0) return {};
        if (length < path.size() - 1) break;
        path.resize(path.size() * 2);  // truncated: try again with more room
    }
    path.resize(length);
    return ToUtf8(path);
}

// Vtables and import tables sit in read-only sections: the page is made writable for one store.
bool WritePointer(void** entry, void* value) {
    DWORD oldProtect = 0;
    if (!VirtualProtect(entry, sizeof(void*), PAGE_READWRITE, &oldProtect)) return false;
    *static_cast<void* volatile*>(entry) = value;
    VirtualProtect(entry, sizeof(void*), oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), entry, sizeof(void*));
    return true;
}

// Replaces one entry and remembers the first address it held. Caller holds g_lock.
bool Replace(void** entry, void* replacement, void** original) {
    auto known = g_replaced.find(entry);
    void* current = *entry;
    if (known == g_replaced.end()) {
        if (!WritePointer(entry, replacement)) return false;
        g_replaced[entry] = current;
        if (original) *original = current;
        return true;
    }
    // Already replaced: the entry must still hold our replacement, and the original is the first one.
    if (current != replacement) return false;
    if (original) *original = known->second;
    return true;
}

// Import table entries of one module for `dll!function`. Returns how many were replaced.
size_t PatchImportsOf(HMODULE module, const char* dll, const char* function, void* replacement, void** original) {
    const BYTE* base = reinterpret_cast<const BYTE*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;

    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;

    const IMAGE_DATA_DIRECTORY& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (directory.VirtualAddress == 0) return 0;

    size_t replaced = 0;
    const auto* descriptor = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
    for (; descriptor->Name != 0; descriptor++) {
        if (_stricmp(reinterpret_cast<const char*>(base + descriptor->Name), dll) != 0) continue;
        if (descriptor->OriginalFirstThunk == 0) continue;  // bound imports: no names to match

        // The names live in the original first thunk; the loader writes the addresses into the first thunk.
        const auto* names = reinterpret_cast<const IMAGE_THUNK_DATA*>(base + descriptor->OriginalFirstThunk);
        auto* addresses = reinterpret_cast<IMAGE_THUNK_DATA*>(const_cast<BYTE*>(base + descriptor->FirstThunk));
        for (size_t i = 0; names[i].u1.AddressOfData != 0; i++) {
            if (IMAGE_SNAP_BY_ORDINAL(names[i].u1.Ordinal)) continue;

            const auto* import = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names[i].u1.AddressOfData);
            if (strcmp(reinterpret_cast<const char*>(import->Name), function) != 0) continue;

            void** entry = reinterpret_cast<void**>(&addresses[i].u1.Function);
            if (Replace(entry, replacement, original)) replaced++;
        }
    }
    return replaced;
}

}  // namespace

void** VtableEntry(void* object, size_t slot) {
    void** table = *reinterpret_cast<void***>(object);
    return &table[slot];
}

bool HookVtableEntry(void** entry, void* replacement, void** original, std::string& why) {
    if (!entry || !replacement || !original) {
        why = "null argument";
        return false;
    }

    std::lock_guard<std::mutex> guard(g_lock);
    std::string module = ModuleOf(*entry);
    if (IsProtectedModule(module)) {
        why = "refused: " + module + " is a protected module";
        return false;
    }
    if (!Replace(entry, replacement, original)) {
        why = "vtable slot cannot be modified";
        return false;
    }
    return true;
}

size_t HookImportEverywhere(const char* dll, const char* function, void* replacement, void** original) {
    std::vector<HMODULE> modules(1024);
    DWORD needed = 0;
    if (!EnumProcessModules(GetCurrentProcess(), modules.data(),
                            static_cast<DWORD>(modules.size() * sizeof(HMODULE)), &needed)) {
        return 0;
    }
    modules.resize(std::min<size_t>(needed / sizeof(HMODULE), modules.size()));

    std::lock_guard<std::mutex> guard(g_lock);
    size_t replaced = 0;
    for (HMODULE module : modules) {
        if (IsProtectedModule(PathOfModule(module))) continue;
        replaced += PatchImportsOf(module, dll, function, replacement, original);
    }
    return replaced;
}

std::string ModuleOf(const void* address) {
    HMODULE module = nullptr;
    constexpr DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
    if (!GetModuleHandleExW(flags, reinterpret_cast<LPCWSTR>(address), &module)) return {};
    return PathOfModule(module);
}

}  // namespace ampify::win
