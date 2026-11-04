#pragma once

// Shared by the command files of `ampify`. Not part of the portable API.

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
#include "../core/text_util.hpp"
#include "catalog.hpp"
#include "commands.hpp"
#include "compile.hpp"
#include "element.hpp"
#include "kit.hpp"
#include "release.hpp"
#include "zip_writer.hpp"

namespace ampify::cli {

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

// Prints "ampify: <message>" on the error stream. Failure returns 1 and Usage returns 2.
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

// True for the answers that mean yes: y, yes (spaces and case are ignored).
inline bool IsYes(std::string answer) {
    std::string lowered;
    for (char c : answer) {
        if (!std::isspace(static_cast<unsigned char>(c))) lowered += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return lowered == "y" || lowered == "yes";
}

// Whole content of a file. False when it cannot be read.
inline bool ReadWholeFile(const std::filesystem::path& file, std::string& text) {
    std::optional<std::string> content = ampify::ReadWholeFile(file);
    if (!content) return false;
    text = std::move(*content);
    return true;
}

// Every regular file under `folder`, named `prefix/<path inside folder>` in the archive. Folders and files whose
// name starts with '.' are left out: they are work in progress or system files.
std::vector<ZipSource> FilesUnder(const std::filesystem::path& folder, const std::string& prefix);

// CompileSources (compile.hpp) with the real program runner: for `build` and `dev`.
CompileOutcome CompileElementSources(const std::filesystem::path& folder, std::string& message);

// Commands in commands.cpp.
int CmdHelp(Context& ctx);
int CmdVersion(Context& ctx);
int CmdPath(Context& ctx);
int CmdConfig(Context& ctx);
int CmdList(Context& ctx);
int CmdKind(Context& ctx, std::string_view kind);
int CmdTheme(Context& ctx);
int CmdThemeImport(Context& ctx);  // theme_import.cpp: takes up a Spicetify theme (color.ini as is)
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

// Asks a running kernel to apply a change at once, through reload.txt: "start <target>", "stop <target>", "rescan".
// A failure is only printed: the change still applies at the next start of Apple Music.
void RequestReload(Context& ctx, const std::vector<std::string>& lines);

// Commands in author.cpp: build, inspect, types.
int CmdBuild(Context& ctx);
int CmdInspect(Context& ctx);
int CmdTypes(Context& ctx);

// Command in publish.cpp: checks an element, then opens a pull request on the public catalog.
int CmdPublish(Context& ctx);

// The TypeScript declarations that `ampify types` writes (sdk_types.cpp).
std::string_view AmpifyDeclarations();

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

// ampify update --auto, which the background launcher runs while Apple Music is closed. Follows kit_update: installs
// a newer release on its own (auto), after a Yes (ask), or does nothing (off). At most one look a day
// (cache\auto-update.txt); a failed look or install is tried again on the launcher's next run.
int CmdAutoUpdate(Context& ctx);

// Commands in diagnose.cpp.
int CmdDoctor(Context& ctx);

// Commands in report.cpp: a zip of the state and the logs, anonymised, to attach to an issue.
int CmdReport(Context& ctx);

// Commands in test_runner.cpp.
int CmdTest(Context& ctx);

// The texts of `ampify new` still in a manifest, each with what to do about it. build and publish refuse an element
// that keeps one: a starter is meant to be changed before it is shared. Empty when none is left.
std::vector<std::string> StarterLeftovers(const ElementInfo& manifest);

// Commands in transfer.cpp: profile, export and import.
int CmdProfile(Context& ctx);
int CmdExport(Context& ctx);
int CmdImport(Context& ctx);

}  // namespace ampify::cli
