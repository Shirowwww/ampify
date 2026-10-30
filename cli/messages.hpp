#pragma once

// Texts of the command line in French and in English (core/i18n.hpp for the mechanism). The language is chosen once
// per run from the setting `language` of the kit (fr, en, auto) and the user's locale. Every text that a user sees
// first goes through Tr: the help, usage errors, the element list and the toggles. Messages not migrated yet stay
// in French.

#include <filesystem>
#include <span>
#include <string>
#include <string_view>

#include "../core/i18n.hpp"

namespace amk::cli {

std::span<const i18n::Message> CliMessages();

// The language of this run. French until SetLanguage is called.
void SetLanguage(i18n::Language language);
i18n::Language CurrentLanguage();

// The language from the `language` key of a settings.txt (missing file: auto) and a locale such as "fr-FR".
i18n::Language LanguageFor(const std::filesystem::path& settingsFile, std::string_view locale);

// The text of `key` in the language of this run, with its placeholders filled.
std::string Tr(std::string_view key, i18n::Args args = {});

}  // namespace amk::cli
