#pragma once

// Texts of the kit in French and in English. A text has a key, such as "tray.next", and one sentence per language.
// The language comes from the setting `language`: fr, en, or auto (the default), which follows the user's locale.
// French is the reference: a text missing in English falls back to French, and a missing key shows the key itself,
// so a forgotten translation is visible but never breaks a message.
//
// Placeholders are written {name} and filled by Format. The tests check that both languages of a text use the
// same placeholders. The command line has its own table (cli/messages.cpp); the kernel and the elements use this
// one (AMK.i18n.t("kit.<key>")).

#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace amk::i18n {

enum class Language { Fr, En };

struct Message {
    std::string_view key;
    std::string_view fr;
    std::string_view en;
};

// The setting key, and its accepted values.
inline constexpr std::string_view kLanguageSetting = "language";
bool IsLanguageSetting(std::string_view value);  // "fr", "en", "auto" or empty

// The language from the setting and the locale ("fr-FR", "en_US.UTF-8", "C"...). auto, an empty or an unknown
// setting follows the locale: French for a French locale and for a locale that names no language (C, POSIX,
// empty), English for any other language, since English is the more widely read of the two.
Language ResolveLanguage(std::string_view setting, std::string_view locale);

// "fr" or "en".
std::string_view Code(Language language);

// The text of `key` in `table`. Falls back to French, then to the key itself.
std::string_view Lookup(std::span<const Message> table, Language language, std::string_view key);

// Replaces each {name} by its value. An unknown placeholder stays as it is.
using Args = std::initializer_list<std::pair<std::string_view, std::string_view>>;
std::string Format(std::string_view pattern, Args args = {});

// The placeholder names of a pattern, in order, for the tests.
std::string Placeholders(std::string_view pattern);

// Texts of the kernel and of the kit services (tray menu, remote control, Discord, MIDI).
std::span<const Message> KitMessages();

// Lookup in KitMessages, then Format.
std::string Text(Language language, std::string_view key, Args args = {});

}  // namespace amk::i18n
