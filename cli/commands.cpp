#include "commands.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>

#include "../core/apple_music.hpp"
#include "archive.hpp"
#include "catalog.hpp"
#include "element.hpp"
#include "internal.hpp"
#include "kit.hpp"
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

    if (key == "theme.id") {
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

    bool isBooleanKey = key.rfind("theme.parts.", 0) == 0 ||
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

// `amk config extensions +id` and `-id`. Enabling needs an element that is installed and valid, or built-in.
int ToggleElement(Context& ctx, Settings& settings, std::string_view list, const std::string& value) {
    std::string_view kind = KindOfList(list);
    std::string id = value.substr(1);
    bool enable = value[0] == '+';
    if (!IsValidId(id)) return Failure(ctx, "identifiant invalide : " + id);

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
    ctx.out << std::string(list) << " " << value << " : " << (enable ? "activé" : "désactivé") << "\n";
    ctx.out << "Lance amk apply (ou amk launch) pour l'appliquer.\n";
    return 0;
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
    std::string error;
    if (!SaveSettings(ctx, settings, error)) return Failure(ctx, error);
    ctx.out << key << " = " << stored << "\n";
    return 0;
}

// The state shown in `amk list`, in French.
std::string StateOf(const Settings& settings, const InstalledElement& element) {
    bool enabled = IsEnabled(settings, element.kind, element.id);
    if (element.kind == "theme") {
        if (!element.Valid()) return "invalide : " + element.errors.front();
        return enabled ? "actif" : "inactif";
    }
    if (!element.Valid()) return "invalide : " + element.errors.front();
    return enabled ? "activé" : "désactivé";
}

void PrintHeader(Context& ctx) {
    ctx.out << Pad("TYPE", 11) << Pad("IDENTIFIANT", 26) << Pad("VERSION", 11) << "ÉTAT\n";
}

int PrintElements(Context& ctx, std::string_view filter) {
    Settings settings;
    std::string error;
    if (!LoadSettings(ctx, settings, error)) return Failure(ctx, error);

    bool any = false;
    PrintHeader(ctx);
    for (std::string_view id : kBuiltinExtensions) {
        if (!filter.empty() && filter != "extension") break;
        ctx.out << Pad("extension", 11) << Pad(id, 26) << Pad("intégré", 11)
                << (IsEnabled(settings, "extension", id) ? "activé" : "désactivé") << " (dans le noyau)\n";
        any = true;
    }
    for (const InstalledElement& element : ListInstalled(ctx.kit)) {
        if (!filter.empty() && element.kind != filter) continue;
        std::string version = element.manifest ? element.manifest->version : "?";
        ctx.out << Pad(element.kind, 11) << Pad(element.id, 26) << Pad(version, 11) << StateOf(settings, element)
                << "\n";
        any = true;
    }
    if (!any) ctx.out << "Aucun élément installé. Voir amk market.\n";
    return 0;
}

int SetElementState(Context& ctx, std::string_view kind, const std::string& id, bool enable) {
    if (!IsValidId(id)) return Failure(ctx, "identifiant invalide : " + id);

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
                return Failure(ctx, "élément introuvable : " + std::string(kind) + " " + id +
                                        ". Installe-le avec amk install " + id);
            }
            if (!found->Valid()) return Failure(ctx, "élément invalide : " + found->errors.front());
            for (const std::string& permission : found->manifest->permissions) {
                permissions += (permissions.empty() ? "" : ", ") + permission;
            }
        }
    }

    SetEnabled(settings, kind, id, enable);
    if (!SaveSettings(ctx, settings, error)) return Failure(ctx, error);

    ctx.out << std::string(kind) << " " << id << (enable ? " activé" : " désactivé") << "\n";
    if (enable && !permissions.empty()) ctx.out << "Permissions demandées : " << permissions << "\n";
    ctx.out << "Lance amk apply (ou amk launch) pour l'appliquer.\n";
    return 0;
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

    Settings settings;
    std::string error;
    if (!LoadSettings(ctx, settings, error)) return Failure(ctx, error);
    settings.Set("theme.id", id);
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
        return {
            {"manifest.json", "{\n" + common + ",\n  \"colors\": \"colors.ini\",\n  \"defaultScheme\": \"Dark\"\n}\n"},
            {"colors.ini", "[Dark]\naccent = FA2D48\n\n[Light]\naccent = FA2D48\n"},
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

    for (size_t i = 0; i < arguments.size(); i++) {
        const std::string& arg = arguments[i];
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
        } else if (arg == "--help" || arg == "-h") {
            options.positional.insert(options.positional.begin(), "help");
        } else if (arg == "--version") {
            options.positional.insert(options.positional.begin(), "version");
        } else if (arg.size() > 2 && arg.compare(0, 2, "--") == 0) {
            error = "option inconnue : " + arg;
            return std::nullopt;
        } else {
            // Element ids start with a letter or a digit, so "-id" from `config extensions -id` is an argument.
            options.positional.push_back(arg);
        }
    }
    return options;
}

int Failure(Context& ctx, const std::string& message) {
    ctx.err << "amk : " << message << "\n";
    return 1;
}

int Usage(Context& ctx, const std::string& message) {
    ctx.err << "amk : " << message << "\nAide : amk help\n";
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
    ctx.out << R"(amk : le kit Apple Music, en ligne de commande

Usage : amk [options] <commande> [arguments]

Réglages et état
  config                       affiche tous les réglages
  config <clé>                 affiche un réglage
  config <clé> <valeur>        change un réglage (valeur vérifiée)
  config extensions +id        active une extension (ou -id pour la désactiver)
  path                         affiche les dossiers du kit
  list                         liste les éléments installés et leur état

Éléments
  extension list|enable|disable <id>   extensions (dont le nivellement Atmos, intégré)
  snippet  list|enable|disable <id>    snippets (fragments XAML)
  app      list|enable|disable <id>    apps (pages ajoutées à l'interface)
  theme    list | set <id> [--scheme <nom>] | clear
  validate <dossier>           vérifie le manifeste d'un élément (ou un catalogue.json)
  new <extension|theme|snippet|app> <id>   crée un élément de départ

Marketplace
  market                       liste le catalogue
  install <id> [--yes]         installe depuis le catalogue (SHA-256 vérifié)
  update <id> [--yes]          met à jour un élément installé depuis le catalogue
  update [--check] [--apply]   met le kit à jour depuis la dernière release GitHub
  remove <id>                  retire un élément (une sauvegarde est faite avant)

Application
  apply                        écrit ce qui se chargera (active.txt) et prévient le noyau
  launch                       applique puis lance Apple Music avec le noyau (amk_launcher.exe)
  restore                      lance Apple Music sans noyau, rien n'est modifié dans le paquet
  restore --backup <nom>       remet les réglages d'une sauvegarde
  backup                       fait une sauvegarde des réglages et des éléments
  backup list                  liste les sauvegardes
  watch [--interval ms]        applique à chaque modification d'un thème ou d'une extension

Création et développement
  build <dossier> [fichier.zip]        compile un élément : valide, écrit le zip, donne le SHA-256 et l'entrée de catalogue
  inspect <dossier> | <type> <id>      affiche le manifeste, les fichiers et leurs SHA-256
  dev <dossier>                        copie l'élément dans le kit à chaque sauvegarde et le recharge
  types [fichier]                      écrit amk.d.ts (types pour l'éditeur, amk.d.ts par défaut)
  test <dossier>                       lance main.js dans le moteur JavaScript, avec un hôte factice

Diagnostic
  doctor                       état du kit : éléments, réglages, mode sans échec, journal, outils
  logs [nombre] [--follow]     dernières lignes du journal du noyau et du lanceur

Profils et transferts
  profile list                 liste les profils de réglages
  profile save|use|delete <nom>   enregistre, charge (l'état précédent est sauvegardé) ou supprime un profil
  export <fichier.zip>         exporte réglages et éléments, sans les fichiers de l'app
  import <fichier.zip>         importe un export après vérification (--dry-run pour voir sans écrire)

Options
  --kit-dir <dossier>          dossier du kit (par défaut %USERPROFILE%\AppleMusicModKit)
  --catalog <source>           catalogue : fichier local, file:// ou https://
  --dry-run                    affiche la commande qui serait lancée, sans la lancer
  --yes, -y                    répond oui à toutes les questions
  --max-cycles <n>             arrête watch, dev ou logs --follow après n passages (utile pour les tests)
  --follow                     logs : continue d'afficher les nouvelles lignes
  --version, --help

Documentation : docs/cli/COMMANDES.md
)";
    return 0;
}

int CmdVersion(Context& ctx) {
    ctx.out << "amk " << AMK_VERSION << " (API d'extensions 1.0)\n";
    return 0;
}

int CmdPath(Context& ctx) {
    ctx.out << "Dossier du kit   : " << Utf8(ctx.kit.Root()) << "\n"
            << "Réglages         : " << Utf8(ctx.kit.Settings()) << "\n"
            << "Éléments         : " << Utf8(ctx.kit.Root()) << "\\{extensions,themes,apps,snippets}\n"
            << "État du kit      : " << Utf8(ctx.kit.Active()) << ", " << Utf8(ctx.kit.Reload()) << "\n"
            << "Sauvegardes      : " << Utf8(ctx.kit.Backups()) << "\n"
            << "Dossier de amk   : " << Utf8(platform::ExecutableDirectory()) << "\n"
            << "Apple Music      : " << kAppUserModelId << "\n";
    return 0;
}

int CmdConfig(Context& ctx) {
    const std::vector<std::string>& args = ctx.options.positional;
    if (args.size() > 3) return Usage(ctx, "trop d'arguments pour config");

    Settings settings;
    std::string error;
    if (!LoadSettings(ctx, settings, error)) return Failure(ctx, error);

    if (args.size() == 1) {
        for (const auto& [key, value] : settings.Values()) ctx.out << key << " = " << value << "\n";
        return 0;
    }
    if (args.size() == 2) {
        std::optional<std::string> value = settings.Get(args[1]);
        if (!value) return Failure(ctx, "clé absente : " + args[1]);
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
    if (args.size() > 3) return Usage(ctx, "trop d'arguments pour " + std::string(kind));

    if (action == "list") return PrintElements(ctx, kind);
    if (action == "enable" || action == "disable") {
        if (args.size() < 3) return Usage(ctx, std::string(action) + " attend un identifiant");
        return SetElementState(ctx, kind, args[2], action == "enable");
    }
    return Usage(ctx, "action inconnue : " + std::string(action));
}

int CmdTheme(Context& ctx) {
    const std::vector<std::string>& args = ctx.options.positional;
    std::string_view action = args.size() > 1 ? std::string_view(args[1]) : std::string_view("list");

    if (action == "list") return PrintElements(ctx, "theme");
    if (action == "set") return CmdThemeSet(ctx);
    if (action == "clear") {
        Settings settings;
        std::string error;
        if (!LoadSettings(ctx, settings, error)) return Failure(ctx, error);
        settings.Set("theme.id", "");
        if (!SaveSettings(ctx, settings, error)) return Failure(ctx, error);
        ctx.out << "Aucun thème actif. Lance amk apply pour l'appliquer.\n";
        return 0;
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
        if (ContainsLink(target)) validation.errors.push_back("lien symbolique interdit dans l'élément");
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

int Execute(const Options& options, std::istream& in, std::ostream& out, std::ostream& err) {
    KitLayout kit(options.kitDir ? PathArgument(*options.kitDir) : platform::DefaultKitDirectory());
    Context ctx{options, kit, in, out, err};

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
    if (command == "inspect") return CmdInspect(ctx);
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
    return Usage(ctx, "commande inconnue : " + command);
}

}  // namespace amk::cli
