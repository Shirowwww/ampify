#pragma once

// Reads the logs of one test run and writes the report that goes back to the developer.
// Pure text work: no Windows API, so it is tested on Linux.

#include <optional>
#include <string>
#include <string_view>

namespace ampify {

// What the logs of one run say. Each field comes from a line the launcher or the kernel wrote.
struct RunFacts {
    std::string packageName;            // full name from the launcher log, empty when it was not found
    bool launched = false;              // "Apple Music started"
    bool versionTested = false;         // the version is in the tested list: the kernel is allowed
    bool suspendedLaunch = false;       // the launcher asked Windows for a suspended start
    std::optional<int> resumedThreads;  // "N thread(s) resumed" in the launcher log
    bool kernelInAppleMusic = false;    // the launcher or AppleMusic.log says the kernel is loaded
    bool kernelInLibraryAgent = false;  // the launcher or AMPLibraryAgent.log says the same
    bool healthy = false;               // AppleMusic.log: "Healthy after 20 s"
    int hungWindowWarnings = 0;         // AppleMusic.log: "Window not responding" lines
    bool safeModeActive = false;        // the launcher started without the kernel, in safe mode
    bool closed = false;                // "Apple Music closed": the launcher saw the app close
};

// Reads the three logs of a run. An empty string means the file was missing.
RunFacts ReadRunFacts(std::string_view launcherLog, std::string_view appleMusicLog, std::string_view libraryAgentLog);

// What the test kit saw around the run, besides the logs.
struct RunEnvironment {
    std::string date;           // "2026-10-30 13:50"
    std::string windows;        // "Windows 11 Pro 24H2 (build 26100)"
    bool installOk = false;
    std::string installDetail;  // where the kit was installed, or why it was not
    std::string traceStatus;    // "trace.etl, 12.4 MB", or why there is none
    std::string eventsStatus;   // Windows events about Apple Music, or why they are missing
};

enum class RunVerdict {
    Passed,       // install, launch and kernel in both processes, app healthy
    NeedsReview,  // Apple Music ran, but one of the checks did not hold
    NotRun,       // the run could not reach Apple Music
};

RunVerdict JudgeRun(const RunFacts& facts, const RunEnvironment& env);

// Replaces the user's name in the paths of a text, such as C:\Users\User\Ampify, with "user".
// The report and the logs are sent around, so the name is removed before the folder is zipped.
std::string HideUserName(std::string_view text, std::string_view userName);

// The report, in English, as plain text. It is meant to be read by a person who does not write code.
std::string BuildReport(const RunFacts& facts, const RunEnvironment& env);

}  // namespace ampify
