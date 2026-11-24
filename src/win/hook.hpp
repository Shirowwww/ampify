#pragma once

// Function hooks without touching code: COM methods through their vtable, and imported functions through
// the import address table of each module. Nothing is decoded or moved, so nothing depends on the
// instructions Apple Music's functions start with. Not compiled on Linux.

#include <windows.h>

#include <cstddef>
#include <string>
#include <vector>

namespace ampify::win {

// The vtable entry of method `slot` for `object`'s class. Every object of that class shares the entry.
// Interface pointers of the same object can have their own vtable: take the entry from the pointer you hold.
void** VtableEntry(void* object, size_t slot);

// Replaces a vtable entry, for every object that uses it in this process. `original` receives the method
// it replaced. Refused when that method belongs to a protected module (see IsProtectedModule), with the
// reason in `why`.
bool HookVtableEntry(void** entry, void* replacement, void** original, std::string& why);

// Replaces the address of `function` imported from `dll` in the import table of every loaded module that
// imports it, delay-loaded imports included (those only when `dll` is a System32 DLL). `original` receives the
// address that was replaced first. Returns how many import entries were replaced. Protected modules are skipped.
// An entry holds one replacement: a second hook on a function already hooked is refused there, so a feature that
// needs the same function goes through the existing hook.
size_t HookImportEverywhere(const char* dll, const char* function, void* replacement, void** original);

// HookImportEverywhere, and the same for every module loaded afterwards (a loader notification). Returns how many
// entries were replaced now. Never undone: meant for hooks that stay for the life of the process.
size_t HookImportFromNowOn(const char* dll, const char* function, void* replacement, void** original);

// The modules of `listed` still loaded, each held by one more reference while this object lives, so that none is
// freed while its import table is read: Windows' audio stack frees codecs while the kernel starts. A module freed
// after it was listed is left out. Taking and dropping references asks the loader: never under the hook lock.
class HeldModules {
public:
    explicit HeldModules(const std::vector<HMODULE>& listed);
    ~HeldModules();
    HeldModules(const HeldModules&) = delete;
    HeldModules& operator=(const HeldModules&) = delete;

    const std::vector<HMODULE>& modules() const { return m_modules; }

private:
    std::vector<HMODULE> m_modules;
};

// Full path of the module that holds `address`. Empty when there is none. Not under the hook lock: it asks the loader.
std::string ModuleOf(const void* address);

}  // namespace ampify::win
