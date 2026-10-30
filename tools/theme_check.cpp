// amk_theme_check: reads a theme folder the way the kernel does and prints what it would apply.
// It needs no Apple Music. Use it to check a theme before sending it, or in continuous integration.
//
//   amk_theme_check <folder> [scheme]
//
// Exit code: 0 when the theme is usable and has no problem, 1 when it is refused or has problems, 2 on bad use.

#include <filesystem>
#include <iostream>
#include <string>

#include "../core/theme/commands.hpp"
#include "../core/theme/files.hpp"
#include "../core/theme/theme.hpp"

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        std::cerr << "usage : amk_theme_check <dossier-du-thème> [schéma]\n";
        return 2;
    }

    std::filesystem::path folder = std::filesystem::absolute(argv[1]);
    if (!std::filesystem::is_directory(folder)) {
        std::cerr << "dossier introuvable : " << folder.string() << '\n';
        return 2;
    }

    amk::theme::LoadOptions options;
    if (argc == 3) options.scheme = argv[2];

    amk::theme::FolderFiles files(folder);
    amk::theme::Theme theme = amk::theme::LoadTheme(folder.filename().string(), folder.generic_string(), files, options);
    return amk::theme::PrintThemeReport(theme, std::cout);
}
