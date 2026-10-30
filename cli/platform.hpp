#pragma once

// The few things that differ between Windows and Linux: where the kit lives, how a program is started, and
// which tools unpack a zip. Everything else in the command line is portable.

#include <filesystem>
#include <string>
#include <vector>

namespace amk::platform {

// Called once at startup. Returns the arguments as UTF-8. On Windows it also switches the console to UTF-8.
std::vector<std::string> Initialize(int argc, char** argv);

// %USERPROFILE%\AppleMusicModKit on Windows, $HOME/AppleMusicModKit elsewhere.
std::filesystem::path DefaultKitDirectory();

// The folder that holds amk itself. The launcher and the updater are looked for next to it.
std::filesystem::path ExecutableDirectory();

// Runs a program with these arguments, without a shell, and waits for it. Returns its exit code, or -1 when
// it cannot start. When `stdoutFile` is not empty, standard output goes there.
int Run(const std::vector<std::string>& command, const std::filesystem::path& stdoutFile = {});

// True when the command runs and exits with 0. Its output is discarded. Used by `amk doctor` on tar, unzip and curl.
bool ToolWorks(const std::vector<std::string>& command);

// Starts a program and does not wait for it. Returns false when it cannot start.
bool StartDetached(const std::vector<std::string>& command);

// True while a process with this image name runs. Systems without the check always answer false.
bool IsProcessRunning(const std::string& imageName);

// A name for a temporary file that does not exist yet. The caller creates and removes it.
std::filesystem::path TemporaryFile(const std::string& stem);

// Entries of a .zip archive, as the archive stores them.
bool ListZip(const std::filesystem::path& archive, std::vector<std::string>& entries, std::string& error);

// Unpacks a .zip into `destination`, which must exist and be empty. Existing files are never overwritten.
bool ExtractZip(const std::filesystem::path& archive, const std::filesystem::path& destination, std::string& error);

// Downloads an https:// address with curl, which ships with Windows 10 and later. Redirects must stay https.
bool DownloadHttps(const std::string& url, const std::filesystem::path& destination, std::string& error);

}  // namespace amk::platform
