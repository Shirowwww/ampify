#pragma once

// Process calls for the launcher: load the kernel DLL into a process, hold and resume its threads,
// find the processes of the package. Windows only.

#include <windows.h>

#include <filesystem>
#include <string>
#include <vector>

namespace ampify::launcher {

// Rights the launcher asks for when it opens an Apple Music process to load the kernel into it.
inline constexpr DWORD kInjectionRights = PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                                          PROCESS_VM_WRITE | PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION |
                                          SYNCHRONIZE;

inline constexpr DWORD kDefaultRemoteLoadTimeoutMs = 10000;

// Opens with kInjectionRights. Null on failure: GetLastError says why.
HANDLE OpenProcessForInjection(DWORD processId);

// Loads `kernelDll` into `process` by running LoadLibraryW there, and waits at most `timeoutMs`.
// On failure, `failure` says why. A load that is still pending at the timeout may finish
// later: the thread is not killed, and the caller should check the kernel's own log.
//
// When `pending` is given and the load is still running at the timeout, the remote thread's handle is stored there
// instead of being closed, so the caller can wait again with FinishRemoteLoad (for example after resuming the
// threads that hold the loader lock).
bool InjectKernel(HANDLE process, const std::filesystem::path& kernelDll, std::string& failure,
                  DWORD timeoutMs = kDefaultRemoteLoadTimeoutMs, HANDLE* pending = nullptr);

// Waits at most `timeoutMs` more for a load that InjectKernel left pending, then closes its handle.
// True when LoadLibraryW returned a module. On failure, `failure` says why.
bool FinishRemoteLoad(HANDLE thread, DWORD timeoutMs, std::string& failure);

// Returns how many threads were suspended.
int SuspendProcessThreads(DWORD processId);

// Returns how many suspended threads were resumed.
int ResumeProcessThreads(DWORD processId);

// Ids of the running processes that have this image name and run from under `directory`.
std::vector<DWORD> FindPackageProcesses(const wchar_t* imageName, const std::filesystem::path& directory);

// The same for each image name, in their order, from one list of the processes: a list costs more than the rest.
std::vector<std::vector<DWORD>> FindPackageProcesses(const std::vector<const wchar_t*>& imageNames,
                                                     const std::filesystem::path& directory);

}  // namespace ampify::launcher
