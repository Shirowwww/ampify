#include "i18n.hpp"

#include <algorithm>
#include <cctype>
#include <optional>

namespace ampify::i18n {

namespace {

// Texts of the kernel and of the services. Simple words: the person reading may not be technical.
constexpr Message kKit[] = {
    {"tray.tooltip", "Ampify"},
    {"tray.previous", "Previous track"},
    {"tray.next", "Next track"},
    {"tray.openKit", "Open the Ampify folder"},
    {"tray.started", "Notification area icon shown"},
    {"tray.failed", "Windows refused the notification area icon ({error})"},
    {"remote.started", "Local remote control ready on http://127.0.0.1:{port} (token required)"},
    {"remote.failed", "Local remote control not started: {error}"},
    {"remote.tokenCreated", "Local remote control: token created in remote.token (ampify config remote.token)"},
    {"discord.connected", "Discord: connected, the presence is shown"},
    {"discord.unavailable", "Discord: client not found or closed, trying again in 15 s"},
    {"discord.error", "Discord refused the presence: {error}"},
    {"midi.opened", "MIDI: input \"{name}\" opened"},
    {"midi.none", "MIDI: no input device found"},
    {"midi.failed", "MIDI: the input cannot be opened ({error})"},
    {"midi.mapRefused", "MIDI: midi.map refused: {error}"},
    {"service.off", "{service}: off (setting {setting} is 0)"},
    {"common.yes", "yes"},
    {"common.no", "no"},
    {"ui.on", "On"},
    {"ui.off", "Off"},
    {"ui.close", "Close"},
    {"page.back", "Back"},
    {"mods.subtitle", "The extensions installed in Ampify. A change applies right away."},
    {"mods.openFolder", "Open the Ampify folder"},
    {"mods.openMarketplace", "Open the Marketplace"},
    {"mods.toggleHelp", "Turns this extension on or off right away"},
};

// French texts of what people see (menus, buttons, the Settings card). The log stays in English, so a key missing here
// falls back to the English text.
constexpr Message kKitFr[] = {
    {"tray.previous", "Morceau précédent"},
    {"tray.next", "Morceau suivant"},
    {"tray.openKit", "Ouvrir le dossier Ampify"},
    {"common.yes", "oui"},
    {"common.no", "non"},
    {"ui.on", "Activé"},
    {"ui.off", "Désactivé"},
    {"ui.close", "Fermer"},
    {"page.back", "Retour"},
    {"mods.subtitle", "Les extensions installées dans Ampify. Un changement s'applique tout de suite."},
    {"mods.openFolder", "Ouvrir le dossier Ampify"},
    {"mods.openMarketplace", "Ouvrir le Marketplace"},
    {"mods.toggleHelp", "Active ou désactive cette extension tout de suite"},
};

std::string g_language = "en";  // set once at start, before any service reads it

std::optional<std::string_view> Find(std::span<const Message> table, std::string_view key) {
    for (const Message& message : table) {
        if (message.key == key) return message.text;
    }
    return std::nullopt;
}

bool IsLetter(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; }

}  // namespace

std::string_view Lookup(std::span<const Message> table, std::string_view key) {
    return Find(table, key).value_or(key);
}

std::string Format(std::string_view pattern, Args args) {
    std::string out;
    out.reserve(pattern.size());
    for (size_t i = 0; i < pattern.size(); i++) {
        if (pattern[i] == '{') {
            size_t close = pattern.find('}', i + 1);
            if (close != std::string_view::npos) {
                std::string_view name = pattern.substr(i + 1, close - i - 1);
                bool replaced = false;
                for (const auto& [key, value] : args) {
                    if (key == name) {
                        out.append(value);
                        replaced = true;
                        break;
                    }
                }
                if (replaced) {
                    i = close;
                    continue;
                }
            }
        }
        out.push_back(pattern[i]);
    }
    return out;
}

std::string Placeholders(std::string_view pattern) {
    std::string names;
    for (size_t i = 0; i < pattern.size(); i++) {
        if (pattern[i] != '{') continue;
        size_t close = pattern.find('}', i + 1);
        if (close == std::string_view::npos) break;
        std::string_view name = pattern.substr(i + 1, close - i - 1);
        bool word = !name.empty() && std::all_of(name.begin(), name.end(), [](char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        });
        if (word) {
            names += (names.empty() ? "" : ",") + std::string(name);
            i = close;
        }
    }
    return names;
}

std::span<const Message> KitMessages() { return kKit; }

std::span<const Message> KitMessagesIn(std::string_view language) {
    if (language == "fr") return kKitFr;
    return kKit;
}

void SetLanguage(std::string_view code) { g_language = std::string(code); }

const std::string& Language() { return g_language; }

std::string Text(std::string_view key, Args args) {
    std::optional<std::string_view> text = Find(KitMessagesIn(g_language), key);
    return Format(text ? *text : Lookup(KitMessages(), key), args);
}

std::string LanguageCode(std::string_view locale) {
    bool twoLetters = locale.size() >= 2 && IsLetter(locale[0]) && IsLetter(locale[1]) &&
                      (locale.size() == 2 || !IsLetter(locale[2]));
    if (!twoLetters) return "en";
    std::string code(locale.substr(0, 2));
    std::transform(code.begin(), code.end(), code.begin(), [](unsigned char c) { return std::tolower(c); });
    return code;
}

}  // namespace ampify::i18n
