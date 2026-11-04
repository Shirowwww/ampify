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
    return std::filesystem::path(home ? home : ".") / "Ampify";
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

bool EndProcesses(const std::string&) {
    return true;
}

std::string UserLocale() {
    const char* lang = std::getenv("LANG");
    return lang && *lang ? lang : "en";
}

bool AskYesNo(const std::string&, const std::string&) {
    return false;
}

void ShowNotice(const std::string&, const std::string&) {}

}  // namespace ampify::platform
