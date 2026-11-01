#pragma once

// Texts of the command line, in English (src/core/i18n.hpp for the mechanism). Every text that a user sees goes through
// Tr: the help, usage errors, the element list and the toggles.

#include <span>
#include <string>
#include <string_view>

#include "../core/i18n.hpp"

namespace ampify::cli {

std::span<const i18n::Message> CliMessages();

// The text of `key`, with its placeholders filled.
std::string Tr(std::string_view key, i18n::Args args = {});

}  // namespace ampify::cli
