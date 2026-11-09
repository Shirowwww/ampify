// Linux and macOS side of platform.hpp. Used for development and for the portable tests.

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "platform.hpp"
#include "release_facts.hpp"

namespace ampify::platform {

namespace {

// Builds the argv array execvp expects. The vector keeps the strings alive for the call.
std::vector<char*> ArgvOf(const std::vector<std::string>& command, std::vector<std::string>& storage) {
    storage = command;
    std::vector<char*> argv;
    for (std::string& arg : storage) argv.push_back(arg.data());
    argv.push_back(nullptr);
    return argv;
}

}  // namespace

std::vector<std::string> Initialize(int argc, char** argv) {
    std::vector<std::string> arguments;
    for (int i = 0; i < argc; i++) arguments.emplace_back(argv[i]);
    return arguments;
}

std::filesystem::path DefaultKitDirectory() {
    const char* home = std::getenv("HOME");
    return std::filesystem::path(home ? home : ".") / kKitFolderName;
}

std::filesystem::path ExecutableDirectory() {
    std::error_code ec;
    std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::current_path() : exe.parent_path();
}

int Run(const std::vector<std::string>& command, const std::filesystem::path& stdoutFile) {
    if (command.empty()) return -1;
    std::fflush(stdout);
    std::fflush(stderr);

    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        if (!stdoutFile.empty()) {
            int fd = open(stdoutFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (fd < 0) _exit(127);
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }
        std::vector<std::string> storage;
        std::vector<char*> argv = ArgvOf(command, storage);
        execvp(argv[0], argv.data());
        _exit(127);  // the program could not be started
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    if (!WIFEXITED(status)) return -1;
    int code = WEXITSTATUS(status);
    return code == 127 ? -1 : code;
}

bool StartDetached(const std::vector<std::string>& command) {
    if (command.empty()) return false;
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        setsid();
        std::vector<std::string> storage;
        std::vector<char*> argv = ArgvOf(command, storage);
        execvp(argv[0], argv.data());
        _exit(127);
    }
    return true;
}

std::optional<std::filesystem::path> AppPackageFolder() {
    return std::nullopt;  // Apple Music for Windows is a Microsoft Store package: there is none here
}

std::optional<std::filesystem::path> AppPreferencesFile() {
    return std::nullopt;
}

std::vector<std::string> OpenFolderCommand(const std::filesystem::path& folder) {
    return {"xdg-open", folder.string()};
}

bool IsProcessRunning(const std::string& imageName) {
    return Run({"pgrep", "-x", imageName}) == 0;
}

std::vector<std::string> RunningProcessNames() {
    return {};  // the programs that crash Apple Music for Windows run on Windows only
}

std::optional<std::string> AppPackageVersion() {
    return std::nullopt;
}

std::optional<std::filesystem::path> ProgramFilesFolder() {
    return std::nullopt;
}

std::optional<std::filesystem::path> CrashDumpsFolder() {
    return std::nullopt;
}

bool CursorPosition(int&, int&) {
    return false;  // Apple Music for Windows does not run here
}

AutoStart AutoStartState() {
    return {};  // Apple Music for Windows does not run here
}

bool EnableAutoStart(const std::filesystem::path&, std::string& error) {
    error = "Apple Music for Windows does not run on this system";
    return false;
}

bool DisableAutoStart(std::string&) {
    return true;  // nothing was ever turned on
}

bool RestartAutoStart(const std::filesystem::path&, std::string& error) {
    error = "Apple Music for Windows does not run on this system";
    return false;
}

bool IsInstallDirectory(const std::filesystem::path&) {
    return false;
}

std::filesystem::path InstallDirectory() { return {}; }

TerminalLook PrepareTerminal() {
    const char* term = std::getenv("TERM");
    if (!isatty(STDOUT_FILENO) || !term || std::string(term) == "dumb") return {};
    const char* locale = std::getenv("LC_ALL");
    if (!locale || !*locale) locale = std::getenv("LANG");
    std::string name = locale ? locale : "";
    return {true, name.find("UTF-8") != std::string::npos || name.find("utf8") != std::string::npos};
}

bool SetInstalledVersion(const std::string&) {
    return false;
}

bool EndProcesses(const std::string&) {
    return true;
}

std::string UserLocale() {
    const char* lang = std::getenv("LANG");
    return lang && *lang ? lang : "en";
}

bool SystemUsesDarkMode() { return false; }

int LocalMinute() { return 0; }

void ShowNotice(const std::string&, const std::string&) {}

std::optional<bool> OfferNotice(const std::string&, const std::string&, unsigned long) { return std::nullopt; }

}  // namespace ampify::platform
