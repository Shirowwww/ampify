#pragma once

// Shared by the command files of `amk`. Not part of the portable API.

#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <istream>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <optional>

#include "../core/settings.hpp"
#include "catalog.hpp"
#include "commands.hpp"
#include "element.hpp"
#include "kit.hpp"
#include "release.hpp"
#include "zip_writer.hpp"

namespace amk::cli {

// Everything a command needs: the options, the kit, and the three streams.
struct Context {
    const Options& options;
    KitLayout kit;
    std::istream& in;
    std::ostream& out;
    std::ostream& err;
};

// Pads a table cell to `width` characters, not bytes, so accented names line up.
inline std::string Pad(std::string_view text, size_t width) {
    std::string cell(text);
    size_t characters = CharacterCount(text);
    if (characters < width) cell.append(width - characters, ' ');
    return cell;
}

// Command-line text is UTF-8. Converting it here keeps accented folder names working on Windows.
std::filesystem::path PathArgument(std::string_view text);
std::string Utf8(const std::filesystem::path& path);

// Prints "amk : <message>" on the error stream. Failure returns 1 and Usage returns 2.
int Failure(Context& ctx, const std::string& message);
int Usage(Context& ctx, const std::string& message);

bool LoadSettings(const Context& ctx, Settings& settings, std::string& error);
bool SaveSettings(const Context& ctx, const Settings& settings, std::string& error);

// The time as HH:MM:SS, for the lines that `watch` and `dev` print.
inline std::string ClockText() {
    std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char buffer[16];
    std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &local);
    return buffer;
}

// True for the answers that mean yes: o, oui, y, yes (spaces and case are ignored).
inline bool IsYes(std::string answer) {
    std::string lowered;
    for (char c : answer) {
        if (!std::isspace(static_cast<unsigned char>(c))) lowered += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return lowered == "o" || lowered == "oui" || lowered == "y" || lowered == "yes";
}

// Whole content of a file. False when it cannot be read.
inline bool ReadWholeFile(const std::filesystem::path& file, std::string& text) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    std::stringstream buffer;
    buffer << in.rdbuf();
    text = buffer.str();
    return true;
}

// Every regular file under `folder`, named `prefix/<path inside folder>` in the archive. Folders and files whose
// name starts with '.' are left out: they are work in progress or system files.
std::vector<ZipSource> FilesUnder(const std::filesystem::path& folder, const std::string& prefix);

// Commands in commands.cpp.
int CmdHelp(Context& ctx);
int CmdVersion(Context& ctx);
int CmdPath(Context& ctx);
int CmdConfig(Context& ctx);
int CmdList(Context& ctx);
int CmdKind(Context& ctx, std::string_view kind);
int CmdTheme(Context& ctx);
int CmdValidate(Context& ctx);
int CmdNew(Context& ctx);

// Commands in marketplace.cpp.
int CmdMarket(Context& ctx);
int CmdInstall(Context& ctx);
int CmdRemove(Context& ctx);
int CmdUpdate(Context& ctx);
int CmdInstallKit(Context& ctx);

// Downloads an https address with curl, or copies a file:// address. The caller has checked the address policy.
bool FetchTo(const std::string& url, const std::filesystem::path& destination, std::string& error, int maxSeconds = 0);

// Reads the catalogue given by --catalog, by catalog.url in the settings, or by default. A remote catalogue that
// cannot be downloaded falls back to the last copy that worked. Prints the problem and returns false on failure.
bool LoadCatalog(Context& ctx, Catalog& catalog, bool& local);

// The GitHub address of the latest kit release, or of the release tagged v<version>.
std::string DefaultReleaseUrl(std::string_view version);

// Reads the release JSON at `source` (https:// or file://) and checks its version.
std::optional<ReleaseInfo> FetchRelease(Context& ctx, const std::string& source, std::string& error, int maxSeconds = 0);

// Downloads, checks and (with --apply) installs a kit release. `wanted` empty means the latest release, and only a
// newer one; otherwise the release at `source` must be that version, older or newer (install-kit).
int InstallKitRelease(Context& ctx, const std::string& source, const std::string& wanted);

// Commands in activation.cpp.
int CmdApply(Context& ctx);
int CmdLaunch(Context& ctx);
int CmdRestore(Context& ctx);
int CmdBackup(Context& ctx);
int CmdWatch(Context& ctx);

// Writes active.txt and reload.txt from the current settings and kit. Prints what loads and what does not.
// Returns 0, or 1 when an enabled element cannot load. Used by apply, launch, watch and dev.
int ApplyNow(Context& ctx, const std::vector<std::string>& restarts = {});

// Asks a running kernel to apply a change at once, through reload.txt: "start <cible>", "stop <cible>", "rescan".
// A failure is only printed: the change still applies at the next start of Apple Music.
void RequestReload(Context& ctx, const std::vector<std::string>& lines);

// Commands in author.cpp: build, inspect, types.
int CmdBuild(Context& ctx);
int CmdInspect(Context& ctx);
int CmdTypes(Context& ctx);

// The TypeScript declarations that `amk types` writes (sdk_types.cpp).
std::string_view AmkDeclarations();

// Commands in develop.cpp: dev and logs.
int CmdDev(Context& ctx);
int CmdLogs(Context& ctx);

// The *.log files of the journal folder, sorted by name. Empty when the kernel has not run yet (develop.cpp).
std::vector<std::filesystem::path> JournalFiles(const KitLayout& kit);

// Commands in maintenance.cpp: devtools, versions, and the notice about a new kit release.
int CmdDevtools(Context& ctx);
int CmdVersions(Context& ctx);
int CmdInspectTree(Context& ctx);
int CmdConfigDir(Context& ctx);

// With check_kit_update = 1: prints one line on the error stream when a newer kit release exists. The answer is
// kept in cache\kit-update.txt and asked again at most once a day. Never fails the command.
void NotifyKitUpdate(Context& ctx);

// The value of a path setting (app_path, prefs_path), or `fallback` when it is empty.
std::optional<std::filesystem::path> PathSetting(const Context& ctx, const std::string& key,
                                                 std::optional<std::filesystem::path> fallback);

// Commands in diagnose.cpp.
int CmdDoctor(Context& ctx);

// Commands in test_runner.cpp.
int CmdTest(Context& ctx);

// Commands in transfer.cpp: profile, export and import.
int CmdProfile(Context& ctx);
int CmdExport(Context& ctx);
int CmdImport(Context& ctx);

}  // namespace amk::cli
