#pragma once

// The few things that differ between Windows and Linux: where the kit lives, how a program is started, and
// which tools unpack a zip. Everything else in the command line is portable.

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ampify::platform {

// Called once at startup. Returns the arguments as UTF-8. On Windows it also switches the console to UTF-8.
std::vector<std::string> Initialize(int argc, char** argv);

// %USERPROFILE%\Ampify on Windows, $HOME/Ampify elsewhere.
std::filesystem::path DefaultKitDirectory();

// The folder that holds ampify itself. The launcher and the updater are looked for next to it.
std::filesystem::path ExecutableDirectory();

// Runs a program with these arguments, without a shell, and waits for it. Returns its exit code, or -1 when
// it cannot start. When `stdoutFile` is not empty, standard output goes there.
int Run(const std::vector<std::string>& command, const std::filesystem::path& stdoutFile = {});

// True when the command runs and exits with 0. Its output is discarded. Used by `ampify doctor` on tar, unzip and curl.
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
// `maxSeconds` above 0 gives up after that many seconds (used by the background check for a new kit release).
bool DownloadHttps(const std::string& url, const std::filesystem::path& destination, std::string& error,
                   int maxSeconds = 0);

// The folder of the installed Apple Music package (under C:\Program Files\WindowsApps), read-only. Nothing when
// Apple Music is not installed, and always nothing outside Windows.
std::optional<std::filesystem::path> AppPackageFolder();

// The preferences file Apple Music writes (a binary plist, see knowledge/apple-music-internals.md):
// %LOCALAPPDATA%\Packages\<family>\LocalCache\Roaming\Apple Computer\Preferences\AMPLibraryAgent.exe.plist.
// Nothing outside Windows. The file itself may not exist yet.
std::optional<std::filesystem::path> AppPreferencesFile();

// The command that opens a folder in the file manager: explorer.exe on Windows, xdg-open elsewhere.
std::vector<std::string> OpenFolderCommand(const std::filesystem::path& folder);

// Ampify loading with Apple Music each time it starts: on Windows, the background launcher started at sign-in
// (src/win/background.hpp). Elsewhere it does not exist, and `supported` is false.
struct AutoStart {
    bool supported = false;
    bool registered = false;  // started at each sign-in
    bool running = false;     // running in this session
};
AutoStart AutoStartState();

// Turns it on: `launcher` starts now and at each sign-in. False with the reason in `error`.
bool EnableAutoStart(const std::filesystem::path& launcher, std::string& error);

// Turns it off: the background launcher stops and no longer starts at sign-in. False with the reason in `error`.
bool DisableAutoStart(std::string& error);

// Stops the background launcher and starts `launcher` in its place, the sign-in entry untouched (after an update).
bool RestartAutoStart(const std::filesystem::path& launcher, std::string& error);

// True when `directory` is the folder the installer puts Ampify in. Only that copy turns the automatic start on, so
// a build folder or an extracted zip never registers itself at sign-in.
bool IsInstallDirectory(const std::filesystem::path& directory);

}  // namespace ampify::platform
