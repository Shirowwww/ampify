#include "messages.hpp"

#include "../core/settings.hpp"

namespace amk::cli {

namespace {

constexpr std::string_view kHelpFr = R"(amk : le kit Apple Music, en ligne de commande

Usage : amk [options] <commande> [arguments]
        amk backup apply launch      plusieurs commandes sans argument à la suite (arrêt au premier échec) :
                                     backup, apply, launch, auto, restore, devtools, doctor, version

Réglages et état
  config                       affiche tous les réglages
  config <clé>                 affiche un réglage
  config <clé> <valeur>        change un réglage (valeur vérifiée)
  config extensions +id        active une extension (ou -id pour la désactiver)
  config language fr|en|auto   langue du kit (auto : celle du système)
  config --dir                 ouvre le dossier du kit (--dry-run pour seulement l'afficher)
  path                         affiche les dossiers du kit et d'Apple Music
  path <nom>                   un seul chemin : kit, settings, extensions, themes, apps, snippets, logs,
                               backups, app (paquet d'Apple Music), prefs (préférences d'Apple Music)
  list                         liste les éléments installés et leur état

Éléments
  extension list|enable|disable <id>   extensions (dont le nivellement Atmos, intégré)
  snippet  list|enable|disable <id>    snippets (fragments XAML)
  app      list|enable|disable <id>    apps (pages ajoutées à l'interface)
  theme    list | set <id> [--scheme <nom>] | clear
  theme    check <dossier> [--scheme <nom>]   lit un thème comme le noyau et affiche ce qui s'appliquerait
  theme    watch <dossier> [--scheme <nom>]   relit le thème à chaque sauvegarde (--max-cycles N pour s'arrêter)
  theme    import <dossier|zip|adresse> [id]  reprend un thème Spicetify (son color.ini marche tel quel)
  validate <dossier>           vérifie le manifeste d'un élément (ou un catalogue.json)
  new <extension|theme|snippet|app> <id>   crée un élément de départ

Marketplace
  market                       liste le catalogue
  install <id> [--yes]         installe depuis le catalogue (SHA-256 vérifié)
  update <id> [--yes]          met à jour un élément installé depuis le catalogue
  update [--check] [--apply]   met le kit à jour depuis la dernière release GitHub
  install-kit <version> [--apply]   installe une version précise du kit (même plus ancienne), SHA-256 vérifié
  versions [list]              versions d'Apple Music acceptées par le noyau, et celle installée
  versions update              télécharge la liste des versions testées publiée avec le catalogue
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
  inspect --tree                       affiche le dernier arbre de l'interface écrit par le noyau
  devtools [on|off]                    demande au noyau d'écrire l'arbre de l'interface (journal\inspector.txt)
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

constexpr std::string_view kHelpEn = R"(amk: the Apple Music kit, on the command line

Usage: amk [options] <command> [arguments]
       amk backup apply launch       several commands without arguments in a row (stops at the first failure):
                                     backup, apply, launch, auto, restore, devtools, doctor, version

Settings and state
  config                       shows every setting
  config <key>                 shows one setting
  config <key> <value>         changes a setting (the value is checked)
  config extensions +id        turns an extension on (or -id to turn it off)
  config language fr|en|auto   language of the kit (auto: the system's)
  config --dir                 opens the kit folder (--dry-run to only print it)
  path                         shows the folders of the kit and of Apple Music
  path <name>                  one path only: kit, settings, extensions, themes, apps, snippets, logs,
                               backups, app (Apple Music package), prefs (Apple Music preferences)
  list                         lists the installed elements and their state

Elements
  extension list|enable|disable <id>   extensions (including the built-in Atmos leveler)
  snippet  list|enable|disable <id>    snippets (XAML fragments)
  app      list|enable|disable <id>    apps (pages added to the interface)
  theme    list | set <id> [--scheme <name>] | clear
  theme    check <folder> [--scheme <name>]   reads a theme as the kernel does and shows what would apply
  theme    watch <folder> [--scheme <name>]   reads the theme again at each save (--max-cycles N to stop)
  theme    import <folder|zip|address> [id] takes up a Spicetify theme (its color.ini works as is)
  validate <folder>            checks the manifest of an element (or a catalog.json)
  new <extension|theme|snippet|app> <id>   creates a starter element

Marketplace
  market                       lists the catalog
  install <id> [--yes]         installs from the catalog (SHA-256 checked)
  update <id> [--yes]          updates an installed element from the catalog
  update [--check] [--apply]   updates the kit from the latest GitHub release
  install-kit <version> [--apply]   installs a given version of the kit (even an older one), SHA-256 checked
  versions [list]              Apple Music versions the kernel accepts, and the installed one
  versions update              downloads the list of tested versions published with the catalog
  remove <id>                  removes an element (a backup is made first)

Applying
  apply                        writes what will load (active.txt) and tells the kernel
  launch                       applies, then starts Apple Music with the kernel (amk_launcher.exe)
  restore                      starts Apple Music without the kernel; nothing in the package is changed
  restore --backup <name>      puts back the settings of a backup
  backup                       backs up the settings and the elements
  backup list                  lists the backups
  watch [--interval ms]        applies at each change of a theme or an extension

Authoring and development
  build <folder> [file.zip]            builds an element: checks it, writes the zip, gives the SHA-256 and the catalog entry
  inspect <folder> | <type> <id>       shows the manifest, the files and their SHA-256
  inspect --tree                       shows the last interface tree written by the kernel
  devtools [on|off]                    asks the kernel to write the interface tree (journal\inspector.txt)
  dev <folder>                         copies the element into the kit at each save and reloads it
  types [file]                         writes amk.d.ts (types for the editor, amk.d.ts by default)
  test <folder>                        runs main.js in the JavaScript engine, with a fake host

Diagnostics
  doctor                       state of the kit: elements, settings, safe mode, log, tools
  logs [count] [--follow]      last lines of the kernel and launcher logs

Profiles and transfers
  profile list                 lists the settings profiles
  profile save|use|delete <name>   saves, loads (the previous state is backed up) or deletes a profile
  export <file.zip>            exports settings and elements, without the app files
  import <file.zip>            imports an export after checking it (--dry-run to look without writing)

Options
  --kit-dir <folder>           kit folder (%USERPROFILE%\AppleMusicModKit by default)
  --catalog <source>           catalog: local file, file:// or https://
  --dry-run                    prints the command that would run, without running it
  --yes, -y                    answers yes to every question
  --max-cycles <n>             stops watch, dev or logs --follow after n passes (useful for tests)
  --follow                     logs: keeps printing new lines
  --version, --help

Documentation (French): docs/cli/COMMANDES.md
)";

constexpr i18n::Message kCli[] = {
    {"help", kHelpFr, kHelpEn},
    {"prefix", "amk : ", "amk: "},
    {"helpHint", "Aide : amk help", "Help: amk help"},
    {"unknownCommand", "commande inconnue : {command}", "unknown command: {command}"},
    {"unknownOption", "option inconnue : {option}", "unknown option: {option}"},
    {"unknownAction", "action inconnue : {action}", "unknown action: {action}"},
    {"needsId", "{action} attend un identifiant", "{action} needs an id"},
    {"tooManyArgs", "trop d'arguments pour {command}", "too many arguments for {command}"},
    {"invalidId", "identifiant invalide : {id}", "invalid id: {id}"},
    {"missingKey", "clé absente : {key}", "key not set: {key}"},
    {"version", "amk {version} (API d'extensions 1.0)", "amk {version} (extension API 1.0)"},
    {"chainStopped", "« {command} » a échoué, les commandes suivantes ne sont pas lancées",
     "\"{command}\" failed, the next commands are not run"},
    {"list.type", "TYPE", "TYPE"},
    {"list.id", "IDENTIFIANT", "ID"},
    {"list.version", "VERSION", "VERSION"},
    {"list.state", "ÉTAT", "STATE"},
    {"list.builtin", "intégré", "built-in"},
    {"list.inKernel", "(dans le noyau)", "(in the kernel)"},
    {"list.empty", "Aucun élément installé. Voir amk market.", "No element installed. See amk market."},
    {"state.on", "activé", "on"},
    {"state.off", "désactivé", "off"},
    {"state.active", "actif", "active"},
    {"state.inactive", "inactif", "inactive"},
    {"state.invalid", "invalide : {error}", "invalid: {error}"},
    {"element.toggled", "{kind} {id} {state}", "{kind} {id} {state}"},
    {"element.permissions", "Permissions demandées : {permissions}", "Requested permissions: {permissions}"},
    {"element.notFound", "élément introuvable : {kind} {id}. Installe-le avec amk install {id}",
     "element not found: {kind} {id}. Install it with amk install {id}"},
    {"element.invalid", "élément invalide : {error}", "invalid element: {error}"},
    {"applyHint", "Lance amk apply (ou amk launch) pour l'appliquer.", "Run amk apply (or amk launch) to apply it."},
    {"language.value", "language attend fr, en ou auto", "language expects fr, en or auto"},
    {"remote.tokenCreated",
     "Jeton de la télécommande créé (remote.token). Garde-le pour toi : il donne le contrôle de la lecture.",
     "Remote control token created (remote.token). Keep it to yourself: it gives control of playback."},
};

i18n::Language g_language = i18n::Language::Fr;

}  // namespace

std::span<const i18n::Message> CliMessages() { return kCli; }

void SetLanguage(i18n::Language language) { g_language = language; }

i18n::Language CurrentLanguage() { return g_language; }

i18n::Language LanguageFor(const std::filesystem::path& settingsFile, std::string_view locale) {
    Settings settings;
    settings.Load(settingsFile.string());
    return i18n::ResolveLanguage(settings.GetString(std::string(i18n::kLanguageSetting), "auto"), locale);
}

std::string Tr(std::string_view key, i18n::Args args) {
    return i18n::Format(i18n::Lookup(kCli, g_language, key), args);
}

}  // namespace amk::cli
