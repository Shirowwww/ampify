#pragma once

// Texts of Ampify, in English. A text has a key, such as "tray.next", and one sentence. A missing key shows the key
// itself, so a forgotten text is visible but never breaks a message.
//
// Placeholders are written {name} and filled by Format. The command line has its own table (src/cli/messages.cpp); the
// kernel and the elements use this one (Ampify.i18n.t("kit.<key>")). Extension authors translate their own texts with
// Ampify.i18n.define.

#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ampify::i18n {

struct Message {
    std::string_view key;
    std::string_view text;
};

// The text of `key` in `table`, or the key itself when the table has no such text.
std::string_view Lookup(std::span<const Message> table, std::string_view key);

// Replaces each {name} by its value. An unknown placeholder stays as it is.
using Args = std::initializer_list<std::pair<std::string_view, std::string_view>>;
std::string Format(std::string_view pattern, Args args = {});

// The placeholder names of a pattern, in order, for the tests.
std::string Placeholders(std::string_view pattern);

// Texts of the kernel and of the Ampify services (tray menu, remote control, Discord, MIDI).
std::span<const Message> KitMessages();

// The texts people see in one language of kTranslationLanguages, English otherwise. A key missing there uses KitMessages.
std::span<const Message> KitMessagesIn(std::string_view language);

// The languages that have a table, English first.
std::vector<std::string_view> Languages();

// The language of Text, as two letters ("fr"). The kernel sets it once at start from the Windows locale; "en" before.
void SetLanguage(std::string_view code);
const std::string& Language();

// Whether the language writes 0,5 rather than 0.5 (kDecimalCommaLanguages).
bool UsesDecimalComma();

// Lookup in the language's table, then in KitMessages, then Format.
std::string Text(std::string_view key, Args args = {});

// The two-letter language of a locale ("fr-FR" -> "fr", "en_US.UTF-8" -> "en"), in lower case. "en" when the locale
// names no language (empty, "C", "POSIX"). Traditional Chinese (zh-TW, zh-HK, zh-MO, zh-Hant) gives "zh-hant".
std::string LanguageCode(std::string_view locale);

}  // namespace ampify::i18n
