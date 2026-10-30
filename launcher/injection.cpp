#include "injection.hpp"

#include <tlhelp32.h>
#include <wchar.h>

namespace amk::launcher {

namespace {

constexpr DWORD kRemoteLoadTimeoutMs = 10000;

std::string ErrorText(DWORD code) {
    return "erreur Windows " + std::to_string(code);
}

}  // namespace

HANDLE OpenProcessForInjection(DWORD processId) {
    return OpenProcess(kInjectionRights, FALSE, processId);
}

bool InjectKernel(HANDLE process, const std::filesystem::path& kernelDll, std::string& failure) {
    // kernel32 sits at the same address in every process, so the address found here is valid there too.
    FARPROC loadLibraryProc = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    if (!loadLibraryProc) {
        failure = "LoadLibraryW introuvable";
        return false;
    }

    std::wstring path = kernelDll.wstring();
    SIZE_T bytes = (path.size() + 1) * sizeof(wchar_t);
    void* remotePath = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remotePath) {
        failure = "mémoire distante refusée (" + ErrorText(GetLastError()) + ")";
        return false;
    }

    if (!WriteProcessMemory(process, remotePath, path.c_str(), bytes, nullptr)) {
        failure = "écriture distante refusée (" + ErrorText(GetLastError()) + ")";
        VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
        return false;
    }

    auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(reinterpret_cast<void*>(loadLibraryProc));
    HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLibrary, remotePath, 0, nullptr);
    if (!thread) {
        failure = "thread distant refusé (" + ErrorText(GetLastError()) + ")";
        VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
        return false;
    }

    bool loaded = false;
    if (WaitForSingleObject(thread, kRemoteLoadTimeoutMs) == WAIT_OBJECT_0) {
        DWORD exitCode = 0;  // LoadLibraryW's result: the module handle, zero on failure
        GetExitCodeThread(thread, &exitCode);
        loaded = exitCode != 0;
        if (!loaded) failure = "LoadLibraryW a échoué dans le processus";
        VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
    } else {
        // The remote thread may still be reading the path, so its memory is left in place.
        failure = "LoadLibraryW n'a pas répondu dans les 10 s";
    }

    CloseHandle(thread);
    return loaded;
}

int ResumeProcessThreads(DWORD processId) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;

    int resumed = 0;
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    if (Thread32First(snapshot, &entry)) {
        do {
            if (entry.th32OwnerProcessID != processId) continue;

            HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, entry.th32ThreadID);
            if (!thread) continue;

            // ResumeThread returns the suspend count it had before: above zero means it was suspended.
            DWORD previous = ResumeThread(thread);
            if (previous != static_cast<DWORD>(-1) && previous > 0) resumed++;
            CloseHandle(thread);
        } while (Thread32Next(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return resumed;
}

std::vector<DWORD> FindPackageProcesses(const wchar_t* imageName, const std::filesystem::path& directory) {
    std::vector<DWORD> found;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return found;

    std::wstring prefix = directory.wstring();
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, imageName) != 0) continue;

            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
            if (!process) continue;

            std::wstring image(32768, L'\0');
            DWORD length = static_cast<DWORD>(image.size());
            if (QueryFullProcessImageNameW(process, 0, image.data(), &length)) {
                image.resize(length);
                if (image.size() >= prefix.size() && _wcsnicmp(image.c_str(), prefix.c_str(), prefix.size()) == 0) {
                    found.push_back(entry.th32ProcessID);
                }
            }
            CloseHandle(process);
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return found;
}

}  // namespace amk::launcher
