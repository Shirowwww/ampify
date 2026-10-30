#include "i18n.hpp"

#include <algorithm>
#include <cctype>

namespace amk::i18n {

namespace {

// Texts of the kernel and of the services. Simple words: the person reading may not be technical.
constexpr Message kKit[] = {
    {"tray.tooltip", "Apple Music Mod Kit", "Apple Music Mod Kit"},
    {"tray.previous", "Morceau précédent", "Previous track"},
    {"tray.next", "Morceau suivant", "Next track"},
    {"tray.openKit", "Ouvrir le dossier du kit", "Open the kit folder"},
    {"tray.started", "Icône de la zone de notification affichée", "Notification area icon shown"},
    {"tray.failed", "Icône de la zone de notification refusée par Windows ({error})",
     "Windows refused the notification area icon ({error})"},
    {"remote.started", "Télécommande locale prête sur http://127.0.0.1:{port} (jeton demandé)",
     "Local remote control ready on http://127.0.0.1:{port} (token required)"},
    {"remote.failed", "Télécommande locale non démarrée : {error}", "Local remote control not started: {error}"},
    {"remote.tokenCreated", "Télécommande locale : jeton créé dans remote.token (amk config remote.token)",
     "Local remote control: token created in remote.token (amk config remote.token)"},
    {"discord.connected", "Discord : connecté, la présence s'affiche", "Discord: connected, the presence is shown"},
    {"discord.unavailable", "Discord : client introuvable ou fermé, nouvel essai dans 15 s",
     "Discord: client not found or closed, trying again in 15 s"},
    {"discord.error", "Discord a refusé la présence : {error}", "Discord refused the presence: {error}"},
    {"midi.opened", "MIDI : entrée « {name} » ouverte", "MIDI: input \"{name}\" opened"},
    {"midi.none", "MIDI : aucun appareil d'entrée trouvé", "MIDI: no input device found"},
    {"midi.failed", "MIDI : ouverture de l'entrée impossible ({error})", "MIDI: the input cannot be opened ({error})"},
    {"midi.mapRefused", "MIDI : midi.map refusé : {error}", "MIDI: midi.map refused: {error}"},
    {"service.off", "{service} : désactivé (réglage {setting} à 0)", "{service}: off (setting {setting} is 0)"},
    {"common.yes", "oui", "yes"},
    {"common.no", "non", "no"},
};

std::string Lower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
    return out;
}

}  // namespace

bool IsLanguageSetting(std::string_view value) {
    return value.empty() || value == "fr" || value == "en" || value == "auto";
}

Language ResolveLanguage(std::string_view setting, std::string_view locale) {
    if (setting == "fr") return Language::Fr;
    if (setting == "en") return Language::En;
    std::string lowered = Lower(locale);
    std::string_view name = lowered;
    if (name.empty() || name == "c" || name == "posix" || name.substr(0, 2) == "c." || name.substr(0, 2) == "fr") {
        return Language::Fr;
    }
    return Language::En;
}

std::string_view Code(Language language) { return language == Language::En ? "en" : "fr"; }

std::string_view Lookup(std::span<const Message> table, Language language, std::string_view key) {
    for (const Message& message : table) {
        if (message.key != key) continue;
        if (language == Language::En && !message.en.empty()) return message.en;
        return message.fr;
    }
    return key;
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

std::string Text(Language language, std::string_view key, Args args) {
    return Format(Lookup(KitMessages(), language, key), args);
}

}  // namespace amk::i18n
