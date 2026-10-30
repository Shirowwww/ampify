#pragma once

#include <filesystem>
#include <functional>
#include <ostream>
#include <string>
#include <vector>

#include "theme.hpp"

namespace amk::theme {

// Where the kit keeps its files: settings.txt, the themes folder, and the journal folder.
struct KitLayout {
    std::filesystem::path root;  // %USERPROFILE%\AppleMusicModKit on Windows

    std::filesystem::path Settings() const { return root / "settings.txt"; }
    std::filesystem::path Themes() const { return root / "themes"; }
};

// Writes what the kernel would apply, as `amk_theme_check` does. Returns 0 when the theme is usable and has no problem,
// 1 otherwise.
int PrintThemeReport(const Theme& theme, std::ostream& out);

// `amk theme list`, `amk theme set <id|none> [--scheme nom]`. Returns 0 when done, 1 when the theme is refused,
// 2 for a wrong command.
int RunThemeCommand(const std::vector<std::string>& args, const KitLayout& kit, std::ostream& out, std::ostream& err);

// `amk watch <dossier> [--scheme nom] [--polls N]`: checks the theme now, then again after each change of its files.
// `pause` waits between two looks at the folder. `--polls 0` (the default) watches until the process is stopped.
int RunWatch(const std::vector<std::string>& args, std::ostream& out, std::ostream& err,
             const std::function<void()>& pause);

}  // namespace amk::theme
