#pragma once

// The command line of `ampify`: parsing of the options, and the commands that a user runs.
// The portable logic lives in the other headers of this folder. This part is the glue with the
// platform (platform.hpp) and the console.

#include <filesystem>
#include <istream>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace ampify::cli {

struct Options {
    std::vector<std::string> positional;  // the command, then its arguments
    std::optional<std::string> kitDir;        // --kit-dir: the folder of the kit
    std::optional<std::string> catalog;       // --catalog: a catalog.json, a file or an https address
    std::optional<std::string> installDir;    // --install-dir: where `update` puts the new files
    std::optional<std::string> releaseUrl;    // --release-url: where `update` looks for releases
    std::optional<std::string> scheme;        // --scheme: a colour scheme for `theme set`
    std::optional<std::string> backup;        // --backup: the snapshot that `restore` puts back
    int intervalMs = 1000;                    // --interval: polling period of `watch`
    int maxCycles = 0;                        // --max-cycles: stop `watch` after N passes, 0 for never
    bool dryRun = false;                      // --dry-run: print what would run, run nothing
    bool yes = false;                         // --yes: answer yes to every question
    bool deleteData = false;                  // --delete-data: `uninstall` deletes the Ampify folder too
    bool check = false;                       // --check: `update` only looks
    bool apply = false;                       // --apply: `update` installs the files it downloaded
    bool autoUpdate = false;                  // --auto: `update` follows kit_update, as the background launcher runs it
    bool follow = false;                      // --follow: `logs` keeps printing the lines that are added
    bool json = false;                        // --json: `market` prints JSON, for the Marketplace page
    bool tree = false;                        // --tree: `inspect` prints the last interface tree of the kernel
    bool dir = false;                         // --dir: `config` opens the kit folder
};

// Reads the arguments that follow the program name. On a mistake returns nullopt and writes the reason to `error`.
std::optional<Options> ParseOptions(const std::vector<std::string>& arguments, std::string& error);

// True when every word is a verb without argument that can be chained (`ampify backup apply launch`). Execute then runs
// them in order and stops at the first one that fails.
bool IsChainLine(const std::vector<std::string>& positional);

// Runs the command. Returns the exit code: 0 for success, 1 for a failure, 2 for a wrong usage.
int Execute(const Options& options, std::istream& in, std::ostream& out, std::ostream& err);

}  // namespace ampify::cli
