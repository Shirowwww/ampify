#include "commands.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <span>
#include <sstream>
#include <thread>

#include "../core/apple_music.hpp"
#include "../core/i18n.hpp"
#include "../core/kit_layout.hpp"
#include "../core/launch_flags.hpp"
#include "../core/midi_map.hpp"
#include "../core/permissions.hpp"
#include "../core/remote_protocol.hpp"
#include "../core/settings_catalog.hpp"
#include "theme_commands.hpp"
#include "theme_replay.hpp"
#include "../core/theme/files.hpp"
#include "../core/theme/scheme_choice.hpp"
#include "../core/theme/theme.hpp"
#include "../core/theme/accent.hpp"
#include "../core/theme/theme_settings.hpp"
#include "../core/ui_render/mods.hpp"
#include "archive.hpp"
#include "catalog.hpp"
#include "element.hpp"
#include "internal.hpp"
#include "kit.hpp"
#include "messages.hpp"
#include "platform.hpp"
#include "release_facts.hpp"

namespace ampify::cli {

namespace {

// Element lists of the settings that accept `ampify config <list> +id` and `-id`: the folders of the kinds switched
// by an enabled key (a theme is chosen with theme.active instead).
std::string_view KindOfList(std::string_view list) {
    std::optional<ElementKind> kind = KindOfFolder(list);
    return kind && *kind != ElementKind::Theme ? ToString(*kind) : std::string_view();
}

// The fixed keys, then the families, for the message that refuses an unknown key.
std::string KnownSettingNames() {
    std::string names;
    for (const KitSetting& setting : FixedKitSettings()) names += (names.empty() ? "" : ", ") + std::string(setting.key);
    for (const KitSetting& family : KitSettingFamilies()) names += ", " + std::string(family.key) + "...";
    return names;
}

// The keys whose value has a format of its own (an id, a colour, an address...); nothing for the others.
std::optional<bool> CheckOwnFormat(const std::string& key, const std::string& value, std::string& problem) {
    auto refuse = [&problem](std::string text) {
        problem = std::move(text);
        return false;
    };
    if (key == kThemeActiveKey) return value.empty() || IsElementId(value) || refuse("invalid theme id: " + value);
    if (key == "theme.scheme") return value.empty() || IsSchemeName(value) || refuse("invalid scheme name: " + value);
    if (key == "theme.schedule") {
        theme::Schedule schedule = theme::ParseSchedule(value);
        return schedule.problems.empty() || refuse(schedule.problems.front() + " (empty: no schedule)");
    }
    if (key == "theme.accent") {
        return value.empty() || theme::ParseAccent(value) || theme::IsArtworkAccent(value) ||
               refuse("theme.accent expects a colour as RRGGBB or #RRGGBB, such as FA2D48, or artwork (empty: the theme's own accent)");
    }
    if (key == "catalog.url") {
        return IsHttpsAddress(value, kMaxAddressLength) || value.rfind("file://", 0) == 0 ||
               refuse("catalog.url must start with https:// (or file:// for a local catalog)");
    }
    if (key == "language") {
        // Two letters, then optional subtags of 2 to 8 letters or digits: de, pt-BR, zh-Hant-TW. Empty follows Windows.
        static const std::regex kLanguage("[A-Za-z]{2}([-_][A-Za-z0-9]{2,8})*");
        return value.empty() || std::regex_match(value, kLanguage) ||
               refuse("language expects a language code such as de, pt-BR or zh-Hant (empty: the Windows language)");
    }
    if (key == kLaunchFlagsKey) return SplitLaunchFlags(value, &problem).has_value();
    if (key == kRemoteTokenKey) {
        return value.empty() || remote::IsValidToken(value) ||
               refuse("remote.token expects 32 to 128 characters among A-Z a-z 0-9 - _ (empty: a token is created)");
    }
    if (key == "midi.map") return midi::Map().Parse(value, &problem);
    return std::nullopt;
}

bool CheckInteger(const std::string& key, const std::string& value, std::string& problem) {
    const KitSetting& row = *FindKitSetting(key);
    int low = static_cast<int>(row.low);
    int high = static_cast<int>(row.high);
    int number = -1;
    auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), number);
    if (error == std::errc() && end == value.data() + value.size() && number >= low && number <= high) return true;
    problem = key + " expects a number from " + std::to_string(low) + " to " + std::to_string(high);
    return false;
}

bool CheckNumber(const std::string& key, const std::string& value, std::string& problem) {
    const KitSetting& row = *FindKitSetting(key);
    double number = 0;
    const char* end = value.data() + value.size();
    auto [stop, error] = std::from_chars(value.data(), end, number);
    if (!value.empty() && error == std::errc() && stop == end && number >= row.low && number <= row.high) return true;
    problem = key + " expects a number from " + ShortestNumberText(row.low) + " to " + ShortestNumberText(row.high);
    return false;
}

// Checks a value before it is written. `stored` receives the form that goes into settings.txt.
bool CheckSetting(const std::string& key, const std::string& value, std::string& stored, std::string& problem) {
    stored = value;
    std::optional<SettingKind> kind = KindOfSetting(key);
    if (!kind) {
        problem = "unknown setting: " + key + " (known: " + KnownSettingNames() + ")";
        return false;
    }
    if (std::optional<bool> checked = CheckOwnFormat(key, value, problem)) return *checked;
    switch (*kind) {
        case SettingKind::Toggle: {
            std::optional<bool> on = ParseToggle(value);
            if (!on) problem = key + " expects 1 or 0 (true, false, on, off, yes, no)";
            if (on) stored = *on ? "1" : "0";
            return on.has_value();
        }
        case SettingKind::Choice:
            if (value.empty() || IsOfferedValue(key, value)) return true;
            problem = key + " expects one of: " + OfferedValues(key) + " (empty: the default)";
            return false;
        case SettingKind::Integer: return CheckInteger(key, value, problem);
        case SettingKind::Number: return CheckNumber(key, value, problem);
        case SettingKind::Text: return true;
    }
    return true;
}

// After an element is switched: an extension or an app starts or stops at once in a running kernel (reload.txt);
// a theme or a snippet follows settings.txt, which the kernel watches.
int AnnounceSwitch(Context& ctx, std::string_view kind, const std::string& id, bool enable) {
    if (kind != "extension" && kind != "app") {
        ctx.out << Tr("applyHint") << "\n";
        return 0;
    }
    RequestReload(ctx, {std::string(enable ? "start " : "stop ") + ReloadTarget(kind, id)});
    ctx.out << "If Apple Music is running with the kernel, the change applies right away. Otherwise, at the next start.\n";
    return 0;
}

// `ampify config extensions +id` and `-id`. Enabling needs an element that is installed and valid, or built-in.
int ToggleElement(Context& ctx, Settings& settings, std::string_view list, const std::string& value) {
    std::string_view kind = KindOfList(list);
    std::string id = value.substr(1);
    bool enable = value[0] == '+';
    if (!IsElementId(id)) return Failure(ctx, Tr("invalidId", {{"id", id}}));

    if (enable) {
        bool builtin = kind == "extension" && IsBuiltInExtension(id);
        bool installed = false;
        for (const InstalledElement& element : ListInstalled(ctx.kit)) {
            if (element.kind == kind && element.id == id) installed = element.Valid();
        }
        if (!builtin && !installed) return Failure(ctx, "element missing or invalid: " + std::string(kind) + " " + id);
    }

    SetEnabled(settings, kind, id, enable);
    std::string error;
    if (!SaveSettings(ctx, settings, error)) return Failure(ctx, error);
    ctx.out << std::string(list) << " " << value << ": " << Tr(enable ? "state.on" : "state.off") << "\n";
    return AnnounceSwitch(ctx, kind, id, enable);
}

int CmdConfigSet(Context& ctx, Settings& settings) {
    const std::vector<std::string>& args = ctx.options.positional;
    const std::string& key = args[1];
    const std::string& value = args[2];

    if (!KindOfList(key).empty() && !value.empty() && (value[0] == '+' || value[0] == '-')) {
        return ToggleElement(ctx, settings, key, value);
    }

    std::string stored;
    std::string problem;
    if (!CheckSetting(key, value, stored, problem)) return Failure(ctx, problem);
    if (!settings.Set(key, stored)) return Failure(ctx, "value must fit on one line: " + key);
    bool tokenCreated = FillDerivedSettings(settings, key);
    std::string error;
    if (!SaveSettings(ctx, settings, error)) return Failure(ctx, error);
    ctx.out << key << " = " << stored << "\n";
    if (tokenCreated) ctx.out << Tr("remote.tokenCreated") << "\n";
    return 0;
}

// The state shown in `ampify list`.
std::string StateOf(const Settings& settings, const InstalledElement& element) {
    bool enabled = IsEnabled(settings, element.kind, element.id);
    if (!element.Valid()) return Tr("state.invalid", {{"error", element.errors.front()}});
    if (element.kind == "theme") return Tr(enabled ? "state.active" : "state.inactive");
    return Tr(enabled ? "state.on" : "state.off");
}

void PrintHeader(Context& ctx) {
    ctx.out << Pad(Tr("list.type"), 11) << Pad(Tr("list.id"), 26) << Pad(Tr("list.version"), 11) << Tr("list.state")
            << "\n";
}

int PrintElements(Context& ctx, std::string_view filter) {
    Settings settings;
    std::string error;
    if (!LoadSettings(ctx, settings, error)) return Failure(ctx, error);

    bool any = false;
    PrintHeader(ctx);
    for (const KitElement& element : kKitElements) {
        if (!IsBuiltInExtension(element.id) || (!filter.empty() && filter != "extension")) continue;
        ctx.out << Pad("extension", 11) << Pad(element.id, 26) << Pad(Tr("list.builtin"), 11)
                << Tr(IsEnabled(settings, "extension", element.id) ? "state.on" : "state.off") << " "
                << Tr("list.inKernel") << "\n";
        any = true;
    }
    for (const InstalledElement& element : ListInstalled(ctx.kit)) {
        if (!filter.empty() && element.kind != filter) continue;
        std::string version = element.manifest ? element.manifest->version : "?";
        ctx.out << Pad(element.kind, 11) << Pad(element.id, 26) << Pad(version, 11) << StateOf(settings, element)
                << "\n";
        any = true;
    }
    if (!any) ctx.out << Tr("list.empty") << "\n";
    return 0;
}

int SetElementState(Context& ctx, std::string_view kind, const std::string& id, bool enable) {
    if (!IsElementId(id)) return Failure(ctx, Tr("invalidId", {{"id", id}}));

    Settings settings;
    std::string error;
    if (!LoadSettings(ctx, settings, error)) return Failure(ctx, error);

    std::string permissions;
    if (enable) {
        bool builtin = kind == "extension" && IsBuiltInExtension(id);
        if (!builtin) {
            const InstalledElement* found = nullptr;
            std::vector<InstalledElement> elements = ListInstalled(ctx.kit);
            for (const InstalledElement& element : elements) {
                if (element.kind == kind && element.id == id) found = &element;
            }
            if (found == nullptr) {
                return Failure(ctx, Tr("element.notFound", {{"kind", kind}, {"id", id}}));
            }
            if (!found->Valid()) return Failure(ctx, Tr("element.invalid", {{"error", found->errors.front()}}));
            for (const std::string& permission : found->manifest->permissions) {
                permissions += (permissions.empty() ? "" : ", ") + permission;
                // Switching the element on is the user's consent to the service it needs, as on Ampify's settings page.
                if (const char* key = ServiceSettingOf(permission)) settings.Set(key, "1");
            }
        }
    }

    SetEnabled(settings, kind, id, enable);
    if (!SaveSettings(ctx, settings, error)) return Failure(ctx, error);

    ctx.out << Tr("element.toggled", {{"kind", kind}, {"id", id}, {"state", Tr(enable ? "state.on" : "state.off")}})
            << "\n";
    if (enable && !permissions.empty()) ctx.out << Tr("element.permissions", {{"permissions", permissions}}) << "\n";
    return AnnounceSwitch(ctx, kind, id, enable);
}

int CmdThemeSet(Context& ctx) {
    const std::vector<std::string>& args = ctx.options.positional;
    if (args.size() < 3) return Usage(ctx, "theme set needs a theme id");
    const std::string& id = args[2];
    if (!IsElementId(id)) return Failure(ctx, Tr("invalidId", {{"id", id}}));

    const InstalledElement* found = nullptr;
    std::vector<InstalledElement> elements = ListInstalled(ctx.kit);
    for (const InstalledElement& element : elements) {
        if (element.kind == "theme" && element.id == id) found = &element;
    }
    if (found == nullptr) return Failure(ctx, "theme not found: " + id + ". Install it with ampify install " + id);
    if (!found->Valid()) return Failure(ctx, "invalid theme: " + found->errors.front());

    Settings settings;
    std::string error;
    if (!LoadSettings(ctx, settings, error)) return Failure(ctx, error);
    // A scheme belongs to the theme it was chosen for: kept, the Dark of one theme opened the next one on its Dark.
    // Setting the same theme again keeps it, and so does "auto", which follows Windows whatever the theme.
    std::string stored = settings.GetString("theme.scheme", "");
    bool keep = !ctx.options.scheme && !stored.empty() &&
                (stored == "auto" || ActiveThemeId(settings) == id);

    // The manifest is valid; now the theme engine reads the theme as the kernel will, and refuses what it would refuse.
    std::filesystem::path folder = ctx.kit.Root() / "themes" / id;
    theme::FolderFiles files(folder);
    theme::LoadOptions load;
    load.scheme = ctx.options.scheme ? *ctx.options.scheme
                  : keep && stored != "auto" ? stored
                                             : theme::ReadDefaultScheme(files);
    theme::Theme loaded = theme::LoadTheme(id, folder.generic_string(), files, load);
    if (!loaded.usable) {
        return Failure(ctx, "theme refused by the theme engine: " +
                                (loaded.problems.empty() ? std::string("unknown reason") : loaded.problems.front()));
    }
    for (const std::string& problem : loaded.problems) ctx.out << "Warning: " << problem << "\n";

    settings.Set(kThemeActiveKey, id);
    if (ctx.options.scheme) {
        if (!IsSchemeName(*ctx.options.scheme)) return Failure(ctx, "invalid scheme name: " + *ctx.options.scheme);
        settings.Set("theme.scheme", *ctx.options.scheme);
    } else if (!keep) {
        settings.Remove("theme.scheme");
    }
    if (!SaveSettings(ctx, settings, error)) return Failure(ctx, error);

    ctx.out << "Theme " << id << " selected";
    if (ctx.options.scheme) ctx.out << " (scheme " << *ctx.options.scheme << ")";
    ctx.out << ". " << Tr("applyHint") << "\n";
    return 0;
}

constexpr std::string_view kStarterAuthor = "your-github-username";
constexpr std::string_view kStarterDescription = "Describe it here.";

// The files that `ampify new` writes for each kind. Placeholders: the id and the author.
std::map<std::string, std::string> Template(std::string_view kind, const std::string& id) {
    const std::string author(kStarterAuthor);
    std::string common = "  \"id\": \"" + id + "\",\n  \"name\": \"" + id + "\",\n  \"version\": \"0.1.0\",\n  \"kind\": \"" +
                         std::string(kind) + "\",\n  \"author\": \"" + author + "\",\n  \"description\": \"" +
                         std::string(kStarterDescription) + "\",\n" + "  \"kitApi\": \"" + std::string(kKitApi) + "\"";
    if (kind == "extension") {
        // Starters show something at once in Apple Music. The guides (docs/extensions/guide.md,
        // docs/themes/guide.md) walk through these same files.
        return {
            {"manifest.json", "{\n" + common + ",\n  \"main\": \"main.js\",\n"
                              "  \"permissions\": [\"ui.playbar\", \"player.read\", \"notifications\"],\n"
                              "  \"settings\": [\n"
                              "    {\"key\": \"show-album\", \"label\": \"Show the album\", \"type\": \"toggle\", "
                              "\"default\": true}\n  ]\n}\n"},
            {"main.js",
             "// " + id + ": a button in the playback bar that tells the song playing.\n"
             "// Save a change: with ampify dev, Apple Music restarts the extension at once.\n"
             "// show-album is declared in manifest.json: Ampify's settings page switches it.\n\n"
             "Ampify.ui.playbar.addButton({\n"
             "  label: \"What is playing?\",\n"
             "  icon: \"\\uE946\", // a Segoe Fluent Icons glyph (Info)\n"
             "  onClick: () => {\n"
             "    const { track } = Ampify.player.state();\n"
             "    const album = Ampify.config.get(\"show-album\") && track ? \" - \" + track.album : \"\";\n"
             "    Ampify.notify({\n"
             "      title: track ? track.title : \"Nothing is playing\",\n"
             "      text: track ? track.artist + album : \"Start a song in Apple Music.\",\n"
             "    });\n"
             "  },\n"
             "});\n\n"
             "Ampify.log(\"" + id + " started\");\n"
             "Ampify.onStop(() => Ampify.log(\"" + id + " stopped\"));\n"},
        };
    }
    if (kind == "theme") {
        // A starter that already looks finished: two schemes in Spicetify's names, rounded surfaces, a hover.
        return {
            {"manifest.json", "{\n" + common + ",\n  \"colors\": \"colors.ini\",\n  \"defaultScheme\": \"Night\"\n}\n"},
            {"colors.ini",
             "; The colors, one scheme per section. The names are Spicetify's:\n"
             ";   main     page background        sidebar  sidebar             player  playback bar\n"
             ";   card     now playing display    text     text                subtext secondary text\n"
             ";   button   accent (buttons, progress bar)\n"
             "; Change a value and save: with ampify dev, Apple Music follows right away.\n\n"
             "[Night]\nmain = 16131F\nsidebar = 1D1928\nplayer = 1D1928\ncard = 2A2438\nmain-elevated = 2A2438\n"
             "text = F3EEFF\nsubtext = B5ABCB\nbutton = FF7A85\nplayback-bar = FF7A85\nhighlight = 2F2840\n\n"
             "[Day]\nmain = FBF7F2\nsidebar = F2ECE4\nplayer = F2ECE4\ncard = FFFFFF\nmain-elevated = FFFFFF\n"
             "text = 1F1A2B\nsubtext = 6A6278\nbutton = E0525F\nplayback-bar = E0525F\nhighlight = EDE5DA\n"},
            {"user.css",
             "/* " + id + ": shapes and effects. The colors come from colors.ini ($main, $button...).\n"
             "   The names (.sidebar, .lcd...) are in docs/themes/names.md; ampify devtools shows the whole tree. */\n\n"
             ":root {\n    --radius: 12px;\n}\n\n"
             "/* Sidebar: rounded on the content side. */\n.sidebar {\n"
             "    corner-radius: 0 var(--radius) var(--radius) 0;\n}\n\n"
             "/* The now playing display, in the middle of the playback bar. */\n"
             ".lcd {\n    corner-radius: var(--radius);\n}\n\n"
             "/* The playback buttons, round. */\n.transport AppBarButton {\n"
             "    corner-radius: 50;\n}\n\n"
             "/* The artwork grows a little on hover. */\n.lcd-artwork-image {\n"
             "    transform-origin: center;\n}\n\n.lcd-artwork-image:hover {\n    transform: scale(1.08);\n}\n"},
        };
    }
    if (kind == "snippet") {
        return {
            {"manifest.json", "{\n" + common + "\n}\n"},
            {"user.css",
             "/* " + id + ": a small style switched on alone (ampify snippet enable " + id + "), on top of any theme.\n"
             "   It sees the theme's colors ($button...) and comes after it. */\n\n"
             "/* Rounder artwork on the cards. */\n.card .artwork {\n    corner-radius: 16;\n}\n"},
        };
    }
    return {
        {"manifest.json", "{\n" + common + ",\n  \"main\": \"main.js\",\n  \"icon\": \"icon.svg\",\n"
                          "  \"permissions\": [\"ui.page\", \"ui.sidebar\"]\n}\n"},
        {"main.js",
         "// " + id + ": a page of its own, opened from an entry at the bottom of Apple Music's sidebar.\n"
         "// Save a change: with ampify dev, Apple Music restarts the app at once.\n\n"
         "const { h, useState } = Ampify.React;\n\n"
         "function Page() {\n"
         "  const [count, setCount] = useState(0);\n"
         "  return h(\"stack\", { gap: 12 },\n"
         "    h(\"text\", { variant: \"subtitle\" }, \"Your page: edit main.js to change it.\"),\n"
         "    h(\"text\", null, \"Clicked \" + count + \" time(s).\"),\n"
         "    h(\"button\", { label: \"Click me\", variant: \"primary\", onClick: () => setCount(count + 1) }));\n"
         "}\n\n"
         "// Ampify shows what render() returns on the page /" + id + ", under the name of the manifest.\n"
         "globalThis.render = () => h(Page);\n\n"
         "Ampify.ui.sidebar.addItem({\n"
         "  label: \"" + id + "\",\n"
         "  icon: \"\\uE71D\", // a Segoe Fluent Icons glyph (AllApps)\n"
         "  onClick: () => Ampify.ui.navigate(\"/" + id + "\"),\n"
         "});\n"},
        {"icon.svg", "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\"><path d=\"M12 2l10 20H2z\"/></svg>\n"},
    };
}

}  // namespace

std::optional<Options> ParseOptions(const std::vector<std::string>& arguments, std::string& error) {
    Options options;
    auto value = [&](size_t& index) -> std::optional<std::string> {
        if (index + 1 >= arguments.size()) {
            error = arguments[index] + " expects a value";
            return std::nullopt;
        }
        return arguments[++index];
    };
    auto number = [&](const std::string& text, int& target) {
        int parsed = 0;
        const char* first = text.data();
        const char* last = first + text.size();
        auto [end, status] = std::from_chars(first, last, parsed);
        if (status != std::errc() || end != last || parsed < 0) {
            error = "number expected: " + text;
            return false;
        }
        target = parsed;
        return true;
    };

    bool onlyArguments = false;
    for (size_t i = 0; i < arguments.size(); i++) {
        const std::string& arg = arguments[i];
        // After "--", and for the value of `config app_launch_flags`, a word starting with "--" is an argument:
        // `ampify config app_launch_flags --remote-debugging-port=9222` stores the flag instead of refusing it.
        bool flagsValue = options.positional.size() == 2 && options.positional[0] == "config" &&
                          options.positional[1] == kLaunchFlagsKey;
        // `theme replay`, `theme diff` and `theme scrub` take options of their own (--tree, --out, --set...).
        flagsValue = flagsValue || (options.positional.size() >= 2 && options.positional[0] == "theme" &&
                                    (options.positional[1] == "replay" || options.positional[1] == "diff" ||
                                     options.positional[1] == "scrub"));
        if (onlyArguments || flagsValue) {
            options.positional.push_back(arg);
            continue;
        }
        if (arg == "--") {
            onlyArguments = true;
            continue;
        }
        if (arg == "--kit-dir" || arg == "--catalog" || arg == "--install-dir" || arg == "--release-url" ||
            arg == "--scheme" || arg == "--backup" || arg == "--interval" || arg == "--max-cycles") {
            std::optional<std::string> text = value(i);
            if (!text) return std::nullopt;
            if (arg == "--kit-dir") options.kitDir = *text;
            else if (arg == "--catalog") options.catalog = *text;
            else if (arg == "--install-dir") options.installDir = *text;
            else if (arg == "--release-url") options.releaseUrl = *text;
            else if (arg == "--scheme") options.scheme = *text;
            else if (arg == "--backup") options.backup = *text;
            else if (arg == "--interval") {
                if (!number(*text, options.intervalMs)) return std::nullopt;
            } else if (!number(*text, options.maxCycles)) {
                return std::nullopt;
            }
        } else if (arg == "--dry-run") {
            options.dryRun = true;
        } else if (arg == "--yes" || arg == "-y") {
            options.yes = true;
        } else if (arg == "--delete-data") {
            options.deleteData = true;
        } else if (arg == "--check") {
            options.check = true;
        } else if (arg == "--apply") {
            options.apply = true;
        } else if (arg == "--auto") {
            options.autoUpdate = true;
        } else if (arg == "--elements") {
            options.elements = true;
        } else if (arg == "--follow") {
            options.follow = true;
        } else if (arg == "--json") {
            options.json = true;
        } else if (arg == "--tree") {
            options.tree = true;
        } else if (arg == "--dir") {
            options.dir = true;
        } else if (arg == "--help" || arg == "-h") {
            options.positional.insert(options.positional.begin(), "help");
        } else if (arg == "--version") {
            options.positional.insert(options.positional.begin(), "version");
        } else if (arg.size() > 2 && arg.compare(0, 2, "--") == 0) {
            error = Tr("unknownOption", {{"option", arg}});
            return std::nullopt;
        } else {
            // Element ids start with a letter or a digit, so "-id" from `config extensions -id` is an argument.
            options.positional.push_back(arg);
        }
    }
    return options;
}

int Failure(Context& ctx, const std::string& message) {
    ctx.err << Tr("prefix") << message << "\n";
    return 1;
}

int Usage(Context& ctx, const std::string& message) {
    ctx.err << Tr("prefix") << message << "\n" << Tr("helpHint") << "\n";
    return 2;
}

std::string Utf8(const std::filesystem::path& path) {
    std::u8string utf8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}

bool LoadSettings(const Context& ctx, Settings& settings, std::string& error) {
    if (!settings.Load(ctx.kit.Settings().string())) {
        error = "cannot read: " + Utf8(ctx.kit.Settings()) + ". Close any program that has it open, then try again.";
        return false;
    }
    return true;
}

bool SaveSettings(const Context& ctx, const Settings& settings, std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(ctx.kit.Root(), ec);
    std::string detail;  // the message below names the file in UTF-8, as every other message does
    if (!settings.SaveChanges(ctx.kit.Settings().string(), detail)) {
        error = "cannot write: " + Utf8(ctx.kit.Settings());
        return false;
    }
    return true;
}

namespace {

// The lines of the general help for one command: those whose first word is the command, with the lines that continue
// them (indented further).
std::string HelpOf(std::string_view help, std::string_view command) {
    std::string found;
    bool inside = false;
    std::istringstream lines{std::string(help)};
    for (std::string line; std::getline(lines, line);) {
        bool entry = line.size() > 2 && line.compare(0, 2, "  ") == 0 && line[2] != ' ';
        if (entry) {
            std::string_view rest = std::string_view(line).substr(2);
            inside = rest.substr(0, rest.find(' ')) == command;
        } else if (line.empty() || line[0] != ' ') {
            inside = false;
        }
        if (inside) found += line + "\n";
    }
    return found;
}

}  // namespace

int CmdHelp(Context& ctx) {
    const std::vector<std::string>& args = ctx.options.positional;
    if (args.size() < 2) {
        ctx.out << Tr("help");
        return 0;
    }
    std::string command = args[1] == "catalog" ? "market" : args[1] == "auto" ? "launch" : args[1];
    std::string lines = HelpOf(Tr("help"), command);
    if (lines.empty()) return Usage(ctx, "no command named " + args[1] + "; ampify help lists them all");
    ctx.out << lines << "\nMore in " << kProjectSite << "/cli.html\n";
    return 0;
}

int CmdVersion(Context& ctx) {
    ctx.out << Tr("version", {{"version", kKitVersion}, {"kitApi", kKitApi}}) << "\n";
    return 0;
}

namespace {

// What `ampify path <name>` prints. Nothing when the path cannot be found (Apple Music on another system).
std::optional<std::filesystem::path> NamedPath(Context& ctx, std::string_view name) {
    if (name == "kit") return ctx.kit.Root();
    if (name == "settings") return ctx.kit.Settings();
    if (name == "extensions" || name == "themes" || name == "apps" || name == "snippets") {
        return ctx.kit.Root() / std::string(name);
    }
    if (name == "logs") return ctx.kit.Journal();
    if (name == "backups") return ctx.kit.Backups();
    if (name == "app") return platform::AppPackageFolder();
    if (name == "prefs") return platform::AppPreferencesFile();
    return std::nullopt;
}

constexpr std::string_view kPathNames[] = {"kit", "settings", "extensions", "themes", "apps", "snippets",
                                           "logs", "backups", "app", "prefs"};

}  // namespace

int CmdPath(Context& ctx) {
    const std::vector<std::string>& args = ctx.options.positional;
    if (args.size() > 2) return Usage(ctx, "path takes at most one name");
    if (args.size() == 2) {
        const std::string& name = args[1];
        bool known = std::find(std::begin(kPathNames), std::end(kPathNames), std::string_view(name)) != std::end(kPathNames);
        if (!known) {
            std::string names;
            for (std::string_view each : kPathNames) names += (names.empty() ? "" : ", ") + std::string(each);
            return Usage(ctx, "unknown name for path: " + name + " (" + names + ")");
        }
        std::optional<std::filesystem::path> path = NamedPath(ctx, name);
        if (!path) {
            if (name == "app") {
                return Failure(ctx, "Apple Music folder not found: Apple Music is not installed, or this system "
                                    "is not Windows.");
            }
            return Failure(ctx, "Apple Music preferences not found on this system.");
        }
        ctx.out << Utf8(*path) << "\n";
        return 0;
    }

    std::optional<std::filesystem::path> app = NamedPath(ctx, "app");
    std::optional<std::filesystem::path> prefs = NamedPath(ctx, "prefs");
    ctx.out << "Ampify folder    : " << Utf8(ctx.kit.Root()) << "\n"
            << "Settings         : " << Utf8(ctx.kit.Settings()) << "\n"
            << "Elements         : " << Utf8(ctx.kit.Root()) << "\\{extensions,themes,apps,snippets}\n"
            << "Ampify state     : " << Utf8(ctx.kit.Active()) << ", " << Utf8(ctx.kit.Reload()) << "\n"
            << "Logs             : " << Utf8(ctx.kit.Journal()) << "\n"
            << "Backups          : " << Utf8(ctx.kit.Backups()) << "\n"
            << "ampify folder    : " << Utf8(platform::ExecutableDirectory()) << "\n"
            << "Apple Music      : " << kAppUserModelId << "\n"
            << "App package      : " << (app ? Utf8(*app) : std::string("not found on this system")) << "\n"
            << "Preferences      : " << (prefs ? Utf8(*prefs) : std::string("not found on this system")) << "\n"
            << "One path only    : ampify path <kit|settings|extensions|themes|apps|snippets|logs|backups|app|prefs>\n";
    return 0;
}

int CmdConfig(Context& ctx) {
    const std::vector<std::string>& args = ctx.options.positional;
    if (ctx.options.dir) return CmdConfigDir(ctx);
    if (args.size() > 3) return Usage(ctx, Tr("tooManyArgs", {{"command", "config"}}));

    Settings settings;
    std::string error;
    if (!LoadSettings(ctx, settings, error)) return Failure(ctx, error);

    if (args.size() == 1) {
        for (const auto& [key, value] : settings.Values()) ctx.out << key << " = " << value << "\n";
        return 0;
    }
    if (args.size() == 2) {
        std::optional<std::string> value = settings.Get(args[1]);
        if (value) {
            ctx.out << *value << "\n";
            return 0;
        }
        const KitSetting* known = FindKitSetting(args[1]);
        if (!known) return Failure(ctx, Tr("missingKey", {{"key", args[1]}}));
        // The value that applies goes to the output, for scripts; that it is a default goes to the error stream.
        ctx.out << known->fallback << "\n";
        ctx.err << args[1] << " is not set: default" << (known->note.empty() ? "" : ", ")
                << std::string(known->note) << "\n";
        return 0;
    }
    return CmdConfigSet(ctx, settings);
}

// `ampify settings <kind> <id> [<key> <value>]`: the settings an element declares, as the Marketplace shows them.
int CmdSettings(Context& ctx) {
    const std::vector<std::string>& args = ctx.options.positional;
    if (args.size() != 3 && args.size() != 5) {
        return Usage(ctx, "settings needs <kind> <id>, or <kind> <id> <key> <value>");
    }
    const std::string& kind = args[1];
    const std::string& id = args[2];
    if (args.size() == 5) {
        std::string error;
        if (!uirender::ChangeElementSetting(ctx.kit.Root(), kind, id, args[3], args[4], error)) return Failure(ctx, error);
        ctx.out << id << " " << args[3] << " = " << args[4] << "\n";
        return 0;
    }
    std::optional<uirender::ModEntry> element = uirender::ElementSettings(ctx.kit.Root(), kind, id);
    if (!element) return Failure(ctx, "no installed " + kind + " \"" + id + "\" declares settings");
    for (size_t index = 0; index < element->settings.size(); index++) {
        ctx.out << element->settings[index].key << " = " << element->values[index] << "\n";
    }
    return 0;
}

int CmdList(Context& ctx) {
    return PrintElements(ctx, "");
}

int CmdKind(Context& ctx, std::string_view kind) {
    const std::vector<std::string>& args = ctx.options.positional;
    std::string_view action = args.size() > 1 ? std::string_view(args[1]) : std::string_view("list");
    if (args.size() > 3) return Usage(ctx, Tr("tooManyArgs", {{"command", kind}}));

    if (action == "list") return PrintElements(ctx, kind);
    if (action == "enable" || action == "disable") {
        if (args.size() < 3) return Usage(ctx, Tr("needsId", {{"action", action}}));
        return SetElementState(ctx, kind, args[2], action == "enable");
    }
    return Usage(ctx, Tr("unknownAction", {{"action", action}}));
}

int CmdTheme(Context& ctx) {
    const std::vector<std::string>& args = ctx.options.positional;
    std::string_view action = args.size() > 1 ? std::string_view(args[1]) : std::string_view("list");

    if (action == "list") return PrintElements(ctx, "theme");
    if (action == "set") return CmdThemeSet(ctx);
    if (action == "import") return CmdThemeImport(ctx);
    if (action == "clear") {
        Settings settings;
        std::string error;
        if (!LoadSettings(ctx, settings, error)) return Failure(ctx, error);
        settings.Set(kThemeActiveKey, "");
        settings.Remove("theme.scheme");
        if (!SaveSettings(ctx, settings, error)) return Failure(ctx, error);
        ctx.out << "No active theme. " << Tr("applyHint") << "\n";
        return 0;
    }
    // names checks the stable names (.sidebar, .card...) against trees saved by the inspector, after an Apple Music update.
    if (action == "names") {
        if (args.size() < 3) return Usage(ctx, "theme names needs one or more inspector.json files");
        std::vector<std::filesystem::path> trees;
        for (size_t i = 2; i < args.size(); i++) trees.push_back(Utf8ToPath(args[i]));
        return theme::RunNames(trees, ctx.out, ctx.err);
    }
    // The offline bench: the engine on a saved tree (they read their own options, ParseOptions passes them through).
    if (action == "replay" || action == "diff" || action == "scrub") {
        return theme::RunReplayCommand(std::vector<std::string>(args.begin() + 1, args.end()), ctx.out, ctx.err);
    }
    // check and watch read a theme folder anywhere on disk, the way the kernel reads it, and print what would apply.
    if (action == "check" || action == "watch") {
        if (args.size() != 3) return Usage(ctx, "theme " + std::string(action) + " needs a theme folder");
        std::vector<std::string> watchArgs{"watch", args[2]};
        if (ctx.options.scheme) watchArgs.insert(watchArgs.end(), {"--scheme", *ctx.options.scheme});
        int polls = action == "check" ? -1 : ctx.options.maxCycles;
        if (polls >= 0) watchArgs.insert(watchArgs.end(), {"--polls", std::to_string(polls)});
        if (action == "check") {
            std::filesystem::path folder = std::filesystem::absolute(Utf8ToPath(args[2]));
            std::error_code error;
            if (!std::filesystem::is_directory(folder, error)) return Failure(ctx, "folder not found: " + args[2]);
            theme::FolderFiles files(folder);
            theme::LoadOptions load;
            if (ctx.options.scheme) load.scheme = *ctx.options.scheme;
            return theme::PrintThemeReport(theme::LoadTheme(Utf8(folder.filename()), folder.generic_string(), files, load),
                                           ctx.out);
        }
        std::chrono::milliseconds interval(std::max(ctx.options.intervalMs, 200));
        return theme::RunWatch(watchArgs, ctx.out, ctx.err, [interval] { std::this_thread::sleep_for(interval); });
    }
    return Usage(ctx, Tr("unknownAction", {{"action", std::string("theme ") + std::string(action)}}));
}

std::vector<std::string> StarterLeftovers(const ElementInfo& manifest) {
    std::vector<std::string> left;
    if (manifest.author == kStarterAuthor) {
        left.push_back("manifest.json: \"author\" is still \"" + std::string(kStarterAuthor) + "\": put your GitHub name");
    }
    if (manifest.description == kStarterDescription) {
        left.push_back("manifest.json: \"description\" is still \"" + std::string(kStarterDescription) +
                       "\": say in a sentence what it does");
    }
    return left;
}

int CmdValidate(Context& ctx) {
    const std::vector<std::string>& args = ctx.options.positional;
    if (args.size() != 2) return Usage(ctx, "validate needs a folder or a catalog.json");

    std::filesystem::path target = Utf8ToPath(args[1]);
    std::error_code ec;
    if (std::filesystem::is_directory(target, ec)) {
        FolderValidation validation = ValidateElementFolder(target);
        if (validation.ok() && validation.errors.empty()) {
            ctx.out << Utf8(target) << ": valid (" << ToString(validation.manifest->kind) << " " << validation.manifest->id
                    << " " << validation.manifest->version << ")\n";
            for (const std::string& warning : validation.warnings) ctx.out << "  warning: " << warning << "\n";
            return 0;
        }
        ctx.err << Utf8(target) << ": invalid\n";
        for (const std::string& problem : validation.errors) ctx.err << "  - " << problem << "\n";
        for (const std::string& warning : validation.warnings) ctx.err << "  warning: " << warning << "\n";
        return 1;
    }

    std::ifstream in(target, std::ios::binary);
    if (!in) return Failure(ctx, "not found: " + args[1] + " (give an element folder or a catalog.json file)");
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::vector<std::string> errors;
    std::optional<Catalog> catalog = ParseCatalog(buffer.str(), false, errors);
    if (!catalog) {
        ctx.err << args[1] << ": invalid\n";
        for (const std::string& problem : errors) ctx.err << "  - " << problem << "\n";
        return 1;
    }
    ctx.out << args[1] << ": valid (" << catalog->entries.size() << " element(s))\n";
    return 0;
}

int CmdNew(Context& ctx) {
    const std::vector<std::string>& args = ctx.options.positional;
    if (args.size() != 3) return Usage(ctx, "new needs a type and an id");
    const std::string& kind = args[1];
    const std::string& id = args[2];
    if (!KindNamed(kind)) return Failure(ctx, "unknown type: " + kind + " (extension, theme, snippet or app)");
    if (!IsElementId(id)) return Failure(ctx, Tr("invalidId", {{"id", id}}));

    std::filesystem::path folder = std::filesystem::current_path() / Utf8ToPath(id);
    std::error_code ec;
    if (std::filesystem::exists(folder, ec)) return Failure(ctx, "the folder already exists: " + Utf8(folder));

    for (const auto& [name, text] : Template(kind, id)) {
        std::string error;
        if (!WriteTextFile(folder / name, text, error)) return Failure(ctx, error);
    }
    ctx.out << "Element created in " << Utf8(folder) << ".\n"
            << "See it live in Apple Music: ampify dev " << id << "\n"
            << "Before sharing it, put your GitHub name and a description in manifest.json\n"
            << "(ampify build and ampify publish refuse the starter's texts), then: ampify publish " << id << "\n";
    return 0;
}

namespace {

// What a command honours besides running.
enum Trait : unsigned {
    kDryRun = 1,  // --dry-run: any other command would run for real, so the option is refused there
    kChain = 2,   // takes no argument and can follow another on a line, as in Spicetify: `ampify backup apply launch`
    kNotice = 4,  // the notice about a new kit release may follow; never a command whose output a script reads
    kAlias = 8,   // another name of the row before it, left out of the lists the messages give
};

int CmdInspectAny(Context& ctx) {
    return ctx.options.tree ? CmdInspectTree(ctx) : CmdInspect(ctx);
}

struct Command {
    std::string_view name;
    int (*run)(Context& ctx);
    unsigned traits = 0;
};

// Every command, once. The order is the one the --dry-run message lists them in.
constexpr Command kCommands[] = {
    {"help", CmdHelp},
    {"version", CmdVersion, kChain},
    {"path", CmdPath},
    {"config", CmdConfig},  // --dry-run with --dir only (SupportsDryRun)
    {"list", CmdList, kNotice},
    {"settings", CmdSettings},
    {"theme", CmdTheme},
    {"validate", CmdValidate},
    {"new", CmdNew},
    {"market", CmdMarket, kNotice},
    {"catalog", CmdMarket, kNotice | kAlias},
    {"install", CmdInstall},
    {"remove", CmdRemove},
    {"update", CmdUpdate},
    {"apply", CmdApply, kDryRun | kChain | kNotice},
    {"launch", CmdLaunch, kDryRun | kChain | kNotice},
    {"auto", CmdLaunch, kDryRun | kChain | kNotice | kAlias},
    {"restore", CmdRestore, kDryRun | kChain},
    {"uninstall", CmdUninstall, kDryRun},
    {"import", CmdImport, kDryRun},
    {"backup", CmdBackup, kChain},
    {"watch", CmdWatch},
    {"build", CmdBuild},
    {"publish", CmdPublish, kDryRun},
    {"inspect", CmdInspectAny},
    {"devtools", CmdDevtools, kDryRun | kChain},
    {"versions", CmdVersions},
    {"notice", CmdNotice, kDryRun},
    {"install-kit", CmdInstallKit},
    {"types", CmdTypes},
    {"dev", CmdDev},
    {"logs", CmdLogs},
    {"doctor", CmdDoctor, kChain | kNotice},
    {"report", CmdReport},
    {"profile", CmdProfile},
    {"export", CmdExport},
    {"test", CmdTest},
};

const Command* FindCommand(std::string_view name) {
    for (const Command& command : kCommands) {
        if (command.name == name) return &command;
    }
    return nullptr;
}

bool Has(std::string_view name, Trait trait) {
    const Command* command = FindCommand(name);
    return command && (command->traits & trait) != 0;
}

bool SupportsDryRun(const Options& options) {
    const std::string& command = options.positional[0];
    if (command == "config") return options.dir;
    return Has(command, kDryRun);
}

// "apply, launch, ..., notice and config --dir": the commands that take --dry-run, for its refusal.
std::string DryRunCommandList() {
    std::string list;
    for (const Command& command : kCommands) {
        if ((command.traits & kDryRun) && !(command.traits & kAlias)) list += std::string(command.name) + ", ";
    }
    list.resize(list.size() - 2);
    return list + " and config --dir";
}

int Dispatch(Context& ctx) {
    const Options& options = ctx.options;
    if (options.positional.empty()) return CmdHelp(ctx);
    const std::string& command = options.positional[0];
    if (options.dryRun && command != "help" && !SupportsDryRun(options)) {
        return Usage(ctx, "--dry-run is not available for " + command + ": run it without --dry-run (it works with " +
                              DryRunCommandList() + ")");
    }
    if (const Command* known = FindCommand(command)) return known->run(ctx);
    // `ampify theme` was answered above: the other kinds share one set of verbs.
    if (std::optional<ElementKind> kind = KindNamed(command)) return CmdKind(ctx, ToString(*kind));
    return Usage(ctx, Tr("unknownCommand", {{"command", command}}));
}

}  // namespace

bool IsChainLine(const std::vector<std::string>& positional) {
    if (positional.size() < 2) return false;
    for (const std::string& word : positional) {
        if (!Has(word, kChain)) return false;
    }
    return true;
}

int Execute(const Options& options, std::istream& in, std::ostream& out, std::ostream& err) {
    KitLayout kit(options.kitDir ? Utf8ToPath(*options.kitDir) : platform::DefaultKitDirectory());
    std::vector<std::vector<std::string>> commands;
    if (IsChainLine(options.positional)) {
        for (const std::string& verb : options.positional) commands.push_back({verb});
    } else {
        commands.push_back(options.positional);
    }

    int code = 0;
    bool notice = false;
    for (size_t i = 0; i < commands.size(); i++) {
        Options one = options;
        one.positional = commands[i];
        Context ctx{one, kit, in, out, err};
        if (!one.positional.empty() && Has(one.positional[0], kNotice)) notice = true;
        code = Dispatch(ctx);
        if (code != 0) {
            if (i + 1 < commands.size()) {
                err << Tr("prefix") << Tr("chainStopped", {{"command", commands[i][0]}}) << "\n";
            }
            break;
        }
    }
    if (notice) {
        Context ctx{options, kit, in, out, err};
        NotifyKitUpdate(ctx);
    }
    return code;
}

}  // namespace ampify::cli
