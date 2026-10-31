#include "run_report.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "apple_music.hpp"
#include "kit_layout.hpp"
#include "text_util.hpp"

namespace ampify {

namespace {

bool Contains(std::string_view text, std::string_view part) {
    return text.find(part) != std::string_view::npos;
}

// Calls `visit` on each line, without its line break.
template <typename Visit>
void ForEachLine(std::string_view text, Visit visit) {
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        visit(line);
        start = end + 1;
    }
}

// The number written just before " thread(s) resumed" on a line, or nothing.
std::optional<int> ResumedCount(std::string_view line) {
    size_t cut = line.find(" thread(s) resumed");
    if (cut == std::string_view::npos) return std::nullopt;

    size_t first = cut;
    while (first > 0 && line[first - 1] >= '0' && line[first - 1] <= '9') first--;
    if (first == cut) return std::nullopt;
    return std::stoi(std::string(line.substr(first, cut - first)));
}

// One line of the checklist. `status` is "OK", "FAIL" (failed), "?" (to look at) or "info" (for information only).
struct Row {
    std::string status;
    std::string label;
    std::string detail;
};

std::string FormatRow(const Row& row) {
    std::string marker = "[" + row.status + "]";
    marker.resize(std::max<size_t>(marker.size(), 7), ' ');
    return marker + row.label + ": " + row.detail + "\n";
}

}  // namespace

RunFacts ReadRunFacts(std::string_view launcherLog, std::string_view appleMusicLog, std::string_view libraryAgentLog) {
    RunFacts facts;

    ForEachLine(launcherLog, [&](std::string_view line) {
        constexpr std::string_view kPackagePrefix = "Package found: ";
        if (line.find(kPackagePrefix) != std::string_view::npos) {
            std::string_view rest = line.substr(line.find(kPackagePrefix) + kPackagePrefix.size());
            facts.packageName = std::string(rest.substr(0, rest.find(' ')));
        }
        if (Contains(line, "Apple Music started")) facts.launched = true;
        if (Contains(line, "tested: kernel injected")) facts.versionTested = true;
        // The M3 launcher holds the first process ("N thread(s) paused"); older builds asked Windows instead.
        if (Contains(line, "thread(s) paused") || Contains(line, "Suspended launch requested")) {
            facts.suspendedLaunch = true;
        }
        if (std::optional<int> resumed = ResumedCount(line)) facts.resumedThreads = resumed;
        // "AppleMusic.exe (4120): kernel loaded during the pause", "... after the resume", or the older form without the id.
        if (Contains(line, "AppleMusic.exe") && Contains(line, ": kernel loaded")) facts.kernelInAppleMusic = true;
        if (Contains(line, "AMPLibraryAgent.exe (") && Contains(line, ": kernel loaded")) {
            facts.kernelInLibraryAgent = true;
        }
        if (Contains(line, "safe mode active")) facts.safeModeActive = true;
        if (Contains(line, "Apple Music closed")) facts.closed = true;
    });

    ForEachLine(appleMusicLog, [&](std::string_view line) {
        if (Contains(line, "Kernel loaded in AppleMusic.exe")) facts.kernelInAppleMusic = true;
        if (Contains(line, "tested") && !Contains(line, "not tested")) facts.versionTested = true;
        if (Contains(line, "Healthy after 20 s")) facts.healthy = true;
        if (Contains(line, "Window not responding")) facts.hungWindowWarnings++;
    });

    ForEachLine(libraryAgentLog, [&](std::string_view line) {
        if (Contains(line, "Kernel loaded in AMPLibraryAgent.exe")) facts.kernelInLibraryAgent = true;
    });

    // A package name that is not Apple Music's is not a run of Apple Music.
    if (AppleMusicVersion(facts.packageName).empty()) facts.packageName.clear();
    return facts;
}

RunVerdict JudgeRun(const RunFacts& facts, const RunEnvironment& env) {
    if (!env.installOk || facts.packageName.empty() || !facts.launched) return RunVerdict::NotRun;

    // A suspended start that resumed no thread means nothing was suspended: the report asks to look at it.
    // A start that was not suspended at all is allowed, and reported as a note.
    bool suspendedOk = !facts.suspendedLaunch || facts.resumedThreads.value_or(0) > 0;
    bool clean = facts.versionTested && facts.kernelInAppleMusic && facts.kernelInLibraryAgent && facts.healthy &&
                 facts.hungWindowWarnings == 0 && !facts.safeModeActive && suspendedOk;
    return clean ? RunVerdict::Passed : RunVerdict::NeedsReview;
}

std::string BuildReport(const RunFacts& facts, const RunEnvironment& env) {
    std::string version = AppleMusicVersion(facts.packageName);
    RunVerdict verdict = JudgeRun(facts, env);

    std::string text;
    text += "Ampify test report (M5)\n";
    text += "Date: " + env.date + "\n";
    text += "Windows: " + env.windows + "\n";
    text += "Apple Music: " + (version.empty() ? std::string("not found") : version) + "\n";
    text += "Ampify: " + std::string(kKitVersion) + "\n\n";

    switch (verdict) {
        case RunVerdict::Passed:
            text += "RESULT: PASSED\n";
            break;
        case RunVerdict::NeedsReview:
            text += "RESULT: NEEDS REVIEW (see the [FAIL] and [?] lines)\n";
            break;
        case RunVerdict::NotRun:
            text += "RESULT: NOT RUN (Apple Music was not started by Ampify)\n";
            break;
    }
    text += "\nChecks\n";

    std::vector<Row> rows;
    rows.push_back({env.installOk ? "OK" : "FAIL", "Ampify installed", env.installDetail});
    rows.push_back({version.empty() ? "FAIL" : "OK", "Apple Music found",
                    version.empty() ? std::string("not found") : "version " + version});
    rows.push_back({facts.launched ? "OK" : "FAIL", "Apple Music started by Ampify", facts.launched ? "yes" : "no"});
    rows.push_back({facts.versionTested ? "OK" : "FAIL", "Tested version, kernel allowed",
                    facts.versionTested ? "yes" : "no, the kernel stays inactive"});

    if (!facts.launched) {
        rows.push_back({"info", "Suspended launch", "not applicable, Apple Music was not started"});
    } else if (!facts.suspendedLaunch) {
        rows.push_back({"?", "Suspended launch", "no, the kernel is injected after the start"});
    } else if (facts.resumedThreads.value_or(0) > 0) {
        rows.push_back({"OK", "Suspended launch", std::to_string(*facts.resumedThreads) + " thread(s) resumed"});
    } else {
        rows.push_back({"?", "Suspended launch", "0 threads resumed: nothing was suspended"});
    }

    rows.push_back({facts.kernelInAppleMusic ? "OK" : "FAIL", "Kernel in AppleMusic.exe",
                    facts.kernelInAppleMusic ? "loaded" : "not seen"});
    rows.push_back({facts.kernelInLibraryAgent ? "OK" : "FAIL", "Kernel in AMPLibraryAgent.exe",
                    facts.kernelInLibraryAgent ? "loaded" : "not seen (play a track to start it)"});
    rows.push_back({facts.healthy ? "OK" : "FAIL", "Healthy after 20 s", facts.healthy ? "yes" : "no"});
    rows.push_back({facts.hungWindowWarnings == 0 ? "OK" : "?", "Window not responding",
                    facts.hungWindowWarnings == 0 ? "no" : std::to_string(facts.hungWindowWarnings) + " times"});
    rows.push_back({facts.safeModeActive ? "?" : "OK", "Safe mode",
                    facts.safeModeActive ? "on: the kernel was turned off" : "off"});
    rows.push_back({"info", "ETW trace", env.traceStatus});
    rows.push_back({"info", "Windows events", env.eventsStatus});

    for (const Row& row : rows) text += FormatRow(row);

    text += "\nPaths on your PC are anonymous: your user name is replaced by \"user\".\n";
    text += "The trace.etl file, if there is one, is not anonymous.\n";
    return text;
}

std::string HideUserName(std::string_view text, std::string_view userName) {
    std::string lowered = ToLowerAscii(text);
    std::string name = ToLowerAscii(userName);
    if (name.empty()) return std::string(text);

    auto isSeparator = [](char c) { return c == '\\' || c == '/'; };
    std::string result;
    size_t copied = 0;
    // A path is "<separator>users<separator><name>", then a separator, a line break, a quote, a space or the end.
    for (size_t i = 0; i + 7 + name.size() <= lowered.size(); i++) {
        if (!isSeparator(lowered[i]) || lowered.compare(i + 1, 5, "users") != 0 || !isSeparator(lowered[i + 6])) continue;

        size_t start = i + 7;
        if (lowered.compare(start, name.size(), name) != 0) continue;

        size_t end = start + name.size();
        bool ends = end == lowered.size() || isSeparator(lowered[end]) || lowered[end] == '\r' ||
                    lowered[end] == '\n' || lowered[end] == '"' || lowered[end] == ' ';
        if (!ends) continue;

        result += std::string(text.substr(copied, start - copied)) + "user";
        copied = end;
        i = end - 1;
    }
    result += std::string(text.substr(copied));
    return result;
}

}  // namespace ampify
