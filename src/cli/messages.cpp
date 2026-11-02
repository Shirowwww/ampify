#include "messages.hpp"

namespace ampify::cli {

namespace {

constexpr std::string_view kHelp = R"(ampify: Ampify on the command line (themes, extensions and apps for Apple Music)

Usage: ampify [options] <command> [arguments]
       ampify backup apply launch       several commands without arguments in a row (stops at the first failure):
                                     backup, apply, launch, auto, restore, devtools, doctor, version

Settings and state
  config                       shows every setting
  config <key>                 shows one setting
  config <key> <value>         changes a setting (the value is checked)
  config extensions +id        turns an extension on (or -id to turn it off)
  config --dir                 opens the Ampify folder (--dry-run to only print it)
  path                         shows the folders of Ampify and of Apple Music
  path <name>                  one path only: kit, settings, extensions, themes, apps, snippets, logs,
                               backups, app (Apple Music package), prefs (Apple Music preferences)
  list                         lists the installed elements and their state

Elements
  extension list|enable|disable <id>   extensions (including the built-in Atmos leveler)
  snippet  list|enable|disable <id>    snippets (small styles switched on alone)
  app      list|enable|disable <id>    apps (pages added to the interface)
  theme    list | set <id> [--scheme <name>] | clear
  theme    check <folder> [--scheme <name>]   reads a theme as the kernel does and shows what would apply
  theme    watch <folder> [--scheme <name>]   reads the theme again at each save (--max-cycles N to stop)
  theme    names <inspector.json>...          checks the stable names (.sidebar, .card...) against saved interface trees
  theme    import <folder|zip|address> [id] takes up a Spicetify theme (its color.ini works as is)
  validate <folder>            checks the manifest of an element (or a catalog.json)
  new <extension|theme|snippet|app> <id>   creates a starter element

Marketplace
  market                       lists the catalog
  install <id> [--yes]         installs from the catalog (SHA-256 checked)
  update <id> [--yes]          updates an installed element from the catalog
  update [--check] [--apply]   updates Ampify from the latest GitHub release
  install-kit <version> [--apply]   installs a given version of Ampify (even an older one), SHA-256 checked
  versions [list]              Apple Music versions the kernel accepts, and the installed one
  versions update              downloads the list of tested versions published with the catalog
  remove <id>                  removes an element (a backup is made first)

Applying
  apply                        writes what will load (active.txt), tells the kernel, and makes Ampify load
                               with Apple Music each time it starts (installed copy only)
  launch                       applies, then starts Apple Music with the kernel now (ampify_launcher.exe)
  restore                      Ampify no longer loads with Apple Music; nothing in the package is changed
  restore --backup <name>      puts back the settings of a backup
  backup                       backs up the settings and the elements
  backup list                  lists the backups
  watch [--interval ms]        applies at each change of a theme or an extension

Authoring and development
  build <folder> [file.zip]            builds an element: checks it, writes the zip, gives the SHA-256 and the catalog entry
  inspect <folder> | <type> <id>       shows the manifest, the files and their SHA-256
  inspect --tree                       shows the last interface tree written by the kernel
  devtools [on|off]                    asks the kernel to write the interface tree (logs\inspector.txt)
  dev <folder>                         copies the element into Ampify at each save and reloads it
  types [file]                         writes ampify.d.ts (types for the editor, ampify.d.ts by default)
  test <folder>                        runs main.js in the JavaScript engine, with a fake host

Diagnostics
  doctor                       state of Ampify: elements, settings, safe mode, log, tools
  logs [count] [--follow]      last lines of the kernel and launcher logs

Profiles and transfers
  profile list                 lists the settings profiles
  profile save|use|delete <name>   saves, loads (the previous state is backed up) or deletes a profile
  export <file.zip>            exports settings and elements, without the app files
  import <file.zip>            imports an export after checking it (--dry-run to look without writing)

Options
  --kit-dir <folder>           Ampify folder (%USERPROFILE%\Ampify by default)
  --catalog <source>           catalog: local file, file:// or https://
  --dry-run                    prints the command that would run, without running it
  --yes, -y                    answers yes to every question
  --max-cycles <n>             stops watch, dev or logs --follow after n passes (useful for tests)
  --follow                     logs: keeps printing new lines
  --json                       market: prints the state as JSON (read by the Marketplace app)
  --version, --help

Documentation: docs/cli.md
)";

constexpr i18n::Message kCli[] = {
    {"help", kHelp},
    {"prefix", "ampify: "},
    {"helpHint", "Help: ampify help"},
    {"unknownCommand", "unknown command: {command}"},
    {"unknownOption", "unknown option: {option}"},
    {"unknownAction", "unknown action: {action}"},
    {"needsId", "{action} needs an id"},
    {"tooManyArgs", "too many arguments for {command}"},
    {"invalidId", "invalid id: {id}"},
    {"missingKey", "key not set: {key}"},
    {"version", "ampify {version} (extension API 1.0)"},
    {"chainStopped", "\"{command}\" failed, the next commands are not run"},
    {"list.type", "TYPE"},
    {"list.id", "ID"},
    {"list.version", "VERSION"},
    {"list.state", "STATE"},
    {"list.builtin", "built-in"},
    {"list.inKernel", "(in the kernel)"},
    {"list.empty", "No element installed. See ampify market."},
    {"state.on", "on"},
    {"state.off", "off"},
    {"state.active", "active"},
    {"state.inactive", "inactive"},
    {"state.invalid", "invalid: {error}"},
    {"element.toggled", "{kind} {id} {state}"},
    {"element.permissions", "Requested permissions: {permissions}"},
    {"element.notFound", "element not found: {kind} {id}. Install it with ampify install {id}"},
    {"element.invalid", "invalid element: {error}"},
    {"applyHint", "If Apple Music is running with Ampify, it changes right away. Otherwise, at the next start."},
    {"remote.tokenCreated",
     "Remote control token created (remote.token). Keep it to yourself: it gives control of playback."},
};

}  // namespace

std::span<const i18n::Message> CliMessages() { return kCli; }

std::string Tr(std::string_view key, i18n::Args args) { return i18n::Format(i18n::Lookup(kCli, key), args); }

}  // namespace ampify::cli
