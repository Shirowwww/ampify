#pragma once

// Function hooks without touching code: COM methods through their vtable, and imported functions through
// the import address table of each module. Nothing is decoded or moved, so nothing depends on the
// instructions Apple Music's functions start with. Not compiled on Linux.

#include <windows.h>

#include <cstddef>
#include <string>

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
size_t HookImportEverywhere(const char* dll, const char* function, void* replacement, void** original);

// HookImportEverywhere, and the same for every module loaded afterwards (a loader notification). Returns how many
// entries were replaced now. Never undone: meant for hooks that stay for the life of the process.
size_t HookImportFromNowOn(const char* dll, const char* function, void* replacement, void** original);

// Full path of the module that holds `address`. Empty when there is none.
std::string ModuleOf(const void* address);

}  // namespace ampify::win
