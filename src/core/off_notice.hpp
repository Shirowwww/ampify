#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "injection_policy.hpp"

namespace ampify {

// Ampify stays off when Apple Music updated itself to a build that was not tested, and for one start after two crashes.
// Apple Music then opens as usual, so the person is told once, by a Windows notification (`ampify notice`, started by
// the launcher), instead of finding the theme gone.

// Name of the notice a verdict calls for, such as "version:1.1792.39682.0" or "safe-mode"; empty when it calls for none.
std::string OffNoticeKey(const PolicyVerdict& verdict, std::string_view appVersion);

// Whether the notice of `verdict` is due now. `file` holds the last notice shown: the same one is not shown twice in
// a row, and it is shown again after Ampify loaded once in between (an allowed verdict empties the file). Records the
// notice it returns true for.
bool TakeOffNotice(const std::filesystem::path& file, const PolicyVerdict& verdict, std::string_view appVersion);

}  // namespace ampify
