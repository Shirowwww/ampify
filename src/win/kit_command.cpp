#include "kit_command.hpp"

#include <windows.h>

#include <filesystem>
#include <thread>

#include "win_common.hpp"

namespace ampify::win {

namespace {

constexpr DWORD kRunLimitMs = 5 * 60 * 1000;

struct RunResult {
    int code = -1;
    std::string out;
    std::string err;
};

// The folder of this DLL (ampify_kernel.dll), where Ampify puts ampify.exe.
std::filesystem::path KernelFolder() {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&KernelFolder), &module);
    wchar_t path[MAX_PATH * 2] = {};
    DWORD length = GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
    return std::filesystem::path(std::wstring(path, length)).parent_path();
}

std::string ReadAll(HANDLE pipe) {
    std::string text;
    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(pipe, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
        if (text.size() < 256 * 1024) text.append(buffer, read);  // the page needs a summary, not megabytes
    }
    return text;
}

RunResult RunAmpify(const std::vector<std::string>& arguments) {
    RunResult result;
    std::filesystem::path program = KernelFolder() / "ampify.exe";
    if (!std::filesystem::exists(program)) {
        result.err = "ampify.exe not found next to the kernel: " + ToUtf8(program.wstring());
        return result;
    }
    std::wstring commandLine = QuoteArgument(program.wstring());
    for (const std::string& argument : arguments) commandLine += L" " + QuoteArgument(FromUtf8(argument));

    SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
    HANDLE outRead = nullptr, outWrite = nullptr, errRead = nullptr, errWrite = nullptr;
    if (!CreatePipe(&outRead, &outWrite, &inherit, 0) || !CreatePipe(&errRead, &errWrite, &inherit, 0)) {
        result.err = "could not create the pipe";
        return result;
    }
    SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);
    HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, nullptr);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input;
    startup.hStdOutput = outWrite;
    startup.hStdError = errWrite;
    PROCESS_INFORMATION process{};
    std::wstring mutableLine = commandLine;
    BOOL started = CreateProcessW(program.c_str(), mutableLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                  program.parent_path().c_str(), &startup, &process);
    CloseHandle(outWrite);
    CloseHandle(errWrite);
    if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
    if (!started) {
        CloseHandle(outRead);
        CloseHandle(errRead);
        result.err = "ampify.exe did not start (error " + std::to_string(GetLastError()) + ")";
        return result;
    }

    std::string errText;
    std::thread errReader([&errText, errRead] { errText = ReadAll(errRead); });
    result.out = ReadAll(outRead);
    if (WaitForSingleObject(process.hProcess, kRunLimitMs) == WAIT_TIMEOUT) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 5000);
    } else {
        DWORD code = 1;
        GetExitCodeProcess(process.hProcess, &code);
        result.code = static_cast<int>(code);
    }
    errReader.join();
    result.err = std::move(errText);
    CloseHandle(outRead);
    CloseHandle(errRead);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return result;
}

}  // namespace

KitCommandRunner::KitCommandRunner(Journal& journal) : m_journal(journal) {}

void KitCommandRunner::RunKitCommand(const std::string& owner, const std::vector<std::string>& arguments, Done done) {
    std::uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> guard(m_owners->lock);
        generation = m_owners->generation[owner];
    }
    std::shared_ptr<Owners> owners = m_owners;
    Journal* journal = &m_journal;
    // Detached: a run outlives nothing but itself, and Forget stops its report from reaching a stopped element.
    std::thread([owners, owner, generation, arguments, done = std::move(done), journal] {
        RunResult result;
        try {
            result = RunAmpify(arguments);
        } catch (...) {
            result.err = "exception while running ampify";
        }
        journal->Write("Ampify: ampify " + arguments.front() + " finished (code " + std::to_string(result.code) + ")");
        std::lock_guard<std::mutex> guard(owners->lock);
        if (owners->generation[owner] != generation) return;
        try {
            done(result.code, result.out, result.err);
        } catch (...) {
        }
    }).detach();
}

void KitCommandRunner::Forget(const std::string& owner) {
    std::lock_guard<std::mutex> guard(m_owners->lock);
    m_owners->generation[owner]++;
}

}  // namespace ampify::win
