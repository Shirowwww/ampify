#pragma once

// The offline bench (docs/development/offline-bench.md): the real theme engine on a tree the inspector recorded, without
// Apple Music. `ampify theme replay`, `ampify theme diff` and `ampify theme scrub`.

#include <filesystem>
#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../core/theme/tree.hpp"

namespace ampify::theme {

struct ReplayRequest {
    std::filesystem::path theme;  // a theme folder; empty for no theme (snippets alone, or Apple's look)
    std::filesystem::path tree;   // an inspector.json
    std::filesystem::path out;    // where the report, the wireframe and the scratch kit go
    std::optional<std::filesystem::path> settings;  // a settings.txt to start from (theme.active is replaced)
    std::vector<std::filesystem::path> snippets;    // snippet folders, enabled
    std::vector<std::pair<std::string, std::string>> values;  // `--set key=value`, after the settings file
    std::string scheme;
    int maxPasses = 8;
    int minute = 12 * 60;  // the local time for theme.schedule, in minutes from midnight
    std::optional<ViewSize> window;
    bool asRecorded = false;  // keep values recorded under a theme instead of giving the originals back
    // The artwork's colours, as the theme service gives them (art-1..., page-art-1...: ArtworkVariablesOf). Empty: no
    // artwork, as before the first song, and var(--art-1, fallback) takes its fallback.
    std::map<std::string, std::string> artwork;
    // `--then`: once the theme has settled, a second state replayed on the SAME tree and session, as a switch in the
    // live app: this theme folder (empty for none) with `thenSnippets` on and the other snippets off. The first theme
    // comes off through the engine's restore path; what it leaves behind is what a fresh tree replaying the second
    // state does not show.
    std::optional<std::filesystem::path> then;
    std::vector<std::filesystem::path> thenSnippets;
};

// The variables of an artwork whose colours are given (`--artwork`): art-1 the most common colour, art-2 the vibrant
// one (the first when not given), art-3 the dark one under white text (the second, darkened as the live app darkens
// it), art-text and the -hex forms, as ArtworkVariables makes them from an image; the same again as page-art-*, the
// album or playlist on screen. `text` is the colours, "#RRGGBB" or "RRGGBB", separated by commas, spaces or lines.
// Nothing, with `problem` set, for anything else or more than three colours.
std::optional<std::map<std::string, std::string>> ArtworkVariablesOf(std::string_view text, std::string* problem);

struct ReplayOutcome {
    std::string report;     // report.json
    std::string wireframe;  // wireframe.svg
    int passes = 0;
    bool settled = false;   // the last pass wrote nothing (with `then`: of both states)
    // With `then`: the second state replayed on a fresh tree, and how many values the first state left behind.
    std::string freshReport;
    std::string freshWireframe;
    int leftovers = 0;
};

// Runs the theme service's own pass (ThemeWorker::Tick, then ThemeSession::Tick) on the recorded tree until a pass writes
// nothing or maxPasses is reached. The theme and the snippets are copied into a scratch kit under `out`, the way the
// worker reads them in %USERPROFILE%\Ampify. Nothing, with `problem` set, when the tree or the theme cannot be read.
std::optional<ReplayOutcome> Replay(const ReplayRequest& request, std::string* problem);

class ReplayBackend;
// What `tree` shows that `fresh`, the same dump, does not, among what either one's replay changed: values, positions,
// resources, the dictionary, decors and values under the pointer; a JSON list, as report.json's `leftovers`.
std::string LeftoversJson(ReplayBackend& tree, ReplayBackend& fresh);

// What changed from one report.json to another: values, refusals, matches per rule, unmatched selectors, values under
// the pointer. `same` tells when nothing did.
std::optional<std::string> DiffReports(const std::string& before, const std::string& after, bool& same,
                                       std::string* problem);

// `ampify theme replay ...`, `ampify theme diff ...`, `ampify theme scrub ...`: `args` starts at "replay", "diff" or
// "scrub". 0 when done (replay: settled; diff: no difference), 1 for a failure, a replay that does not settle or a
// difference, 2 for a wrong use.
int RunReplayCommand(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

}  // namespace ampify::theme
