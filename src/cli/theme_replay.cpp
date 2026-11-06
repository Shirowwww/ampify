#include "theme_replay.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>
#include <tuple>

#include "../core/color_extract.hpp"
#include "../core/journal.hpp"
#include "../core/json.hpp"
#include "../core/settings.hpp"
#include "../core/theme/inspector.hpp"
#include "../core/theme/properties.hpp"
#include "../core/theme/replay.hpp"
#include "../core/theme/replay_contrast.hpp"
#include "../core/theme/scheme_choice.hpp"
#include "../core/theme/selector.hpp"
#include "../core/theme/session.hpp"
#include "../core/theme/theme_settings.hpp"
#include "../core/theme/worker.hpp"
#include "dump_scrub.hpp"
#include "replay_util.hpp"

namespace ampify::theme {

namespace fs = std::filesystem;
using namespace replay;

namespace {

constexpr size_t kMatchesListed = 200;  // per rule; `count` gives them all

// Every `from` in `text`, its ASCII letters in any case (Windows paths), replaced by `to`.
std::string ReplaceNoCase(std::string text, std::string_view from, std::string_view to) {
    if (from.empty()) return text;
    auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
    std::string out;
    for (size_t at = 0; at < text.size();) {
        bool match = at + from.size() <= text.size();
        for (size_t i = 0; match && i < from.size(); i++) match = lower(text[at + i]) == lower(from[i]);
        if (match) {
            out += to;
            at += from.size();
        } else {
            out += text[at++];
        }
    }
    return out;
}

// A report or a wireframe names no folder of this PC (docs/development/offline-bench.md): a path under the scratch kit
// becomes relative to the kit, the rest of the profile folder %USERPROFILE%. Each folder is looked for as written with
// '/', with '\', and with '\' escaped as JSON writes it.
std::string KeepPathsOut(std::string text, const fs::path& kit) {
    auto spellings = [](const std::string& generic) {
        std::string native = generic;
        std::replace(native.begin(), native.end(), '/', '\\');
        std::string escaped;
        for (char c : native) escaped += c == '\\' ? std::string("\\\\") : std::string(1, c);
        return std::array<std::string, 3>{generic, escaped, native};
    };
    const std::string kitText = Utf8(kit.lexically_normal());
    if (!kitText.empty()) {
        const auto forms = spellings(kitText);
        const std::array<std::string, 3> separators = {"/", "\\\\", "\\"};
        for (size_t i = 0; i < forms.size(); i++) text = ReplaceNoCase(std::move(text), forms[i] + separators[i], "");
        for (const std::string& form : forms) text = ReplaceNoCase(std::move(text), form, ".");
    }
    for (const std::string& home : cli::CurrentIdentity().homes) {
        if (home.size() < 4) continue;  // never a drive alone
        std::string generic = home;
        std::replace(generic.begin(), generic.end(), '\\', '/');
        for (const std::string& form : spellings(generic)) text = ReplaceNoCase(std::move(text), form, "%USERPROFILE%");
    }
    return text;
}

// --- The scratch kit ------------------------------------------------------------------------------------------------

bool CopyFolder(const fs::path& from, const fs::path& to, std::string* problem) {
    std::error_code error;
    fs::create_directories(to.parent_path(), error);
    fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing, error);
    if (error && problem) *problem = "cannot copy " + Utf8(from) + ": " + error.message();
    return !error;
}

// The kit the worker reads: themes\<id>\ and snippets\<id>\, copied from where they are.
// Copies a theme ("themes") or a snippet ("snippets") folder into the kit; its id, or nothing with `problem` set.
std::optional<std::string> CopyIntoKit(const fs::path& folder, const fs::path& kit, const char* where,
                                       std::string* problem) {
    std::error_code error;
    const bool theme = std::string_view(where) == "themes";
    if (!fs::is_directory(folder, error)) {
        if (problem) *problem = std::string(theme ? "theme" : "snippet") + " folder not found: " + Utf8(folder);
        return std::nullopt;
    }
    if ((Utf8(kit) + "/").starts_with(Utf8(fs::absolute(folder).lexically_normal()) + "/")) {
        if (problem) *problem = "--out must not be inside the folder " + Utf8(folder);
        return std::nullopt;
    }
    const std::string id = FolderId(folder);
    if (!CopyFolder(folder, kit / where / PathOf(id), problem)) return std::nullopt;
    return id;
}

// The ids in the kit: the theme (empty for none) and its snippets, then those of --then.
struct KitIds {
    std::string theme;
    std::vector<std::string> snippets;
    std::string thenTheme;
    std::vector<std::string> thenSnippets;
};

// The kit the worker reads: themes\<id>\ and snippets\<id>\, copied from where they are.
std::optional<KitIds> PrepareKit(const ReplayRequest& request, const fs::path& kit, std::string* problem) {
    std::error_code error;
    fs::remove_all(kit, error);
    fs::create_directories(kit / "themes", error);
    KitIds ids;
    auto copy = [&](const fs::path& folder, const char* where, std::string& id) {
        std::optional<std::string> copied = CopyIntoKit(folder, kit, where, problem);
        if (copied) id = *copied;
        return copied.has_value();
    };
    if (!request.theme.empty() && !copy(request.theme, "themes", ids.theme)) return std::nullopt;
    for (const fs::path& snippet : request.snippets) {
        if (!copy(snippet, "snippets", ids.snippets.emplace_back())) return std::nullopt;
    }
    if (request.then && !request.then->empty() && !copy(*request.then, "themes", ids.thenTheme)) return std::nullopt;
    for (const fs::path& snippet : request.thenSnippets) {
        if (!copy(snippet, "snippets", ids.thenSnippets.emplace_back())) return std::nullopt;
    }
    return ids;
}

// The settings of one state: its theme, its snippets on and every other snippet of the kit off.
Settings StateSettings(Settings settings, const std::string& theme, const std::vector<std::string>& on,
                       const KitIds& ids) {
    settings.Set("theme.active", theme.empty() ? std::string("none") : theme);
    for (const auto* list : {&ids.snippets, &ids.thenSnippets}) {
        for (const std::string& id : *list) settings.Set("snippets." + id + ".enabled", "0");
    }
    for (const std::string& id : on) settings.Set("snippets." + id + ".enabled", "1");
    return settings;
}

Settings ReplaySettings(const ReplayRequest& request, std::string* problem, bool& ok) {
    Settings settings;
    ok = true;
    if (request.settings && !settings.Load(request.settings->string())) {
        if (problem) *problem = "cannot read " + Utf8(*request.settings);
        ok = false;
    }
    // The bench never asks for a dump and never watches files: one generation for the whole replay.
    settings.Set("theme.inspect", "0");
    settings.Set("theme.live_reload", "0");
    if (!request.scheme.empty()) settings.Set("theme.scheme", request.scheme);
    for (const auto& [key, value] : request.values) settings.Set(key, value);
    return settings;
}

// --- The report -----------------------------------------------------------------------------------------------------

struct Pass {
    int changes = 0;   // accepted writes, moves, state values and decors
    int refusals = 0;  // refused calls of the backend
    std::vector<std::string> written;  // "element property", to name what fights when nothing settles
};

Json RulesJson(const Theme& theme, const std::vector<NodeInfo>& nodes, ReplayBackend& tree, Json::Array& unmatched) {
    std::optional<ViewSize> size = tree.WindowSize();
    const bool dark = theme.dark.value_or(IsDarkScheme(theme.scheme));
    std::map<std::string, std::pair<const Assignment*, int>> bySelector;  // selector text: first rule, elements
    Json::Array rules;
    for (const Assignment& assignment : theme.assignments) {
        Json::Array matched;
        int count = 0;
        for (int i = 0; i < static_cast<int>(nodes.size()); i++) {
            if (!Matches(assignment.selector, nodes, i)) continue;
            if (count++ < static_cast<int>(kMatchesListed)) matched.push_back(Number(static_cast<double>(nodes[i].id - 1)));
        }
        auto& entry = bySelector.try_emplace(assignment.selector.text, &assignment, 0).first->second;
        entry.second += count;

        Json::Object rule;
        rule.emplace_back("selector", Json(assignment.selector.text));
        rule.emplace_back("property", Json(assignment.property));
        rule.emplace_back("value", Json(ShownOf(assignment.property, assignment.value)));
        rule.emplace_back("source", Json(assignment.source));
        rule.emplace_back("line", Number(assignment.line));
        if (assignment.selector.trigger != Trigger::Always) {
            rule.emplace_back("state", Json(TriggerName(assignment.selector.trigger)));
        }
        if (assignment.selector.pseudo != PseudoElement::None) {
            rule.emplace_back("pseudo", Json(PseudoElementName(assignment.selector.pseudo)));
        }
        if (assignment.media) {
            bool active = size && assignment.media->Matches(size->width, size->height, size->sidebarFolded) &&
                          assignment.media->MatchesScheme(dark);
            rule.emplace_back("media", Json(assignment.media->text));
            rule.emplace_back("mediaActive", Json(active));
        }
        rule.emplace_back("count", Number(count));
        rule.emplace_back("matched", Json(std::move(matched)));
        rules.push_back(Json(std::move(rule)));
    }
    for (const auto& [text, entry] : bySelector) {
        if (entry.second > 0) continue;
        Json::Object item;
        item.emplace_back("selector", Json(text));
        item.emplace_back("source", Json(entry.first->source));
        item.emplace_back("line", Number(entry.first->line));
        unmatched.push_back(Json(std::move(item)));
    }
    return Json(std::move(rules));
}

constexpr size_t kRefusedNodesListed = 20;  // per refusal; `count` gives them all

Json NodeList(const std::set<int>& nodes) {
    Json::Array list;
    for (int node : nodes) {
        if (list.size() == kRefusedNodesListed) break;
        list.push_back(Number(node));
    }
    return Json(std::move(list));
}

// Each refusal once, with `count`, the elements it was refused on: eight shelves refusing the same font are one entry
// counted 8. `fromPass` leaves out the backend's refusals of earlier passes (the first theme of --then).
Json RefusedJson(const ReplayBackend& tree, const ThemeSession& session, int fromPass) {
    struct Seen {
        std::string value;
        std::set<int> nodes;
    };
    std::map<std::tuple<std::string, std::string, std::string>, Seen> backend;  // element, property, reason
    std::vector<std::tuple<std::string, std::string, std::string>> order;
    for (const ReplayBackend::Change& change : tree.Changes()) {
        if (change.accepted || change.pass < fromPass) continue;
        const std::string element = change.node >= 0 ? Label(tree.Nodes()[change.node]) : std::string();
        auto key = std::make_tuple(element, change.property, change.reason);
        auto [it, fresh] = backend.try_emplace(key, Seen{change.after, {}});
        if (fresh) order.push_back(key);
        if (change.node >= 0) it->second.nodes.insert(change.node);
    }
    Json::Array refused;
    for (const auto& key : order) {
        const Seen& seen = backend[key];
        Json::Object item;
        item.emplace_back("from", Json("backend"));
        if (!std::get<0>(key).empty()) item.emplace_back("element", Json(std::get<0>(key)));
        item.emplace_back("property", Json(std::get<1>(key)));
        item.emplace_back("value", Json(seen.value));
        item.emplace_back("reason", Json(std::get<2>(key)));
        item.emplace_back("count", Number(static_cast<double>(std::max<size_t>(1, seen.nodes.size()))));
        if (!seen.nodes.empty()) item.emplace_back("nodes", NodeList(seen.nodes));
        refused.push_back(Json(std::move(item)));
    }
    // The engine refuses before it writes (a property the element does not have, a size it cannot scale, a move
    // without a target); its log names each refusal once, the engine keeps every element.
    for (const auto& [key, refusal] : session.Refusals()) {
        std::set<int> nodes;
        for (NodeId id : refusal.elements) nodes.insert(tree.IndexOf(id));
        Json::Object item;
        item.emplace_back("from", Json("engine"));
        item.emplace_back("reason", Json(refusal.message));
        item.emplace_back("count", Number(static_cast<double>(std::max<size_t>(1, nodes.size()))));
        if (!nodes.empty()) item.emplace_back("nodes", NodeList(nodes));
        refused.push_back(Json(std::move(item)));
    }
    return Json(std::move(refused));
}

Json FinalJson(ReplayBackend& tree, const std::vector<NodeInfo>& nodes) {
    std::map<int, std::map<std::string, std::string>> before;  // node: property: value before the first change
    for (const ReplayBackend::Change& change : tree.Changes()) {
        if (!change.accepted || change.node < 0 || change.property == "parent") continue;
        before[change.node].try_emplace(change.property, change.before);
    }
    std::map<int, int> place;  // dump index: index in `nodes`
    for (int i = 0; i < static_cast<int>(nodes.size()); i++) place[static_cast<int>(nodes[i].id - 1)] = i;
    Json::Array list;
    for (const auto& [node, properties] : before) {
        Json::Object values;
        for (const auto& [property, was] : properties) {
            Json::Object value;
            value.emplace_back("before", Json(was));
            value.emplace_back("after", Json(ShownOf(property, tree.Shown(node, property))));
            values.emplace_back(property, Json(std::move(value)));
        }
        Json::Object item;
        item.emplace_back("node", Number(node));
        item.emplace_back("element", Json(Label(tree.Nodes()[node])));
        item.emplace_back("selector", Json(SuggestSelector(nodes, place[node])));
        item.emplace_back("values", Json(std::move(values)));
        list.push_back(Json(std::move(item)));
    }
    return Json(std::move(list));
}

Json HoversJson(const ReplayBackend& tree) {
    Json::Array list;
    for (const auto& [node, values] : tree.Hovers()) {
        Json::Array shown;
        for (const HoverValue& value : values) {
            Json::Object item;
            item.emplace_back("property", Json(value.property));
            item.emplace_back("value", Json(ShownOf(value.property, value.value)));
            item.emplace_back("state", Json(TriggerName(value.trigger)));
            if (value.watch) item.emplace_back("watch", Number(static_cast<double>(value.watch - 1)));
            shown.push_back(Json(std::move(item)));
        }
        Json::Object item;
        item.emplace_back("node", Number(node));
        item.emplace_back("element", Json(Label(tree.Nodes()[node])));
        item.emplace_back("values", Json(std::move(shown)));
        list.push_back(Json(std::move(item)));
    }
    return Json(std::move(list));
}

// Each ::before and ::after: its image (a path, kept relative to the kit by KeepPathsOut, or bars:N) and its other
// values (size, margins, colour, opacity...), so a decor that changes alone shows in a diff.
Json DecorsJson(const ReplayBackend& tree) {
    Json::Array list;
    for (const auto& [key, decor] : tree.Decors()) {
        Json::Object values;
        for (const auto& [property, value] : decor.properties) {
            values.emplace_back(property, Json(ShownOf(property, value)));
        }
        Json::Object item;
        item.emplace_back("node", Number(key.first));
        item.emplace_back("element", Json(Label(tree.Nodes()[key.first])));
        item.emplace_back("slot", Json(PseudoElementName(key.second)));
        item.emplace_back("image", Json(decor.image));
        item.emplace_back("values", Json(std::move(values)));
        list.push_back(Json(std::move(item)));
    }
    return Json(std::move(list));
}

Json ResourcesJson(const ReplayBackend& tree) {
    std::map<std::string, std::pair<std::string, std::string>> changed;  // key: first before, last after
    for (const ReplayBackend::Change& change : tree.Changes()) {
        if (!change.accepted || !change.property.starts_with("resource ")) continue;
        auto [it, fresh] = changed.try_emplace(change.property.substr(9), change.before, change.after);
        if (!fresh) it->second.second = change.after;
    }
    Json::Array list;
    for (const auto& [key, values] : changed) {
        Json::Object item;
        item.emplace_back("key", Json(key));
        item.emplace_back("before", Json(values.first));
        item.emplace_back("after", Json(values.second));
        list.push_back(Json(std::move(item)));
    }
    return Json(std::move(list));
}

// --- Contrast -------------------------------------------------------------------------------------------------------

std::string ModeName(std::optional<bool> dark) {
    return dark ? (*dark ? "dark" : "light") : "unknown";
}

// What the bench cannot know when the theme asks for the other mode than the dump's.
std::string ModeWarning(const ReplayBackend& tree) {
    if (!tree.AppMode() || tree.AppMode() == tree.RecordedDark()) return {};
    return "the theme puts Apple Music in " + ModeName(tree.AppMode()) + " mode, and the dump was recorded in " +
           (tree.RecordedDark() ? ModeName(tree.RecordedDark()) + " mode" : std::string("an unknown mode")) +
           ": the colours of Apple's that the theme does not write are those of the dump, not the ones Apple Music "
           "shows; contrast on them is not judged";
}

constexpr size_t kTextsListed = 300;

// The contrast summary, and in `texts` each text that is low, not judged or unknown (replay_contrast.hpp).
Json ContrastJson(ReplayBackend& tree, const std::vector<NodeInfo>& nodes, Json::Array& texts) {
    std::map<int, int> place;  // dump index: index in `nodes`
    for (int i = 0; i < static_cast<int>(nodes.size()); i++) place[static_cast<int>(nodes[i].id - 1)] = i;
    std::map<TextContrast::Verdict, int> counts;
    for (const TextContrast& text : JudgeContrast(tree)) {
        counts[text.verdict]++;
        if (text.verdict == TextContrast::Verdict::Fine || texts.size() == kTextsListed) continue;
        Json::Object item;
        item.emplace_back("node", Number(text.node));
        item.emplace_back("element", Json(Label(tree.Nodes()[text.node])));
        item.emplace_back("selector", Json(SuggestSelector(nodes, place[text.node])));
        item.emplace_back("verdict", Json(std::string(VerdictName(text.verdict))));
        item.emplace_back("text", Json(text.text));
        if (text.backdrop >= 0) {
            item.emplace_back("under", Json(text.under));
            item.emplace_back("ratio", Number(std::round(text.ratio * 100) / 100));
            item.emplace_back("backdrop", Number(text.backdrop));
            item.emplace_back("backdropElement", Json(Label(tree.Nodes()[text.backdrop])));
        }
        item.emplace_back("needed", Number(text.needed));
        if (text.passedOver) item.emplace_back("passedOver", Number(text.passedOver));
        texts.push_back(Json(std::move(item)));
    }
    Json::Object summary;
    summary.emplace_back("recordedMode", Json(ModeName(tree.RecordedDark())));
    summary.emplace_back("appMode", Json(tree.AppMode() ? ModeName(tree.AppMode()) : std::string("as recorded")));
    summary.emplace_back("fine", Number(counts[TextContrast::Verdict::Fine]));
    summary.emplace_back("low", Number(counts[TextContrast::Verdict::Low]));
    summary.emplace_back("notJudged", Number(counts[TextContrast::Verdict::NotJudged]));
    summary.emplace_back("unknown", Number(counts[TextContrast::Verdict::Unknown]));
    return Json(std::move(summary));
}

// --- The run --------------------------------------------------------------------------------------------------------

struct Run {
    std::vector<Pass> passes;
    bool settled = false;
};

// The passes of one state, numbered from `firstPass` in the tree's changes.
Run RunPasses(ThemeWorker& worker, const Settings& settings, const ReplayRequest& request, ThemeSession& session,
              ReplayBackend& tree, int firstPass = 1) {
    Run run;
    SessionRequest wanted = worker.Tick(settings, request.minute);
    for (int pass = 1; pass <= std::max(1, request.maxPasses); pass++) {
        tree.BeginPass(firstPass + pass - 1);
        const size_t first = tree.Changes().size();
        const auto hovers = tree.Hovers();
        const auto decors = tree.Decors();
        session.Tick(tree, wanted);
        // The theme service sets the app's mode after each pass: the theme's, or Apple's own when it says none.
        tree.SetAppMode(wanted.theme ? AppModeOf(wanted.theme->dark, wanted.theme->scheme) : std::nullopt);
        Pass done;
        for (size_t i = first; i < tree.Changes().size(); i++) {
            const ReplayBackend::Change& change = tree.Changes()[i];
            if (!change.accepted) {
                done.refusals++;
                continue;
            }
            done.changes++;
            done.written.push_back((change.node >= 0 ? Label(tree.Nodes()[change.node]) + " " : std::string()) +
                                   change.property);
        }
        if (tree.Hovers() != hovers) done.changes++;
        if (tree.Decors() != decors) done.changes++;
        run.passes.push_back(std::move(done));
        if (run.passes.back().changes == 0) {
            run.settled = true;
            break;
        }
    }
    return run;
}

// With --then: the first state, and what it left behind once the second replaced it.
struct Removal {
    std::string first;  // the first theme's id, empty for none
    const Run* firstRun = nullptr;
    const Json::Array* leftovers = nullptr;
    int fromPass = 1;  // the second state's first pass
};

Json ThenJson(const Removal& removal) {
    Json::Object then;
    then.emplace_back("first", Json(removal.first));
    then.emplace_back("firstPasses", Number(static_cast<double>(removal.firstRun->passes.size())));
    then.emplace_back("firstSettled", Json(removal.firstRun->settled));
    then.emplace_back("leftovers", Number(static_cast<double>(removal.leftovers->size())));
    return Json(std::move(then));
}

std::string ReportJson(const ReplayRequest& request, const std::string& id, ReplayBackend& tree, ThemeSession& session,
                       const Run& run, const std::vector<std::string>& journal, const std::vector<std::string>& own,
                       const Removal* removal = nullptr) {
    std::vector<NodeInfo> nodes = tree.Snapshot();
    Json::Array unmatched;
    Json rules = Json(Json::Array{});
    std::shared_ptr<const Theme> applied = session.Applied();
    if (applied) rules = RulesJson(*applied, nodes, tree, unmatched);

    Json::Array warnings;
    for (const std::string& warning : tree.Warnings()) warnings.push_back(Json(warning));
    for (const std::string& warning : own) warnings.push_back(Json(warning));
    for (const std::string& line : journal) {
        // The theme's own problems (a value it cannot read, a missing file...), as the kernel logs them.
        if ((line.starts_with("Theme \"") || line.starts_with("Snippet \"") || line.starts_with("Layout: ")) &&
            line.find(" loaded") == std::string::npos && line.find(" value(s) applied") == std::string::npos) {
            warnings.push_back(Json(line));
        }
    }
    if (!tree.UnrecordedResources().empty()) {
        std::string keys;
        for (const std::string& key : tree.UnrecordedResources()) {
            if (keys.size() > 300) {
                keys += ", ...";
                break;
            }
            keys += (keys.empty() ? "" : ", ") + key;
        }
        warnings.push_back(Json(std::to_string(tree.UnrecordedResources().size()) +
                                " resource key(s) the dump does not list were taken unverified, as WinUI's control "
                                "keys are: " + keys));
    }
    if (!run.settled) {
        std::string what;
        for (const std::string& item : run.passes.back().written) {
            if (what.size() > 300) break;
            what += (what.empty() ? "" : ", ") + item;
        }
        warnings.push_back(Json("did not settle in " + std::to_string(run.passes.size()) +
                                " passes: the last one still wrote " + what));
    }
    if (std::string mode = ModeWarning(tree); !mode.empty()) warnings.push_back(Json(mode));
    Json::Array texts;
    Json contrast = ContrastJson(tree, nodes, texts);
    Json::Array passes;
    for (size_t i = 0; i < run.passes.size(); i++) {
        Json::Object item;
        item.emplace_back("pass", Number(static_cast<double>(i + 1)));
        item.emplace_back("changes", Number(run.passes[i].changes));
        item.emplace_back("refused", Number(run.passes[i].refusals));
        passes.push_back(Json(std::move(item)));
    }
    Json::Array lines;
    for (const std::string& line : journal) lines.push_back(Json(line));

    Json::Object artwork;  // the song's colours given with --artwork; the page's (page-art-*) are the same
    for (const auto& [name, value] : request.artwork) {
        if (name.starts_with("art-")) artwork.emplace_back(name, Json(value));
    }
    Json::Object dump;
    dump.emplace_back("file", Json(Utf8(request.tree.filename())));
    dump.emplace_back("version", Number(tree.Version()));
    dump.emplace_back("elements", Number(static_cast<double>(tree.Nodes().size())));
    dump.emplace_back("boxes", Json(tree.HasBoxes()));
    dump.emplace_back("recordedTheme", Json(tree.RecordedTheme()));
    if (std::optional<ViewSize> size = tree.WindowSize()) {
        dump.emplace_back("window", Json(Json::Array{Number(size->width), Number(size->height)}));
    }

    std::string text = "{\n  \"version\": 1,\n  \"theme\": " + Json(id).Dump() +
                       ",\n  \"scheme\": " + Json(applied ? applied->scheme : std::string()).Dump() +
                       ",\n  \"dump\": " + Json(std::move(dump)).Dump() +
                       ",\n  \"artwork\": " + Json(std::move(artwork)).Dump() +
                       ",\n  \"settled\": " + (run.settled ? "true" : "false") +
                       ",\n  \"passes\": " + Lines(passes) +
                       (removal ? ",\n  \"then\": " + ThenJson(*removal).Dump() + ",\n  \"leftovers\": " +
                                      Lines(*removal->leftovers)
                                : std::string()) +
                       ",\n  \"warnings\": " + Lines(warnings) +
                       ",\n  \"rules\": " + Lines(*rules.AsArray()) +
                       ",\n  \"unmatched\": " + Lines(unmatched) +
                       ",\n  \"refused\": " + Lines(*RefusedJson(tree, session, removal ? removal->fromPass : 1).AsArray()) +
                       ",\n  \"contrast\": " + contrast.Dump() +
                       ",\n  \"texts\": " + Lines(texts) +
                       ",\n  \"final\": " + Lines(*FinalJson(tree, nodes).AsArray()) +
                       ",\n  \"hover\": " + Lines(*HoversJson(tree).AsArray()) +
                       ",\n  \"decors\": " + Lines(*DecorsJson(tree).AsArray()) +
                       ",\n  \"resources\": " + Lines(*ResourcesJson(tree).AsArray()) +
                       ",\n  \"dictionary\": " + (tree.Dictionary().empty() ? "false" : "true") +
                       ",\n  \"journal\": " + Lines(lines) + "\n}\n";
    return text;
}

Json Leftover(int node, const std::string& element, const std::string& property, const std::string& value,
              const std::string& fresh) {
    Json::Object item;
    if (node >= 0) item.emplace_back("node", Number(node));
    if (!element.empty()) item.emplace_back("element", Json(element));
    item.emplace_back("property", Json(property));
    item.emplace_back("value", Json(value));
    item.emplace_back("fresh", Json(fresh));
    return Json(std::move(item));
}

std::string DecorText(const std::map<std::pair<int, PseudoElement>, Decor>& decors,
                      const std::pair<int, PseudoElement>& key) {
    auto it = decors.find(key);
    if (it == decors.end()) return "(none)";
    std::string text = it->second.image;
    for (const auto& [property, value] : it->second.properties) text += " " + property + "=" + ShownOf(property, value);
    return text;
}

// What the tree shows after the second state that a fresh tree replaying the same state does not: the first state's
// values, positions, resources, dictionary, decors and values under the pointer that its removal left behind.
Json::Array Leftovers(ReplayBackend& tree, ReplayBackend& fresh) {
    std::set<std::pair<int, std::string>> values;
    std::set<std::string> resources;
    for (ReplayBackend* side : {&tree, &fresh}) {
        for (const ReplayBackend::Change& change : side->Changes()) {
            if (!change.accepted) continue;
            if (change.node >= 0) values.insert({change.node, change.property});
            else if (change.property.starts_with("resource ")) resources.insert(change.property.substr(9));
        }
    }
    Json::Array list;
    for (const auto& [node, property] : values) {
        const std::string element = Label(tree.Nodes()[node]);
        if (property == "parent") {
            const int now = tree.Nodes()[node].parent;
            const int clean = fresh.Nodes()[node].parent;
            if (now != clean) list.push_back(Leftover(node, element, "parent", std::to_string(now), std::to_string(clean)));
            continue;
        }
        const std::string now = ShownOf(property, tree.Shown(node, property));
        const std::string clean = ShownOf(property, fresh.Shown(node, property));
        if (now != clean) list.push_back(Leftover(node, element, property, now, clean));
    }
    for (const std::string& key : resources) {
        const std::string now = ShownOf(key, tree.ReadResource(key));
        const std::string clean = ShownOf(key, fresh.ReadResource(key));
        if (now != clean) list.push_back(Leftover(-1, {}, "resource " + key, now, clean));
    }
    if (tree.Dictionary() != fresh.Dictionary()) {
        list.push_back(Leftover(-1, {}, "dictionary", tree.Dictionary().empty() ? "(none)" : "merged",
                                fresh.Dictionary().empty() ? "(none)" : "merged"));
    }
    std::set<std::pair<int, PseudoElement>> decors;
    for (ReplayBackend* side : {&tree, &fresh}) {
        for (const auto& [key, decor] : side->Decors()) decors.insert(key);
    }
    for (const auto& key : decors) {
        const std::string now = DecorText(tree.Decors(), key);
        const std::string clean = DecorText(fresh.Decors(), key);
        if (now != clean) {
            list.push_back(Leftover(key.first, Label(tree.Nodes()[key.first]), PseudoElementName(key.second), now, clean));
        }
    }
    std::set<int> hovers;
    for (ReplayBackend* side : {&tree, &fresh}) {
        for (const auto& [node, held] : side->Hovers()) hovers.insert(node);
    }
    for (int node : hovers) {
        auto now = tree.Hovers().find(node);
        auto clean = fresh.Hovers().find(node);
        const bool same = (now == tree.Hovers().end()) == (clean == fresh.Hovers().end()) &&
                          (now == tree.Hovers().end() || now->second == clean->second);
        if (same) continue;
        auto count = [](auto it, auto end) {
            return it == end ? std::string("(none)") : std::to_string(it->second.size()) + " value(s)";
        };
        list.push_back(Leftover(node, Label(tree.Nodes()[node]), "hover", count(now, tree.Hovers().end()),
                                count(clean, fresh.Hovers().end())));
    }
    return list;
}

// True when a user.css of the kit (the theme's or a snippet's) reads the artwork's colours, as the worker checks.
bool KitNamesArtwork(const fs::path& kit) {
    std::error_code error;
    for (const char* root : {"themes", "snippets"}) {
        for (const auto& entry : fs::directory_iterator(kit / root, error)) {
            std::optional<std::string> css = ReadText(entry.path() / "user.css");
            if (css && (css->find("--art-") != std::string::npos || css->find("--page-art-") != std::string::npos)) {
                return true;
            }
        }
    }
    return false;
}

}  // namespace

std::optional<std::map<std::string, std::string>> ArtworkVariablesOf(std::string_view text, std::string* problem) {
    std::vector<Rgb> colours;
    std::string token;
    auto take = [&]() {
        if (token.empty()) return true;
        std::string hex = token.starts_with('#') ? token.substr(1) : token;
        token.clear();
        if (hex.size() != 6 || hex.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) return false;
        const unsigned long value = std::strtoul(hex.c_str(), nullptr, 16);
        colours.push_back({static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 8),
                           static_cast<uint8_t>(value)});
        return true;
    };
    for (char c : std::string(text) + "\n") {
        if (c != ',' && c != ';' && !std::isspace(static_cast<unsigned char>(c))) {
            token += c;
            continue;
        }
        std::string was = token;
        if (!take()) {
            if (problem) *problem = "--artwork: \"" + was + "\" is not a colour (#RRGGBB)";
            return std::nullopt;
        }
    }
    if (colours.empty() || colours.size() > 3) {
        if (problem) *problem = "--artwork needs one to three colours (art-1, art-2, art-3), such as #204080,#40C0FF";
        return std::nullopt;
    }
    ExtractedColors extracted;
    extracted.prominent = colours[0];
    extracted.vibrant = colours.size() > 1 ? colours[1] : colours[0];
    extracted.darkVibrant = colours.size() > 2 ? colours[2] : extracted.vibrant;
    std::map<std::string, std::string> variables = ArtworkVariables(extracted);
    std::map<std::string, std::string> page;
    for (const auto& [name, value] : variables) page["page-" + name] = value;
    variables.merge(page);
    return variables;
}

namespace {

std::unique_ptr<ReplayBackend> LoadTree(const std::string& dump, const ReplayRequest& request, std::string* problem) {
    std::string why;
    std::unique_ptr<ReplayBackend> tree = ReplayBackend::Load(dump, &why, request.asRecorded);
    if (!tree) {
        if (problem) *problem = Utf8(request.tree) + ": " + why;
        return nullptr;
    }
    if (request.window) tree->SetWindow(*request.window);
    return tree;
}

// One tree and the theme service's pieces that drive it, with the log they write.
struct Bench {
    std::unique_ptr<ReplayBackend> tree;
    std::stringstream log;
    Journal journal{log, [] { return std::string(); }};
    ThemeWorker worker;
    ThemeSession session{journal};

    Bench(std::unique_ptr<ReplayBackend> loaded, const fs::path& kit, const ReplayRequest& request)
        : tree(std::move(loaded)), worker(kit) {
        worker.SetArtwork(request.artwork);
    }

    std::vector<std::string> Lines() {
        std::vector<std::string> lines;
        std::stringstream copy(log.str());
        for (std::string line; std::getline(copy, line);) lines.push_back(line.starts_with("[] ") ? line.substr(3) : line);
        return lines;
    }
};

std::string Title(const std::string& id, const ReplayRequest& request) {
    return (id.empty() ? std::string("no theme") : id) + " on " + Utf8(request.tree.filename());
}

}  // namespace

std::string LeftoversJson(ReplayBackend& tree, ReplayBackend& fresh) {
    return Lines(Leftovers(tree, fresh));
}

std::optional<ReplayOutcome> Replay(const ReplayRequest& request, std::string* problem) {
    std::optional<std::string> dump = ReadText(request.tree);
    if (!dump) {
        if (problem) *problem = "cannot read " + Utf8(request.tree);
        return std::nullopt;
    }
    std::unique_ptr<ReplayBackend> tree = LoadTree(*dump, request, problem);
    if (!tree) return std::nullopt;

    bool ok = true;
    Settings base = ReplaySettings(request, problem, ok);
    if (!ok) return std::nullopt;
    const fs::path kit = fs::absolute(request.out) / "kit";
    std::optional<KitIds> ids = PrepareKit(request, kit, problem);
    if (!ids) return std::nullopt;
    const Settings settings = StateSettings(base, ids->theme, ids->snippets, *ids);

    std::vector<std::string> own;
    const bool artworkAccent = IsArtworkAccent(settings.Get("theme.accent").value_or(""));
    if (request.artwork.empty() && (artworkAccent || KitNamesArtwork(kit))) {
        own.push_back("no artwork given: what reads its colours (var(--art-1)..., theme.accent = artwork) takes its "
                      "fallback, as before the first song (give --artwork)");
    }

    Bench bench(std::move(tree), kit, request);
    Run run = RunPasses(bench.worker, settings, request, bench.session, *bench.tree);
    ReplayOutcome outcome;
    if (!request.then) {
        outcome.passes = static_cast<int>(run.passes.size());
        outcome.settled = run.settled;
        outcome.report = KeepPathsOut(
            ReportJson(request, ids->theme, *bench.tree, bench.session, run, bench.Lines(), own), kit);
        outcome.wireframe = KeepPathsOut(Wireframe(*bench.tree, Title(ids->theme, request)), kit);
        return outcome;
    }

    // The second state on the same tree, then on a fresh one: what differs is what the first left behind.
    const Settings second = StateSettings(base, ids->thenTheme, ids->thenSnippets, *ids);
    const int fromPass = static_cast<int>(run.passes.size()) + 1;
    Run after = RunPasses(bench.worker, second, request, bench.session, *bench.tree, fromPass);
    Bench fresh(LoadTree(*dump, request, problem), kit, request);
    Run clean = RunPasses(fresh.worker, second, request, fresh.session, *fresh.tree);
    const Json::Array leftovers = Leftovers(*bench.tree, *fresh.tree);
    if (!run.settled) own.push_back("the first state did not settle before the second replaced it");

    Removal removal{ids->theme, &run, &leftovers, fromPass};
    outcome.passes = static_cast<int>(after.passes.size());
    outcome.settled = run.settled && after.settled;
    outcome.leftovers = static_cast<int>(leftovers.size());
    outcome.report = KeepPathsOut(
        ReportJson(request, ids->thenTheme, *bench.tree, bench.session, after, bench.Lines(), own, &removal), kit);
    outcome.wireframe = KeepPathsOut(Wireframe(*bench.tree, Title(ids->thenTheme, request)), kit);
    outcome.freshReport = KeepPathsOut(
        ReportJson(request, ids->thenTheme, *fresh.tree, fresh.session, clean, fresh.Lines(), own), kit);
    outcome.freshWireframe = KeepPathsOut(Wireframe(*fresh.tree, Title(ids->thenTheme, request) + ", fresh"), kit);
    return outcome;
}

}  // namespace ampify::theme
