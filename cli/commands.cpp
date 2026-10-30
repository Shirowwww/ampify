#include "commands.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <span>
#include <sstream>
#include <thread>

#include "../core/apple_music.hpp"
#include "../core/i18n.hpp"
#include "../core/launch_flags.hpp"
#include "../core/midi_map.hpp"
#include "../core/remote_protocol.hpp"
#include "../core/theme/commands.hpp"
#include "../core/theme/files.hpp"
#include "../core/theme/theme.hpp"
#include "archive.hpp"
#include "catalog.hpp"
#include "element.hpp"
#include "internal.hpp"
#include "kit.hpp"
#include "messages.hpp"
#include "platform.hpp"

#ifndef AMK_VERSION
#define AMK_VERSION "0.0.0-dev"
#endif

namespace amk::cli {

namespace {

constexpr std::string_view kKindCommands[] = {"extension", "snippet", "app"};

// Element lists of the settings that accept `amk config <liste> +id` and `-id`.
std::string_view KindOfList(std::string_view list) {
    if (list == "extensions") return "extension";
    if (list == "apps") return "app";
    if (list == "snippets") return "snippet";
    return {};
}

bool IsKindFolderText(std::string_view text) {
    return text == "extension" || text == "theme" || text == "app" || text == "snippet";
}

bool ParseBoolText(std::string_view text, bool& value) {
    std::string lowered;
    for (char c : text) lowered += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lowered == "1" || lowered == "true" || lowered == "on" || lowered == "yes") {
        value = true;
        return true;
    }
    if (lowered == "0" || lowered == "false" || lowered == "off" || lowered == "no") {
        value = false;
        return true;
    }
    return false;
}

// Checks a value before it is written. `stored` receives the form that goes into settings.txt.
bool CheckSetting(const std::string& key, const std::string& value, std::string& stored, std::string& problem) {
    stored = value;
    bool boolean = false;

    if (key == "theme.active") {
        if (!value.empty() && !IsValidId(value)) {
            problem = "identifiant de thème invalide : " + value;
            return false;
        }
        return true;
    }
    if (key == "theme.scheme") {
        if (!value.empty() && !IsValidScheme(value)) {
            problem = "nom de schéma invalide : " + value;
            return false;
        }
        return true;
    }
    if (key == "catalog.url") {
        if (!IsHttpsUrl(value) && value.rfind("file://", 0) != 0) {
            problem = "catalog.url doit commencer par https:// (ou file:// pour un catalogue local)";
            return false;
        }
        return true;
    }

    if (key == "app_path" || key == "prefs_path") {
        if (value.empty()) return true;  // empty: back to the path found on its own
        for (char c : value) {
            if (static_cast<unsigned char>(c) < 0x20) {
                problem = key + " contient un caractère de contrôle";
                return false;
            }
        }
        if (!PathArgument(value).is_absolute()) {
            problem = key + " attend un chemin complet, par exemple C:\\dossier\\fichier";
            return false;
        }
        return true;
    }
    if (key == kLaunchFlagsKey) {
        if (!SplitLaunchFlags(value, &problem)) return false;
        return true;
    }
    if (key == i18n::kLanguageSetting) {
        if (!i18n::IsLanguageSetting(value)) {
            problem = Tr("language.value");
            return false;
        }
        return true;
    }
    if (key == "remote.port" || key == "midi.device") {
        int low = key == "remote.port" ? 1024 : 0;
        int high = key == "remote.port" ? 65535 : 255;
        int number = -1;
        auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), number);
        if (error != std::errc() || end != value.data() + value.size() || number < low || number > high) {
            problem = key + " attend un nombre de " + std::to_string(low) + " à " + std::to_string(high);
            return false;
        }
        return true;
    }
    if (key == "remote.token") {
        if (!value.empty() && !remote::IsValidToken(value)) {
            problem = "remote.token attend 32 à 128 caractères parmi A-Z a-z 0-9 - _ (vide : un jeton est créé)";
            return false;
        }
        return true;
    }
    if (key == "midi.map") {
        midi::Map map;
        if (!map.Parse(value, &problem)) return false;
        return true;
    }

    bool isBooleanKey = key.rfind("theme.parts.", 0) == 0 || key == "theme.inspect" || key == "theme.live_reload" ||
                        key == "check_kit_update" || key == "telemetry.block" || key == "expose_api" ||
                        key == "discord.enabled" || key == "tray.enabled" || key == "remote.enabled" ||
                        key == "midi.enabled" ||
                        (key.size() > 8 && key.compare(key.size() - 8, 8, ".enabled") == 0 &&
                         (key.rfind("extensions.", 0) == 0 || key.rfind("apps.", 0) == 0 ||
                          key.rfind("snippets.", 0) == 0));
    if (isBooleanKey) {
        if (!ParseBoolText(value, boolean)) {
            problem = key + " attend 1 ou 0 (true, false, on, off, yes, no)";
            return false;
        }
        stored = boolean ? "1" : "0";
    }
    return true;
}

// After an element is switched: an extension or an app starts or stops at once in a running kernel (reload.txt);
// the other kinds wait for amk apply or the next start.
int AnnounceSwitch(Context& ctx, std::string_view kind, const std::string& id, bool enable) {
    if (kind != "extension" && kind != "app") {
        ctx.out << Tr("applyHint") << "\n";
        return 0;
    }
    RequestReload(ctx, {std::string(enable ? "start " : "stop ") + ReloadTarget(kind, id)});
    ctx.out << "Si Apple Music tourne avec le noyau, le changement s'applique tout de suite. Sinon, lance amk launch.\n";
    return 0;
}

// `amk config extensions +id` and `-id`. Enabling needs an element that is installed and valid, or built-in.
int ToggleElement(Context& ctx, Settings& settings, std::string_view list, const std::string& value) {
    std::string_view kind = KindOfList(list);
    std::string id = value.substr(1);
    bool enable = value[0] == '+';
    if (!IsValidId(id)) return Failure(ctx, Tr("invalidId", {{"id", id}}));

    if (enable) {
        bool builtin = kind == "extension" && std::find(std::begin(kBuiltinExtensions), std::end(kBuiltinExtensions),
                                                        std::string_view(id)) != std::end(kBuiltinExtensions);
        bool installed = false;
        for (const InstalledElement& element : ListInstalled(ctx.kit)) {
            if (element.kind == kind && element.id == id) installed = element.Valid();
        }
        if (!builtin && !installed) return Failure(ctx, "élément absent ou invalide : " + std::string(kind) + " " + id);
    }

    SetEnabled(settings, kind, id, enable);
    std::string error;
    if (!SaveSettings(ctx, settings, error)) return Failure(ctx, error);
    ctx.out << std::string(list) << " " << value << " : " << Tr(enable ? "state.on" : "state.off") << "\n";
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
    settings.Set(key, stored);
    // The remote control needs a token: it is created when the remote control is turned on, if none is set yet.
    bool tokenCreated = false;
    if (key == "remote.enabled" && stored == "1" && !remote::IsValidToken(settings.GetString("remote.token", ""))) {
        settings.Set("remote.token", remote::NewToken());
        tokenCreated = true;
    }
    std::string error;
    if (!SaveSettings(ctx, settings, error)) return Failure(ctx, error);
    ctx.out << key << " = " << stored << "\n";
    if (tokenCreated) ctx.out << Tr("remote.tokenCreated") << "\n";
    return 0;
}

// The state shown in `amk list`, in French.
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
    for (std::string_view id : kBuiltinExtensions) {
        if (!filter.empty() && filter != "extension") break;
        ctx.out << Pad("extension", 11) << Pad(id, 26) << Pad(Tr("list.builtin"), 11)
                << Tr(IsEnabled(settings, "extension", id) ? "state.on" : "state.off") << " " << Tr("list.inKernel")
                << "\n";
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
    if (!IsValidId(id)) return Failure(ctx, Tr("invalidId", {{"id", id}}));

    Settings settings;
    std::string error;
    if (!LoadSettings(ctx, settings, error)) return Failure(ctx, error);

    std::string permissions;
    if (enable) {
        bool builtin = kind == "extension" && std::find(std::begin(kBuiltinExtensions), std::end(kBuiltinExtensions),
                                                        std::string_view(id)) != std::end(kBuiltinExtensions);
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
    if (args.size() < 3) return Usage(ctx, "theme set attend un identifiant de thème");
    const std::string& id = args[2];
    if (!IsValidId(id)) return Failure(ctx, "identifiant invalide : " + id);

    const InstalledElement* found = nullptr;
    std::vector<InstalledElement> elements = ListInstalled(ctx.kit);
    for (const InstalledElement& element : elements) {
        if (element.kind == "theme" && element.id == id) found = &element;
    }
    if (found == nullptr) return Failure(ctx, "thème introuvable : " + id + ". Installe-le avec amk install " + id);
    if (!found->Valid()) return Failure(ctx, "thème invalide : " + found->errors.front());

    // The manifest is valid; now the theme engine reads the theme as the kernel will, and refuses what it would refuse.
    std::filesystem::path folder = ctx.kit.Root() / "themes" / id;
    theme::FolderFiles files(folder);
    theme::LoadOptions load;
    if (ctx.options.scheme) load.scheme = *ctx.options.scheme;
    theme::Theme loaded = theme::LoadTheme(id, folder.generic_string(), files, load);
    if (!loaded.usable) {
        return Failure(ctx, "thème refusé par le moteur : " +
                                (loaded.problems.empty() ? std::string("raison inconnue") : loaded.problems.front()));
    }
    for (const std::string& problem : loaded.problems) ctx.out << "Attention : " << problem << "\n";

    Settings settings;
    std::string error;
    if (!LoadSettings(ctx, settings, error)) return Failure(ctx, error);
    settings.Set("theme.active", id);
    if (ctx.options.scheme) {
        if (!IsValidScheme(*ctx.options.scheme)) return Failure(ctx, "nom de schéma invalide : " + *ctx.options.scheme);
        settings.Set("theme.scheme", *ctx.options.scheme);
    }
    if (!SaveSettings(ctx, settings, error)) return Failure(ctx, error);

    ctx.out << "Thème " << id << " choisi";
    if (ctx.options.scheme) ctx.out << " (schéma " << *ctx.options.scheme << ")";
    ctx.out << ". Lance amk apply (ou amk launch) pour l'appliquer.\n";
    return 0;
}

// The files that `amk new` writes for each kind. Placeholders: the id and the author.
std::map<std::string, std::string> Template(std::string_view kind, const std::string& id) {
    const std::string author = "votre-identifiant-github";
    std::string common = "  \"id\": \"" + id + "\",\n  \"name\": \"" + id + "\",\n  \"version\": \"0.1.0\",\n  \"kind\": \"" +
                         std::string(kind) + "\",\n  \"author\": \"" + author + "\",\n  \"description\": \"À décrire.\",\n" +
                         "  \"kitApi\": \"1.0\"";
    if (kind == "extension") {
        return {
            {"manifest.json", "{\n" + common + ",\n  \"main\": \"main.js\",\n  \"permissions\": []\n}\n"},
            {"main.js", "// Extension \"" + id + "\". Démarrée une fois le noyau prêt.\nAMK.log(\"" + id + " démarrée\");\n"
                        "AMK.onStop(() => AMK.log(\"" + id + " arrêtée\"));\n"},
        };
    }
    if (kind == "theme") {
        // A starter that already looks finished: two schemes in Spicetify's names, rounded surfaces, a hover. The
        // guide (docs/themes/GUIDE.md) walks through these same files.
        return {
            {"manifest.json", "{\n" + common + ",\n  \"colors\": \"colors.ini\",\n  \"defaultScheme\": \"Nuit\"\n}\n"},
            {"colors.ini",
             "; Les couleurs, un schéma par section. Les noms sont ceux de Spicetify :\n"
             ";   main     fond des pages          sidebar  barre latérale      player  barre de lecture\n"
             ";   card     écran du morceau        text     texte               subtext texte secondaire\n"
             ";   button   accent (boutons, barre de progression)\n"
             "; Change une valeur et sauvegarde : avec amk theme watch, Apple Music suit tout de suite.\n\n"
             "[Nuit]\nmain = 16131F\nsidebar = 1D1928\nplayer = 1D1928\ncard = 2A2438\nmain-elevated = 2A2438\n"
             "text = F3EEFF\nsubtext = B5ABCB\nbutton = FF7A85\nplayback-bar = FF7A85\nhighlight = 2F2840\n\n"
             "[Jour]\nmain = FBF7F2\nsidebar = F2ECE4\nplayer = F2ECE4\ncard = FFFFFF\nmain-elevated = FFFFFF\n"
             "text = 1F1A2B\nsubtext = 6A6278\nbutton = E0525F\nplayback-bar = E0525F\nhighlight = EDE5DA\n"},
            {"user.css",
             "/* " + id + " : les formes et les effets. Les couleurs viennent de colors.ini ($main, $button...).\n"
             "   Les noms des éléments (Grid#PaneRoot...) sont dans docs/themes/GUIDE.md ; amk devtools montre l'arbre. */\n\n"
             ":root {\n    --rayon: 12px;\n}\n\n"
             "/* Barre latérale : arrondie côté contenu. */\nGrid#PaneRoot {\n"
             "    corner-radius: 0 var(--rayon) var(--rayon) 0;\n}\n\n"
             "/* L'écran du morceau en cours, au centre de la barre de lecture. */\n"
             "NowPlayingControl#LCD > Grid#LCD {\n    corner-radius: var(--rayon);\n}\n\n"
             "/* Les boutons de lecture, ronds. */\nTransportControl#TransportControl AppBarButton {\n"
             "    corner-radius: 50;\n}\n\n"
             "/* La pochette grossit un peu au survol. */\nArtworkImage#ActualArtwork {\n"
             "    transform-origin: center;\n}\n\nArtworkImage#ActualArtwork:hover {\n    transform: scale(1.08);\n}\n"},
        };
    }
    if (kind == "snippet") {
        return {
            {"manifest.json", "{\n" + common + ",\n  \"resources\": \"resources.xaml\"\n}\n"},
            {"resources.xaml",
             "<ResourceDictionary xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\">\n"
             "</ResourceDictionary>\n"},
        };
    }
    return {
        {"manifest.json", "{\n" + common + ",\n  \"main\": \"main.js\",\n  \"icon\": \"icon.svg\"\n}\n"},
        {"main.js", "// App \"" + id + "\". Son code tourne quand sa page est ouverte.\n"},
        {"icon.svg", "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\"><path d=\"M12 2l10 20H2z\"/></svg>\n"},
    };
}

}  // namespace

std::optional<Options> ParseOptions(const std::vector<std::string>& arguments, std::string& error) {
    Options options;
    auto value = [&](size_t& index) -> std::optional<std::string> {
        if (index + 1 >= arguments.size()) {
            error = arguments[index] + " attend une valeur";
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
            error = "nombre attendu : " + text;
            return false;
        }
        target = parsed;
        return true;
    };

    bool onlyArguments = false;
    for (size_t i = 0; i < arguments.size(); i++) {
        const std::string& arg = arguments[i];
        // After "--", and for the value of `config app_launch_flags`, a word starting with "--" is an argument:
        // `amk config app_launch_flags --remote-debugging-port=9222` stores the flag instead of refusing it.
        bool flagsValue = options.positional.size() == 2 && options.positional[0] == "config" &&
                          options.positional[1] == kLaunchFlagsKey;
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
        } else if (arg == "--check") {
            options.check = true;
        } else if (arg == "--apply") {
            options.apply = true;
        } else if (arg == "--follow") {
            options.follow = true;
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

std::filesystem::path PathArgument(std::string_view text) {
    std::u8string utf8;
    utf8.reserve(text.size());
    for (char c : text) utf8.push_back(static_cast<char8_t>(static_cast<unsigned char>(c)));
    return std::filesystem::path(utf8);
}

std::string Utf8(const std::filesystem::path& path) {
    std::u8string utf8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}

bool LoadSettings(const Context& ctx, Settings& settings, std::string& error) {
    if (!settings.Load(ctx.kit.Settings().string())) {
        error = "lecture impossible : " + Utf8(ctx.kit.Settings());
        return false;
    }
    return true;
}

bool SaveSettings(const Context& ctx, const Settings& settings, std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(ctx.kit.Root(), ec);
    if (!settings.Save(ctx.kit.Settings().string())) {
        error = "écriture impossible : " + Utf8(ctx.kit.Settings());
        return false;
    }
    return true;
}

int CmdHelp(Context& ctx) {
    ctx.out << Tr("help");
    return 0;
}

int CmdVersion(Context& ctx) {
    ctx.out << Tr("version", {{"version", AMK_VERSION}}) << "\n";
    return 0;
}

namespace {

// What `amk path <nom>` prints. Nothing when the path cannot be found (Apple Music on another system).
std::optional<std::filesystem::path> NamedPath(Context& ctx, std::string_view name) {
    if (name == "kit" || name == "userdata") return ctx.kit.Root();
    if (name == "settings") return ctx.kit.Settings();
    if (name == "extensions" || name == "themes" || name == "apps" || name == "snippets") {
        return ctx.kit.Root() / std::string(name);
    }
    if (name == "logs") return ctx.kit.Journal();
    if (name == "backups") return ctx.kit.Backups();
    if (name == "app") return PathSetting(ctx, "app_path", platform::AppPackageFolder());
    if (name == "prefs") return PathSetting(ctx, "prefs_path", platform::AppPreferencesFile());
    return std::nullopt;
}

constexpr std::string_view kPathNames[] = {"kit", "settings", "extensions", "themes", "apps", "snippets",
                                           "logs", "backups", "app", "prefs"};

}  // namespace

int CmdPath(Context& ctx) {
    const std::vector<std::string>& args = ctx.options.positional;
    if (args.size() > 2) return Usage(ctx, "path prend au plus un nom");
    if (args.size() == 2) {
        const std::string& name = args[1];
        bool known = name == "userdata" ||
                     std::find(std::begin(kPathNames), std::end(kPathNames), std::string_view(name)) != std::end(kPathNames);
        if (!known) {
            std::string names;
            for (std::string_view each : kPathNames) names += (names.empty() ? "" : ", ") + std::string(each);
            return Usage(ctx, "nom inconnu pour path : " + name + " (" + names + ")");
        }
        std::optional<std::filesystem::path> path = NamedPath(ctx, name);
        if (!path) {
            if (name == "app") {
                return Failure(ctx, "dossier d'Apple Music introuvable : Apple Music n'est pas installé, ou ce système "
                                    "n'est pas Windows. Tu peux l'indiquer avec amk config app_path <dossier>.");
            }
            return Failure(ctx, "préférences d'Apple Music introuvables sur ce système. Tu peux les indiquer avec "
                                "amk config prefs_path <fichier>.");
        }
        ctx.out << Utf8(*path) << "\n";
        return 0;
    }

    std::optional<std::filesystem::path> app = NamedPath(ctx, "app");
    std::optional<std::filesystem::path> prefs = NamedPath(ctx, "prefs");
    ctx.out << "Dossier du kit   : " << Utf8(ctx.kit.Root()) << "\n"
            << "Réglages         : " << Utf8(ctx.kit.Settings()) << "\n"
            << "Éléments         : " << Utf8(ctx.kit.Root()) << "\\{extensions,themes,apps,snippets}\n"
            << "État du kit      : " << Utf8(ctx.kit.Active()) << ", " << Utf8(ctx.kit.Reload()) << "\n"
            << "Journal          : " << Utf8(ctx.kit.Journal()) << "\n"
            << "Sauvegardes      : " << Utf8(ctx.kit.Backups()) << "\n"
            << "Dossier de amk   : " << Utf8(platform::ExecutableDirectory()) << "\n"
            << "Apple Music      : " << kAppUserModelId << "\n"
            << "Paquet de l'app  : " << (app ? Utf8(*app) : std::string("introuvable sur ce système")) << "\n"
            << "Préférences      : " << (prefs ? Utf8(*prefs) : std::string("introuvables sur ce système")) << "\n"
            << "Un seul chemin   : amk path <kit|settings|extensions|themes|apps|snippets|logs|backups|app|prefs>\n";
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
        if (!value) return Failure(ctx, Tr("missingKey", {{"key", args[1]}}));
        ctx.out << *value << "\n";
        return 0;
    }
    return CmdConfigSet(ctx, settings);
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
        settings.Set("theme.active", "");
        if (!SaveSettings(ctx, settings, error)) return Failure(ctx, error);
        ctx.out << "Aucun thème actif. Lance amk apply pour l'appliquer.\n";
        return 0;
    }
    // check and watch read a theme folder anywhere on disk, the way the kernel reads it, and print what would apply.
    if (action == "check" || action == "watch") {
        if (args.size() != 3) return Usage(ctx, "theme " + std::string(action) + " attend un dossier de thème");
        std::vector<std::string> watchArgs{"watch", args[2]};
        if (ctx.options.scheme) watchArgs.insert(watchArgs.end(), {"--scheme", *ctx.options.scheme});
        int polls = action == "check" ? -1 : ctx.options.maxCycles;
        if (polls >= 0) watchArgs.insert(watchArgs.end(), {"--polls", std::to_string(polls)});
        if (action == "check") {
            std::filesystem::path folder = std::filesystem::absolute(PathArgument(args[2]));
            std::error_code error;
            if (!std::filesystem::is_directory(folder, error)) return Failure(ctx, "dossier introuvable : " + args[2]);
            theme::FolderFiles files(folder);
            theme::LoadOptions load;
            if (ctx.options.scheme) load.scheme = *ctx.options.scheme;
            return theme::PrintThemeReport(theme::LoadTheme(Utf8(folder.filename()), folder.generic_string(), files, load),
                                           ctx.out);
        }
        std::chrono::milliseconds interval(std::max(ctx.options.intervalMs, 200));
        return theme::RunWatch(watchArgs, ctx.out, ctx.err, [interval] { std::this_thread::sleep_for(interval); });
    }
    return Usage(ctx, "action inconnue pour theme : " + std::string(action));
}

int CmdValidate(Context& ctx) {
    const std::vector<std::string>& args = ctx.options.positional;
    if (args.size() != 2) return Usage(ctx, "validate attend un dossier ou un catalogue.json");

    std::filesystem::path target = PathArgument(args[1]);
    std::error_code ec;
    if (std::filesystem::is_directory(target, ec)) {
        FolderValidation validation = ValidateElementFolder(target);
        if (validation.ok() && validation.errors.empty()) {
            ctx.out << Utf8(target) << " : valide (" << validation.manifest->kind << " " << validation.manifest->id
                    << " " << validation.manifest->version << ")\n";
            return 0;
        }
        ctx.err << Utf8(target) << " : invalide\n";
        for (const std::string& problem : validation.errors) ctx.err << "  - " << problem << "\n";
        return 1;
    }

    std::ifstream in(target, std::ios::binary);
    if (!in) return Failure(ctx, "fichier introuvable : " + args[1]);
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::vector<std::string> errors;
    std::optional<Catalog> catalog = ParseCatalog(buffer.str(), false, errors);
    if (!catalog) {
        ctx.err << args[1] << " : invalide\n";
        for (const std::string& problem : errors) ctx.err << "  - " << problem << "\n";
        return 1;
    }
    ctx.out << args[1] << " : valide (" << catalog->entries.size() << " élément(s))\n";
    return 0;
}

int CmdNew(Context& ctx) {
    const std::vector<std::string>& args = ctx.options.positional;
    if (args.size() != 3) return Usage(ctx, "new attend un type et un identifiant");
    const std::string& kind = args[1];
    const std::string& id = args[2];
    if (!IsKindFolderText(kind)) return Failure(ctx, "type inconnu : " + kind + " (extension, theme, snippet ou app)");
    if (!IsValidId(id)) return Failure(ctx, "identifiant invalide : " + id);

    std::filesystem::path folder = std::filesystem::current_path() / PathArgument(id);
    std::error_code ec;
    if (std::filesystem::exists(folder, ec)) return Failure(ctx, "le dossier existe déjà : " + Utf8(folder));

    for (const auto& [name, text] : Template(kind, id)) {
        std::string error;
        if (!WriteTextFile(folder / name, text, error)) return Failure(ctx, error);
    }
    ctx.out << "Élément créé dans " << Utf8(folder) << ". Remplace votre-identifiant-github par le tien,\n"
            << "puis vérifie-le avec : amk validate " << id << "\n";
    return 0;
}

namespace {

int Dispatch(Context& ctx) {
    const Options& options = ctx.options;
    if (options.positional.empty()) return CmdHelp(ctx);
    const std::string& command = options.positional[0];

    if (command == "help") return CmdHelp(ctx);
    if (command == "version") return CmdVersion(ctx);
    if (command == "path") return CmdPath(ctx);
    if (command == "config") return CmdConfig(ctx);
    if (command == "list") return CmdList(ctx);
    if (command == "theme") return CmdTheme(ctx);
    if (command == "validate") return CmdValidate(ctx);
    if (command == "new") return CmdNew(ctx);
    if (command == "market" || command == "catalog") return CmdMarket(ctx);
    if (command == "install") return CmdInstall(ctx);
    if (command == "remove") return CmdRemove(ctx);
    if (command == "update") return CmdUpdate(ctx);
    if (command == "apply") return CmdApply(ctx);
    if (command == "launch" || command == "auto") return CmdLaunch(ctx);
    if (command == "restore") return CmdRestore(ctx);
    if (command == "backup") return CmdBackup(ctx);
    if (command == "watch") return CmdWatch(ctx);
    if (command == "build") return CmdBuild(ctx);
    if (command == "inspect") return options.tree ? CmdInspectTree(ctx) : CmdInspect(ctx);
    if (command == "devtools") return CmdDevtools(ctx);
    if (command == "versions") return CmdVersions(ctx);
    if (command == "install-kit") return CmdInstallKit(ctx);
    if (command == "types") return CmdTypes(ctx);
    if (command == "dev") return CmdDev(ctx);
    if (command == "logs") return CmdLogs(ctx);
    if (command == "doctor") return CmdDoctor(ctx);
    if (command == "profile") return CmdProfile(ctx);
    if (command == "export") return CmdExport(ctx);
    if (command == "import") return CmdImport(ctx);
    if (command == "test") return CmdTest(ctx);
    for (std::string_view kind : kKindCommands) {
        if (command == kind) return CmdKind(ctx, kind);
    }
    return Usage(ctx, Tr("unknownCommand", {{"command", command}}));
}

// Verbs that take no argument and can follow one another on a line, as in Spicetify (`spicetify backup apply`):
// `amk backup apply launch`. A word that is not in this list (an id, `list`, `on`) makes the line one command.
constexpr std::string_view kChainVerbs[] = {"backup", "apply", "launch", "auto", "restore", "devtools", "doctor", "version"};

// Commands after which the notice about a new kit release may be printed. Never after a command whose output a script
// reads (path, config), nor after update itself.
constexpr std::string_view kNoticeCommands[] = {"launch", "auto", "apply", "list", "doctor", "market", "catalog"};

bool Contains(std::span<const std::string_view> list, std::string_view word) {
    return std::find(list.begin(), list.end(), word) != list.end();
}

}  // namespace

bool IsChainLine(const std::vector<std::string>& positional) {
    if (positional.size() < 2) return false;
    for (const std::string& word : positional) {
        if (!Contains(kChainVerbs, word)) return false;
    }
    return true;
}

int Execute(const Options& options, std::istream& in, std::ostream& out, std::ostream& err) {
    KitLayout kit(options.kitDir ? PathArgument(*options.kitDir) : platform::DefaultKitDirectory());
    SetLanguage(LanguageFor(kit.Settings(), platform::UserLocale()));
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
        if (!one.positional.empty() && Contains(kNoticeCommands, one.positional[0])) notice = true;
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

}  // namespace amk::cli
