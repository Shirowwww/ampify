#pragma once

#include <filesystem>
#include <functional>
#include <ostream>
#include <string>
#include <vector>

#include "../core/theme/report.hpp"
#include "../core/theme/theme.hpp"

namespace ampify::theme {

// `ampify theme names <inspector.json>...`: checks the stable names (.sidebar, .card...) against interface trees saved by
// the inspector, to see which names a new Apple Music version broke. Returns 0 when every name is found in some tree,
// 1 when a name is found in none, 2 when a tree cannot be read.
int RunNames(const std::vector<std::filesystem::path>& files, std::ostream& out, std::ostream& err);

// `ampify theme watch <folder> [--scheme name] [--polls N]`: checks the theme now, then again after each change of its files.
// `pause` waits between two looks at the folder. `--polls 0` (the default) watches until the process is stopped.
int RunWatch(const std::vector<std::string>& args, std::ostream& out, std::ostream& err,
             const std::function<void()>& pause);

}  // namespace ampify::theme
