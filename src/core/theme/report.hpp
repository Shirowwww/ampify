#pragma once

#include <ostream>

#include "theme.hpp"

namespace ampify::theme {

// Writes what the kernel would apply, as `ampify_theme_check` does. Returns 0 when the theme is usable and has no problem,
// 1 otherwise.
int PrintThemeReport(const Theme& theme, std::ostream& out);

}  // namespace ampify::theme
