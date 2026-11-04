#pragma once

// `ampify devtools pick` in the kernel. The command reads the pointer's place on the screen and asks for the element under
// it through the file pick.txt of the kit folder ("x y seconds": a place in pixels, and the time it was asked). On the
// interface thread, the theme service looks for that file at each pass; the answer is logs\picked.txt: the element
// under the place, with the selector a theme can use for it, then the elements above it, topmost first. Compiled only
// with AMPIFY_WITH_XAML.

#if defined(AMPIFY_WITH_XAML)

#include <filesystem>

#include "../core/journal.hpp"

namespace ampify::win {

// Answers a request waiting in `kit`\pick.txt, then removes it. A request older than a minute is dropped, so one left by a
// command run while Apple Music was closed does not read a place nobody pointed at. True when `output` was written.
bool AnswerPickRequest(const std::filesystem::path& kit, const std::filesystem::path& output, Journal& journal) noexcept;

}  // namespace ampify::win

#endif
