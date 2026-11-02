#include "background.hpp"

#include <string>
#include <vector>

#include "win_common.hpp"

namespace ampify::win {

namespace {

// After the background launcher released its mutex, the time left for its process to end, so its file can be replaced.
constexpr DWORD kExitGraceMs = 1000;

std::wstring RunCommand(const std::filesystem::path& launcher) {
    return L"\"" + launcher.wstring() + L"\" " + kBackgroundArgument;
}

// Sets the stop event when it exists. The launcher creates it just after its mutex, so a caller that came in between
// tries again on its next pass.
void AskToStop() {
    HANDLE stop = OpenEventW(EVENT_MODIFY_STATE, FALSE, kBackgroundStopEvent);
    if (!stop) return;
    SetEvent(stop);
    CloseHandle(stop);
}

}  // namespace

bool BackgroundRegistered() {
    DWORD type = 0;
    DWORD size = 0;
    LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, &type, nullptr, &size);
    return status == ERROR_SUCCESS;
}

bool RegisterBackground(const std::filesystem::path& launcher) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key,
                        nullptr) != ERROR_SUCCESS) {
        return false;
    }
    std::wstring command = RunCommand(launcher);
    LSTATUS status = RegSetValueExW(key, kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                                    static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

bool UnregisterBackground() {
    HKEY key = nullptr;
    LSTATUS opened = RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key);
    if (opened == ERROR_FILE_NOT_FOUND) return true;
    if (opened != ERROR_SUCCESS) return false;
    LSTATUS status = RegDeleteValueW(key, kRunValue);
    RegCloseKey(key);
    return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
}

bool BackgroundRunning() {
    HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, kBackgroundMutex);
    if (mutex) {
        CloseHandle(mutex);
        return true;
    }
    // A launcher started with administrator rights holds a mutex this process may not open: it runs all the same.
    return GetLastError() == ERROR_ACCESS_DENIED;
}

bool StartBackground(const std::filesystem::path& launcher) {
    if (BackgroundRunning()) return true;
    std::wstring command = RunCommand(launcher);
    std::wstring folder = launcher.parent_path().wstring();
    // Out of the caller's job when Windows allows it, so a terminal or installer that waits for its whole job (such
    // as Start-Process -Wait) does not wait for a launcher that runs until sign-out.
    const DWORD attempts[] = {DETACHED_PROCESS | CREATE_BREAKAWAY_FROM_JOB, DETACHED_PROCESS};
    for (DWORD flags : attempts) {
        std::vector<wchar_t> buffer(command.begin(), command.end());
        buffer.push_back(L'\0');
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (CreateProcessW(launcher.c_str(), buffer.data(), nullptr, nullptr, FALSE, flags, nullptr, folder.c_str(),
                           &startup, &process)) {
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            return true;
        }
    }
    return false;
}

bool StopBackground(DWORD timeoutMs) {
    if (!BackgroundRunning()) return true;
    // The launchers running now, kept to wait for the background one to really end once its mutex is gone.
    std::vector<HANDLE> launchers;
    for (DWORD id : ProcessIdsNamed(L"ampify_launcher.exe")) {
        if (launchers.size() == MAXIMUM_WAIT_OBJECTS) break;
        if (HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, id)) launchers.push_back(process);
    }
    ULONGLONG deadline = GetTickCount64() + timeoutMs;
    bool stopped = true;
    while (BackgroundRunning()) {
        if (GetTickCount64() >= deadline) {
            stopped = false;
            break;
        }
        AskToStop();
        Sleep(50);
    }
    // A foreground launcher (ampify launch) never ends here: the wait is short, and only a courtesy to the file copy.
    if (stopped && !launchers.empty()) {
        WaitForMultipleObjects(static_cast<DWORD>(launchers.size()), launchers.data(), TRUE, kExitGraceMs);
    }
    for (HANDLE process : launchers) CloseHandle(process);
    return stopped;
}

}  // namespace ampify::win
