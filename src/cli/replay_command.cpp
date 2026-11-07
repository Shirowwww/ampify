#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../core/json.hpp"
#include "../core/manifest.hpp"
#include "../core/settings_catalog.hpp"
#include "../core/theme/accent.hpp"
#include "../core/theme/theme_settings.hpp"
#include "dump_scrub.hpp"
#include "replay_util.hpp"
#include "theme_replay.hpp"

namespace ampify::theme {

using namespace replay;

namespace {

constexpr int kOk = 0;
constexpr int kFailed = 1;
constexpr int kWrongUse = 2;

constexpr const char* kUsage =
    "usage: ampify theme replay <theme-folder|none> --tree <inspector.json> --out <folder>\n"
    "           [--settings <settings.txt>] [--set key=value]... [--snippet <folder>]... [--scheme name]\n"
    "           [--passes N] [--window WxH] [--minute N] [--as-recorded] [--compare <theme-folder|none>]\n"
    "           [--artwork <#RRGGBB>[,<#RRGGBB>[,<#RRGGBB>]] | <palette-file>]\n"
    "           [--then <theme-folder|none> [--then-snippet <folder>]...]\n"
    "       ampify theme replay --tree <inspector.json> --scrub <out.json>\n"
    "       ampify theme diff <report.json|folder> <report.json|folder> [--out <diff.json>]\n"
    "       ampify theme scrub <inspector.json> <out.json>\n";

struct Arguments {
    std::vector<std::string> positional;
    std::map<std::string, std::vector<std::string>> options;
    std::set<std::string> flags;
    std::string problem;
};

Arguments Parse(const std::vector<std::string>& args) {
    static const std::set<std::string> kValued = {"--tree",   "--out",    "--settings", "--set",     "--snippet",
                                                  "--scheme", "--passes", "--window",   "--minute",  "--compare",
                                                  "--scrub",  "--artwork", "--then",    "--then-snippet"};
    static const std::set<std::string> kFlags = {"--as-recorded"};
    Arguments parsed;
    for (size_t i = 1; i < args.size(); i++) {
        std::string arg = args[i];
        std::string value;
        bool inline_ = false;
        if (size_t equals = arg.find('='); arg.starts_with("--") && equals != std::string::npos) {
            value = arg.substr(equals + 1);
            arg = arg.substr(0, equals);
            inline_ = true;
        }
        if (kFlags.count(arg)) {
            parsed.flags.insert(arg);
        } else if (kValued.count(arg)) {
            if (!inline_) {
                if (i + 1 >= args.size()) {
                    parsed.problem = arg + " needs a value";
                    return parsed;
                }
                value = args[++i];
            }
            parsed.options[arg].push_back(value);
        } else if (arg.starts_with("--")) {
            parsed.problem = "unknown option: " + arg;
            return parsed;
        } else {
            parsed.positional.push_back(args[i]);
        }
    }
    return parsed;
}

std::string Last(const Arguments& parsed, const std::string& option) {
    auto it = parsed.options.find(option);
    return it == parsed.options.end() || it->second.empty() ? std::string() : it->second.back();
}

std::optional<int> Whole(const std::string& text) {
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos || text.size() > 6) return std::nullopt;
    return std::stoi(text);
}

std::optional<ReplayRequest> RequestOf(const Arguments& parsed, std::string& problem) {
    ReplayRequest request;
    if (parsed.positional.size() != 1) {
        problem = "replay needs one theme folder (or none)";
        return std::nullopt;
    }
    if (parsed.positional[0] != "none") request.theme = PathOf(parsed.positional[0]);
    request.tree = PathOf(Last(parsed, "--tree"));
    request.out = PathOf(Last(parsed, "--out"));
    if (request.tree.empty() || request.out.empty()) {
        problem = "replay needs --tree and --out";
        return std::nullopt;
    }
    if (std::string file = Last(parsed, "--settings"); !file.empty()) request.settings = PathOf(file);
    if (auto it = parsed.options.find("--snippet"); it != parsed.options.end()) {
        for (const std::string& folder : it->second) request.snippets.push_back(PathOf(folder));
    }
    if (auto it = parsed.options.find("--set"); it != parsed.options.end()) {
        for (const std::string& pair : it->second) {
            size_t equals = pair.find('=');
            if (equals == std::string::npos || equals == 0) {
                problem = "--set needs key=value: " + pair;
                return std::nullopt;
            }
            request.values.emplace_back(pair.substr(0, equals), pair.substr(equals + 1));
        }
    }
    request.scheme = Last(parsed, "--scheme");
    if (std::string passes = Last(parsed, "--passes"); !passes.empty()) {
        std::optional<int> number = Whole(passes);
        if (!number || *number < 1) {
            problem = "--passes needs a number from 1";
            return std::nullopt;
        }
        request.maxPasses = *number;
    }
    if (std::string minute = Last(parsed, "--minute"); !minute.empty()) {
        std::optional<int> number = Whole(minute);
        if (!number || *number >= 24 * 60) {
            problem = "--minute needs a number from 0 to 1439";
            return std::nullopt;
        }
        request.minute = *number;
    }
    if (std::string window = Last(parsed, "--window"); !window.empty()) {
        size_t cross = window.find('x');
        std::optional<int> width = cross == std::string::npos ? std::nullopt : Whole(window.substr(0, cross));
        std::optional<int> height = cross == std::string::npos ? std::nullopt : Whole(window.substr(cross + 1));
        if (!width || !height || *width < 1 || *height < 1) {
            problem = "--window needs WIDTHxHEIGHT, both above 0, such as 1280x800";
            return std::nullopt;
        }
        request.window = ViewSize{static_cast<double>(*width), static_cast<double>(*height), false};
    }
    request.asRecorded = parsed.flags.count("--as-recorded") > 0;
    if (std::string then = Last(parsed, "--then"); !then.empty()) {
        request.then = then == "none" ? fs::path() : PathOf(then);
    }
    if (auto it = parsed.options.find("--then-snippet"); it != parsed.options.end()) {
        if (!request.then) {
            problem = "--then-snippet goes with --then";
            return std::nullopt;
        }
        for (const std::string& folder : it->second) request.thenSnippets.push_back(PathOf(folder));
    }
    if (request.then && !Last(parsed, "--compare").empty()) {
        problem = "--then and --compare both write diff.json: give one of them";
        return std::nullopt;
    }
    if (std::string artwork = Last(parsed, "--artwork"); !artwork.empty()) {
        // A palette file holds the same colours, one per line or separated by commas.
        std::error_code error;
        if (fs::is_regular_file(PathOf(artwork), error)) {
            std::optional<std::string> text = ReadText(PathOf(artwork));
            if (!text) {
                problem = "--artwork: cannot read " + artwork;
                return std::nullopt;
            }
            artwork = *text;
        }
        std::optional<std::map<std::string, std::string>> variables = ArtworkVariablesOf(artwork, &problem);
        if (!variables) return std::nullopt;
        request.artwork = std::move(*variables);
    }
    return request;
}

// The kind a folder's manifest.json gives it ("theme", "snippet"...), empty without one.
std::string FolderKind(const fs::path& folder) {
    std::optional<std::string> text = ReadText(folder / "manifest.json");
    std::optional<Json> json = text ? Json::Parse(*text) : std::nullopt;
    const Json* kind = json ? json->Find("kind") : nullptr;
    return kind && kind->AsString() ? *kind->AsString() : std::string();
}

// A theme given as a snippet, or the reverse, and an --out that is a file.
std::vector<std::string> FolderProblems(const ReplayRequest& request) {
    std::vector<std::string> problems;
    auto check = [&](const fs::path& folder, std::string_view wanted, std::string_view how) {
        const std::string kind = FolderKind(folder);
        if (kind.empty() || kind == wanted) return;
        problems.push_back(Utf8(folder) + " is a " + kind + ", not a " + std::string(wanted) + ": " + std::string(how));
    };
    if (!request.theme.empty()) check(request.theme, "theme", "give a snippet with --snippet");
    for (const fs::path& snippet : request.snippets) check(snippet, "snippet", "give a theme as the first argument");
    if (request.then && !request.then->empty()) check(*request.then, "theme", "give a snippet with --then-snippet");
    for (const fs::path& snippet : request.thenSnippets) check(snippet, "snippet", "give a theme with --then");
    std::error_code error;
    if (fs::exists(request.out, error) && !fs::is_directory(request.out, error)) {
        problems.push_back("--out is a file, not a folder: " + Utf8(request.out));
    }
    return problems;
}

const SettingSpec* FindSetting(const std::vector<SettingSpec>& specs, std::string_view key) {
    auto it = std::find_if(specs.begin(), specs.end(), [&](const SettingSpec& spec) { return spec.key == key; });
    return it == specs.end() ? nullptr : &*it;
}

// The problem of a themes.<id>.<key> or snippets.<id>.<key> value, against the manifest of the folder it names.
std::string ElementSetProblem(std::string_view key, const std::string& value,
                              const std::map<std::string, fs::path>& themes,
                              const std::map<std::string, fs::path>& snippets) {
    const bool theme = key.starts_with("themes.");
    std::string_view rest = key.substr(theme ? 7 : 9);
    const size_t dot = rest.find('.');
    if (dot == std::string_view::npos || dot == 0 || dot + 1 == rest.size()) return "needs <id>.<key>";
    const std::string id(rest.substr(0, dot));
    const std::string name(rest.substr(dot + 1));
    const auto& replayed = theme ? themes : snippets;
    auto folder = replayed.find(id);
    if (folder == replayed.end()) return std::string(theme ? "no theme" : "no snippet") + " \"" + id + "\" in this replay";
    if (!theme && name == "enabled") return {};
    const std::vector<SettingSpec> specs = DeclaredSettings(ReadText(folder->second / "manifest.json").value_or(""));
    const SettingSpec* spec = FindSetting(specs, name);
    if (!spec) {
        std::string declared;
        for (const SettingSpec& known : specs) declared += (declared.empty() ? "" : ", ") + known.key;
        return "\"" + id + "\" declares no setting \"" + name + "\"" +
               (declared.empty() ? std::string(" (it declares none)") : " (it declares " + declared + ")");
    }
    return SettingProblem(*spec, value);
}

// Each --set checked against what reads it: a theme's or a snippet's manifest, or the theme service's own keys. A
// misspelt key would otherwise pass for a setting that changes nothing.
std::vector<std::string> SetProblems(const ReplayRequest& request) {
    static const std::set<std::string, std::less<>> kThemeKeys = {
        "theme.scheme", "theme.schedule", "theme.accent", "theme.glass", "theme.parts.colors", "theme.parts.resources",
        "theme.parts.assets", "theme.parts.script"};
    std::map<std::string, fs::path> themes;
    std::map<std::string, fs::path> snippets;
    if (!request.theme.empty()) themes[FolderId(request.theme)] = request.theme;
    for (const fs::path& snippet : request.snippets) snippets[FolderId(snippet)] = snippet;
    if (request.then && !request.then->empty()) themes[FolderId(*request.then)] = *request.then;
    for (const fs::path& snippet : request.thenSnippets) snippets[FolderId(snippet)] = snippet;
    std::vector<std::string> problems;
    for (const auto& [key, value] : request.values) {
        std::string why;
        if (key.starts_with("themes.") || key.starts_with("snippets.")) {
            why = ElementSetProblem(key, value, themes, snippets);
        } else if (key == "theme.active") {
            why = "the replayed theme is the first argument";
        } else if (key == kInspectKey || key == kLiveReloadKey) {
            why = "the bench sets it itself";
        } else if (!kThemeKeys.count(key)) {
            why = key.starts_with("theme.") ? "not a key of the theme service"
                                            : "the bench reads theme.*, themes.<id>.* and snippets.<id>.* only";
        } else if (key == "theme.accent" && !value.empty() && !IsArtworkAccent(value) && !ParseAccent(value)) {
            why = "\"" + value + "\" is not RRGGBB, #RRGGBB or artwork";
        }
        if (!why.empty()) problems.push_back("--set " + key + "=" + value + ": " + why);
    }
    return problems;
}

bool WriteOutcome(const ReplayOutcome& outcome, const fs::path& out, std::ostream& err) {
    std::error_code error;
    fs::create_directories(out, error);
    if (!WriteText(out / "report.json", outcome.report) || !WriteText(out / "wireframe.svg", outcome.wireframe)) {
        err << "cannot write into " << Utf8(out) << '\n';
        return false;
    }
    return true;
}

void Summarize(const ReplayOutcome& outcome, const fs::path& out, std::ostream& stream) {
    std::optional<Json> report = Json::Parse(outcome.report);
    auto count = [&](std::string_view key) { return report ? ArrayOf(*report, key).size() : 0; };
    stream << (outcome.settled ? "Settled" : "Did not settle") << " after " << outcome.passes << " pass(es): "
           << count("rules") << " rule(s), " << count("unmatched") << " selector(s) matching nothing, " << count("refused")
           << " refusal(s), " << count("final") << " element(s) changed, " << count("hover")
           << " with values under the pointer, " << count("warnings") << " warning(s)\n";
    stream << "  " << Utf8(out / "report.json") << "\n  " << Utf8(out / "wireframe.svg") << '\n';
}

// --then: the second state on a fresh tree into <out>\fresh, its diff with the same state after the first, and what
// the first left behind. 1 when something was, or when a state did not settle.
int RemovalOutcome(const ReplayOutcome& outcome, const fs::path& folder, std::ostream& out, std::ostream& err) {
    ReplayOutcome fresh;
    fresh.report = outcome.freshReport;
    fresh.wireframe = outcome.freshWireframe;
    if (!WriteOutcome(fresh, folder / "fresh", err)) return kFailed;
    bool same = false;
    std::string problem;
    std::optional<std::string> diff = DiffReports(outcome.report, outcome.freshReport, same, &problem);
    if (!diff || !WriteText(folder / "diff.json", *diff)) {
        err << (diff ? "cannot write diff.json" : problem) << '\n';
        return kFailed;
    }
    if (outcome.leftovers == 0) {
        out << "Nothing left behind: the second state shows what a fresh tree shows\n";
    } else {
        out << outcome.leftovers << " value(s) left behind by the first state (report.json, leftovers): "
            << Utf8(folder / "diff.json") << '\n';
    }
    return outcome.settled && outcome.leftovers == 0 ? kOk : kFailed;
}

int ReplayCommand(const Arguments& parsed, std::ostream& out, std::ostream& err) {
    if (std::string target = Last(parsed, "--scrub"); !target.empty()) {
        std::string tree = Last(parsed, "--tree");
        if (tree.empty() || !parsed.positional.empty()) {
            err << kUsage;
            return kWrongUse;
        }
        return RunReplayCommand({"scrub", tree, target}, out, err);
    }
    std::string problem;
    std::optional<ReplayRequest> request = RequestOf(parsed, problem);
    if (!request) {
        err << problem << '\n' << kUsage;
        return kWrongUse;
    }
    std::vector<std::string> problems = FolderProblems(*request);
    if (std::string compare = Last(parsed, "--compare"); !compare.empty() && compare != "none") {
        const std::string kind = FolderKind(PathOf(compare));
        if (!kind.empty() && kind != "theme") problems.push_back(compare + " is a " + kind + ", not a theme: --compare takes one");
    }
    for (const std::string& line : SetProblems(*request)) problems.push_back("warning: " + line);
    if (!problems.empty()) {
        for (const std::string& line : problems) err << line << '\n';
        return kWrongUse;
    }
    std::optional<ReplayOutcome> outcome = Replay(*request, &problem);
    if (!outcome) {
        err << problem << '\n';
        return kFailed;
    }
    if (!WriteOutcome(*outcome, request->out, err)) return kFailed;
    Summarize(*outcome, request->out, out);
    int status = outcome->settled ? kOk : kFailed;
    if (request->then) return RemovalOutcome(*outcome, request->out, out, err);

    std::string compare = Last(parsed, "--compare");
    if (compare.empty()) return status;
    ReplayRequest other = *request;
    other.theme = compare == "none" ? fs::path() : PathOf(compare);
    other.out = request->out / "compare";
    std::optional<ReplayOutcome> second = Replay(other, &problem);
    if (!second) {
        err << problem << '\n';
        return kFailed;
    }
    if (!WriteOutcome(*second, other.out, err)) return kFailed;
    Summarize(*second, other.out, out);
    bool same = false;
    std::optional<std::string> diff = DiffReports(outcome->report, second->report, same, &problem);
    if (!diff || !WriteText(request->out / "diff.json", *diff)) {
        err << (diff ? "cannot write diff.json" : problem) << '\n';
        return kFailed;
    }
    out << (same ? "No difference" : "Differences") << ": " << Utf8(request->out / "diff.json") << '\n';
    return status;
}

std::optional<std::string> ReportAt(const std::string& argument) {
    fs::path path = PathOf(argument);
    std::error_code error;
    if (fs::is_directory(path, error)) path /= "report.json";
    return ReadText(path);
}

}  // namespace

int RunReplayCommand(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    if (args.empty()) {
        err << kUsage;
        return kWrongUse;
    }
    Arguments parsed = Parse(args);
    if (!parsed.problem.empty()) {
        err << parsed.problem << '\n' << kUsage;
        return kWrongUse;
    }
    if (args[0] == "replay") return ReplayCommand(parsed, out, err);
    if (args[0] == "scrub") {
        if (parsed.positional.size() != 2) {
            err << kUsage;
            return kWrongUse;
        }
        std::optional<std::string> text = ReadText(PathOf(parsed.positional[0]));
        if (!text) {
            err << "cannot read " << parsed.positional[0] << '\n';
            return kFailed;
        }
        cli::ScrubCounts counts;
        std::string problem;
        std::optional<std::string> scrubbed = cli::ScrubDump(*text, cli::CurrentIdentity(), counts, &problem);
        if (!scrubbed) {
            err << parsed.positional[0] << ": " << problem << '\n';
            return kFailed;
        }
        if (!WriteText(PathOf(parsed.positional[1]), *scrubbed)) {
            err << "cannot write " << parsed.positional[1] << '\n';
            return kFailed;
        }
        out << "Scrubbed: " << counts.names << " name(s), " << counts.texts << " text(s), " << counts.addresses
            << " address(es), " << counts.masked << " value(s) masked: " << parsed.positional[1] << '\n';
        return kOk;
    }
    if (args[0] == "diff") {
        if (parsed.positional.size() != 2) {
            err << kUsage;
            return kWrongUse;
        }
        std::optional<std::string> a = ReportAt(parsed.positional[0]);
        std::optional<std::string> b = ReportAt(parsed.positional[1]);
        if (!a || !b) {
            err << "cannot read " << (a ? parsed.positional[1] : parsed.positional[0]) << '\n';
            return kFailed;
        }
        bool same = false;
        std::string problem;
        std::optional<std::string> diff = DiffReports(*a, *b, same, &problem);
        if (!diff) {
            err << problem << '\n';
            return kFailed;
        }
        if (std::string file = Last(parsed, "--out"); !file.empty()) {
            if (!WriteText(PathOf(file), *diff)) {
                err << "cannot write " << file << '\n';
                return kFailed;
            }
            out << (same ? "No difference" : "Differences") << ": " << file << '\n';
        } else {
            out << *diff;
        }
        return same ? kOk : kFailed;
    }
    err << kUsage;
    return kWrongUse;
}

}  // namespace ampify::theme
