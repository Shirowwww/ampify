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

// Replaces one entry and remembers the first address it held. Caller holds g_lock. `original` is filled before the
// entry changes: another thread can call the replacement as soon as it is written, and it calls the original.
// `before`, when set, is what the original is instead of the entry's content (a delay-load stub is no original).
bool Replace(void** entry, void* replacement, void** original, void* before = nullptr) {
    auto known = g_replaced.find(entry);
    void* current = *entry;
    if (known == g_replaced.end()) {
        void* kept = before ? before : current;
        if (original) *original = kept;
        if (!WritePointer(entry, replacement)) return false;
        g_replaced[entry] = kept;
        return true;
    }
    // Already replaced: the entry must still hold our replacement, and the original is the first one.
    if (current != replacement) return false;
    if (original) *original = known->second;
    return true;
}

const IMAGE_NT_HEADERS* HeadersOf(const BYTE* base) {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE ? nt : nullptr;
}

// The address entries whose name entry imports `function` by name (the names and the addresses run in parallel).
std::vector<void**> EntriesNamed(const BYTE* base, DWORD namesRva, DWORD addressesRva, const char* function) {
    std::vector<void**> entries;
    const auto* names = reinterpret_cast<const IMAGE_THUNK_DATA*>(base + namesRva);
    auto* addresses = reinterpret_cast<IMAGE_THUNK_DATA*>(const_cast<BYTE*>(base + addressesRva));
    for (size_t i = 0; names[i].u1.AddressOfData != 0; i++) {
        if (IMAGE_SNAP_BY_ORDINAL(names[i].u1.Ordinal)) continue;
        const auto* import = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names[i].u1.AddressOfData);
        if (strcmp(reinterpret_cast<const char*>(import->Name), function) == 0) {
            entries.push_back(reinterpret_cast<void**>(&addresses[i].u1.Function));
        }
    }
    return entries;
}

// Delay-loaded imports, which most of Windows' own DLLs use for Winsock: until the first call an entry holds a stub
// of the module itself, which would load the function and write it over the hook. So the original is the export
// (`exported`, resolved by the caller outside g_lock), unless the entry already left the module (bound, or hooked).
size_t PatchDelayImportsOf(const BYTE* base, const IMAGE_NT_HEADERS* nt, const char* dll, const char* function,
                           void* replacement, void** original, void* exported) {
    const IMAGE_DATA_DIRECTORY& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
    if (directory.VirtualAddress == 0 || !exported) return 0;
    const BYTE* end = base + nt->OptionalHeader.SizeOfImage;
    size_t replaced = 0;
    const auto* descriptor = reinterpret_cast<const IMAGE_DELAYLOAD_DESCRIPTOR*>(base + directory.VirtualAddress);
    for (; descriptor->DllNameRVA != 0; descriptor++) {
        if (!descriptor->Attributes.RvaBased || descriptor->ImportNameTableRVA == 0) continue;  // old VC6 layout
        if (_stricmp(reinterpret_cast<const char*>(base + descriptor->DllNameRVA), dll) != 0) continue;
        for (void** entry :
             EntriesNamed(base, descriptor->ImportNameTableRVA, descriptor->ImportAddressTableRVA, function)) {
            const BYTE* current = static_cast<const BYTE*>(*entry);
            bool stub = current >= base && current < end;
            if (Replace(entry, replacement, original, stub ? exported : nullptr)) replaced++;
        }
    }
    return replaced;
}

// Import table entries of one module for `dll!function`, regular and delay-loaded. Returns how many were replaced.
size_t PatchImportsOf(HMODULE module, const char* dll, const char* function, void* replacement, void** original,
                      void* exported) {
    const BYTE* base = reinterpret_cast<const BYTE*>(module);
    const IMAGE_NT_HEADERS* nt = HeadersOf(base);
    if (!nt) return 0;

    size_t replaced = PatchDelayImportsOf(base, nt, dll, function, replacement, original, exported);
    const IMAGE_DATA_DIRECTORY& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (directory.VirtualAddress == 0) return replaced;

    const auto* descriptor = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
    for (; descriptor->Name != 0; descriptor++) {
        if (_stricmp(reinterpret_cast<const char*>(base + descriptor->Name), dll) != 0) continue;
        if (descriptor->OriginalFirstThunk == 0) continue;  // bound imports: no names to match
        // The names live in the original first thunk; the loader writes the addresses into the first thunk.
        for (void** entry : EntriesNamed(base, descriptor->OriginalFirstThunk, descriptor->FirstThunk, function)) {
            if (Replace(entry, replacement, original)) replaced++;
        }
    }
    return replaced;
}

// The function as `dll` exports it, from System32 only (loaded if it is not yet). Null when there is none.
void* ExportOf(const char* dll, const char* function) {
    HMODULE module = GetModuleHandleA(dll);
    if (!module) module = LoadLibraryExA(dll, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    return module ? reinterpret_cast<void*>(GetProcAddress(module, function)) : nullptr;
}

// Import hooks kept for the modules loaded later (HookImportFromNowOn).
struct LaterHook {
    std::string dll;
    std::string function;
    void* replacement;
    void** original;
    void* exported;
};
std::vector<LaterHook> g_later;  // under g_lock
bool g_watching = false;         // under g_lock

// The loader's notification, declared here: winternl.h does not have it.
struct LoaderString {
    USHORT length;
    USHORT maximumLength;
    PWSTR buffer;
};
struct LoadedDll {
    ULONG flags;
    const LoaderString* fullName;
    const LoaderString* baseName;
    PVOID base;
    ULONG size;
};
constexpr ULONG kDllLoaded = 1;
using DllNotification = VOID(CALLBACK*)(ULONG reason, const LoadedDll* data, PVOID context);
using RegisterDllNotification = LONG(NTAPI*)(ULONG flags, DllNotification callback, PVOID context, PVOID* cookie);

// Runs under the loader lock: nothing here loads a library or asks the loader anything (the path comes with the
// notification, not from GetModuleFileName).
VOID CALLBACK OnDllLoaded(ULONG reason, const LoadedDll* data, PVOID) {
    if (reason != kDllLoaded || !data || !data->base) return;
    try {
        std::string path;
        if (data->fullName && data->fullName->buffer) {
            path = ToUtf8(std::wstring(data->fullName->buffer, data->fullName->length / sizeof(wchar_t)));
        }
        if (IsProtectedModule(path)) return;
        std::lock_guard<std::mutex> guard(g_lock);
        for (const LaterHook& hook : g_later) {
            PatchImportsOf(static_cast<HMODULE>(data->base), hook.dll.c_str(), hook.function.c_str(), hook.replacement,
                           hook.original, hook.exported);
        }
    } catch (...) {
    }
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

namespace {

size_t PatchEveryModule(const char* dll, const char* function, void* replacement, void** original, void* exported) {
    std::vector<HMODULE> modules(1024);
    DWORD needed = 0;
    if (!EnumProcessModules(GetCurrentProcess(), modules.data(),
                            static_cast<DWORD>(modules.size() * sizeof(HMODULE)), &needed)) {
        return 0;
    }
    modules.resize(std::min<size_t>(needed / sizeof(HMODULE), modules.size()));
    // The paths are read before taking g_lock: GetModuleFileName takes the loader lock, which OnDllLoaded holds when
    // it waits for g_lock.
    std::vector<HMODULE> allowed;
    for (HMODULE module : modules) {
        if (!IsProtectedModule(PathOfModule(module))) allowed.push_back(module);
    }

    std::lock_guard<std::mutex> guard(g_lock);
    size_t replaced = 0;
    for (HMODULE module : allowed) replaced += PatchImportsOf(module, dll, function, replacement, original, exported);
    return replaced;
}

}  // namespace

size_t HookImportEverywhere(const char* dll, const char* function, void* replacement, void** original) {
    return PatchEveryModule(dll, function, replacement, original, ExportOf(dll, function));
}

size_t HookImportFromNowOn(const char* dll, const char* function, void* replacement, void** original) {
    void* exported = ExportOf(dll, function);  // resolved here: the loader notification may not ask the loader
    {
        std::lock_guard<std::mutex> guard(g_lock);
        g_later.push_back({dll, function, replacement, original, exported});
        if (!g_watching) {
            auto registerNotification = reinterpret_cast<RegisterDllNotification>(
                GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "LdrRegisterDllNotification"));
            PVOID cookie = nullptr;  // never unregistered: the hooks live as long as the process
            g_watching = registerNotification && registerNotification(0, &OnDllLoaded, nullptr, &cookie) >= 0;
        }
    }
    return PatchEveryModule(dll, function, replacement, original, exported);
}

std::string ModuleOf(const void* address) {
    HMODULE module = nullptr;
    constexpr DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
    if (!GetModuleHandleExW(flags, reinterpret_cast<LPCWSTR>(address), &module)) return {};
    return PathOfModule(module);
}

}  // namespace ampify::win
