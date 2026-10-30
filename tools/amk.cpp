// amk: command-line tool for the kit's themes. It edits settings.txt, which the kernel reads again every few seconds,
// so a change shows in Apple Music without a restart.
//
//   amk theme list
//   amk theme set <id|none> [--scheme nom]
//   amk watch <dossier-du-thème> [--scheme nom] [--polls N]
//
// The kit folder is %USERPROFILE%\AppleMusicModKit. Use --kit <dossier> or the AMK_KIT variable to choose another.
// Exit code: 0 when done, 1 when a theme is refused, 2 for a wrong command.

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "../core/theme/commands.hpp"

namespace {

std::filesystem::path DefaultKit() {
    if (const char* kit = std::getenv("AMK_KIT"); kit && *kit) return kit;
    for (const char* home : {"USERPROFILE", "HOME"}) {
        if (const char* folder = std::getenv(home); folder && *folder) {
            return std::filesystem::path(folder) / "AppleMusicModKit";
        }
    }
    return "AppleMusicModKit";
}

void PrintUsage() {
    std::cerr << "usage :\n"
                 "  amk theme list\n"
                 "  amk theme set <id|none> [--scheme nom]\n"
                 "  amk watch <dossier-du-thème> [--scheme nom] [--polls N]\n"
                 "  option : --kit <dossier>\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);

    amk::theme::KitLayout kit{DefaultKit()};
    for (size_t i = 0; i + 1 < args.size(); i++) {
        if (args[i] == "--kit") {
            kit.root = args[i + 1];
            args.erase(args.begin() + static_cast<std::ptrdiff_t>(i), args.begin() + static_cast<std::ptrdiff_t>(i) + 2);
            break;
        }
    }

    if (args.empty()) {
        PrintUsage();
        return 2;
    }
    if (args[0] == "theme") return amk::theme::RunThemeCommand(args, kit, std::cout, std::cerr);
    if (args[0] == "watch") {
        return amk::theme::RunWatch(args, std::cout, std::cerr, [] { std::this_thread::sleep_for(std::chrono::seconds(1)); });
    }

    PrintUsage();
    return 2;
}
