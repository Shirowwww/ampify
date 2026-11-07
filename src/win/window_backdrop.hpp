#pragma once
// The material of Apple Music's main window (Windows 11's Mica, Mica Alt or acrylic), as a theme asks for it with
// --window-material in its :root. It shows through what the theme leaves see-through.

#if defined(AMPIFY_WITH_XAML)

#include "../core/theme/theme.hpp"

#include <optional>

namespace ampify {
class Journal;
}

namespace ampify::win {

// Gives the main window that material, or Apple Music's own background back when the theme asks for none. Interface
// thread only; nothing is written while the material stays the same.
void KeepWindowMaterial(std::optional<theme::WindowMaterial> material, Journal& journal) noexcept;

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
