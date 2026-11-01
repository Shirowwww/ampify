// `ampify doctor`: one report on the state of the kit, for a person who reports a problem and for the tests.

#include <algorithm>
#include <fstream>
#include <system_error>
#include <vector>

#include "../core/safe_mode.hpp"
#include "../core/version_gate.hpp"
#include "../core/version_list.hpp"
#include "element.hpp"
#include "internal.hpp"
#include "platform.hpp"

namespace ampify::cli {

namespace {

constexpr size_t kJournalTail = 3;

// The archive and download tools that `ampify install` and `ampify update` need.
#ifdef _WIN32
const std::vector<std::string> kArchiveTool = {"tar.exe", "--version"};
const std::vector<std::string> kDownloadTool = {"curl.exe", "--version"};
#else
const std::vector<std::string> kArchiveTool = {"unzip", "-v"};
const std::vector<std::string> kDownloadTool = {"curl", "--version"};
#endif

// The last lines of a log file, without the carriage returns that Windows leaves.
std::vector<std::string> LastLines(const std::filesystem::path& file, size_t count) {
    std::ifstream in(file, std::ios::binary);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    size_t begin = lines.size() > count ? lines.size() - count : 0;
    return std::vector<std::string>(lines.begin() + static_cast<std::ptrdiff_t>(begin), lines.end());
}

// Folders of the kit that have a name starting with '.': leftovers of an install or a dev reload that stopped midway.
std::vector<std::filesystem::path> Leftovers(const KitLayout& kit) {
    std::vector<std::filesystem::path> found;
    for (std::string_view kind : {"extension", "theme", "app", "snippet"}) {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(kit.Folder(kind), ec)) {
            std::string name = Utf8(entry.path().filename());
            if (!name.empty() && name[0] == '.') found.push_back(entry.path());
        }
    }
    return found;
}

}  // namespace

int CmdDoctor(Context& ctx) {
    if (ctx.options.positional.size() != 1) return Usage(ctx, "doctor takes no argument");

    int errors = 0;
    int warnings = 0;
    auto ok = [&](const std::string& text) { ctx.out << "[OK]        " << text << "\n"; };
    auto warn = [&](const std::string& text) {
        ctx.out << "[WARNING]   " << text << "\n";
        warnings++;
    };
    auto fail = [&](const std::string& text) {
        ctx.out << "[ERROR]     " << text << "\n";
        errors++;
    };

    std::error_code ec;
    if (!std::filesystem::is_directory(ctx.kit.Root(), ec)) {
        warn("Ampify folder missing: " + Utf8(ctx.kit.Root()) + " (ampify new, ampify install or ampify config will create it)");
    } else {
        ok("Ampify folder: " + Utf8(ctx.kit.Root()));
    }

    Settings settings;
    std::string error;
    if (!LoadSettings(ctx, settings, error)) {
        fail("unreadable settings: " + error);
    } else {
        ok("settings read (" + std::to_string(settings.Values().size()) + " key(s))");
    }

    std::vector<InstalledElement> installed = ListInstalled(ctx.kit);
    size_t valid = 0;
    for (const InstalledElement& element : installed) {
        if (element.Valid()) {
            valid++;
            continue;
        }
        std::string reason = element.errors.empty() ? "unreadable manifest" : element.errors.front();
        warn("invalid element " + element.kind + " " + element.id + ": " + reason);
    }
    ok(std::to_string(valid) + " valid element(s) out of " + std::to_string(installed.size()));

    for (const std::filesystem::path& leftover : Leftovers(ctx.kit)) {
        warn("leftover of an interrupted install or development: " + Utf8(leftover) + " (can be deleted)");
    }

    // active.txt must describe the current settings, or the kernel loads what was there before the last change.
    ActiveReport report = ComputeActive(ctx.kit, settings);
    for (const std::string& problem : report.problems) warn("enabled but not loaded: " + problem);
    std::string written;
    if (!ReadWholeFile(ctx.kit.Active(), written)) {
        warn("active.txt missing: run ampify apply");
    } else if (written != FormatActive(report)) {
        warn("active.txt no longer matches the settings: run ampify apply");
    } else {
        ok("active.txt up to date");
    }

    ampify::SafeMode safeMode(ctx.kit.SafeModeState().string());
    if (safeMode.IsActive()) {
        warn("safe mode on: the kernel is turned off after two starts without an answer");
    } else {
        ok("safe mode: off");
    }

    std::vector<std::filesystem::path> journals = JournalFiles(ctx.kit);
    if (journals.empty()) {
        ok("logs: no file, the kernel has not run yet");
    } else {
        ok("logs: " + std::to_string(journals.size()) + " file(s)");
        for (const std::filesystem::path& journal : journals) {
            ctx.out << "            " << Utf8(journal.filename()) << "\n";
            for (const std::string& line : LastLines(journal, kJournalTail)) ctx.out << "              " << line << "\n";
        }
    }

    std::vector<std::string> extra = LoadVersionList(ctx.kit.Root() / std::string(kVersionListFile));
    std::string versions;
    for (std::string_view version : KnownAppVersionsWith(extra)) {
        versions += (versions.empty() ? "" : ", ") + std::string(version);
    }
    ok("tested Apple Music versions: " + versions + (extra.empty() ? "" : " (with versions.txt)"));
    ok(std::string("Apple Music ") +
       (platform::IsProcessRunning("AppleMusic.exe") ? "is open" : "is closed") +
       " (process check)");

    if (platform::ToolWorks(kArchiveTool)) {
        ok("archive tool available: " + kArchiveTool.front());
    } else {
        fail("archive tool not found: " + kArchiveTool.front() + " (install and update will not work)");
    }
    if (platform::ToolWorks(kDownloadTool)) {
        ok("download tool available: " + kDownloadTool.front());
    } else {
        fail("download tool not found: " + kDownloadTool.front() + " (the remote catalog will not work)");
    }

#ifdef _WIN32
    if (!std::filesystem::is_regular_file(platform::ExecutableDirectory() / "ampify_launcher.exe", ec)) {
        warn("ampify_launcher.exe missing next to ampify: ampify launch cannot start Apple Music with the kernel");
    } else {
        ok("ampify_launcher.exe present");
    }
#endif

    ctx.out << "\nSummary: " << errors << " error(s), " << warnings << " warning(s).\n";
    return errors > 0 ? 1 : 0;
}

}  // namespace ampify::cli
