#pragma once

// Process calls for the launcher: load the kernel DLL into a process, hold and resume its threads,
// find the processes of the package. Windows only.

#include <windows.h>

#include <filesystem>
#include <string>
#include <vector>

namespace amk::launcher {

// Rights the launcher asks for when it opens an Apple Music process to load the kernel into it.
inline constexpr DWORD kInjectionRights = PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                                          PROCESS_VM_WRITE | PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION |
                                          SYNCHRONIZE;

inline constexpr DWORD kDefaultRemoteLoadTimeoutMs = 10000;

// Opens a process with kInjectionRights. Null on failure: GetLastError says why.
HANDLE OpenProcessForInjection(DWORD processId);

// Loads `kernelDll` into `process` by running LoadLibraryW there, and waits at most `timeoutMs`.
// On failure, `failure` says why, in French. A load that is still pending at the timeout may finish
// later: the thread is not killed, and the caller should check the kernel's own journal.
bool InjectKernel(HANDLE process, const std::filesystem::path& kernelDll, std::string& failure,
                  DWORD timeoutMs = kDefaultRemoteLoadTimeoutMs);

// Suspends every thread of `processId`. Returns how many were suspended.
int SuspendProcessThreads(DWORD processId);

// Resumes every thread of `processId` that was suspended. Returns how many were resumed.
int ResumeProcessThreads(DWORD processId);

// Ids of the running processes that have this image name and run from under `directory`.
std::vector<DWORD> FindPackageProcesses(const wchar_t* imageName, const std::filesystem::path& directory);

}  // namespace amk::launcher
