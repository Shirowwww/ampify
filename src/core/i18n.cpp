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
    {"mods.themeTitle", "Theme: {name}"},
    {"mods.themeSubtitle", "The settings of the active theme. A change shows at once."},
    {"mods.theme", "Theme"},
    {"mods.themeHelp", "The look of Apple Music. The Marketplace has more."},
    {"mods.themeNone", "None (Apple Music's own look)"},
    {"tweaks.title", "Apple Music"},
    {"tweaks.subtitle", "What Ampify changes in how Apple Music behaves. A change applies at once."},
    {"tweaks.notifications", "Notifications"},
    {"tweaks.notificationsHelp", "Such as the one of each new song. Apple's own switch is in Settings, General."},
    {"tweaks.notifications.apple", "As Apple Music shows them"},
    {"tweaks.notifications.background", "Only when Apple Music is not in front"},
    {"tweaks.notifications.never", "Never"},
    {"tweaks.player", "Full-screen player"},
    {"tweaks.playerHelp", "Where Now Playing and Apple's full-screen button open the player."},
    {"tweaks.player.window", "Over the Apple Music window"},
    {"tweaks.player.replace", "In place of the Apple Music window"},
    {"tweaks.player.workArea", "Whole screen, taskbar visible"},
    {"tweaks.player.screen", "Whole screen (Apple's way)"},
    {"tweaks.close", "Closing the window"},
    {"tweaks.closeHelp", "In the notification area, the music goes on: its icon brings the window back or quits."},
    {"tweaks.close.quit", "Quits Apple Music"},
    {"tweaks.close.tray", "Keeps it in the notification area"},
    {"tray.show", "Show Apple Music"},
    {"tray.quit", "Quit Apple Music"},
    {"mods.report", "Diagnostic report"},
    {"mods.reportHelp", "A zip of Ampify's state and logs, without your names, folders or music, to attach to an issue."},
    {"mods.report.run", "Export a report"},
    {"mods.report.running", "Writing the report…"},
    {"mods.report.done", "Report exported"},
    {"mods.report.failed", "The report failed"},
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
    {"mods.themeTitle", "Thème : {name}"},
    {"mods.themeSubtitle", "Les réglages du thème actif. Un changement se voit tout de suite."},
    {"mods.theme", "Thème"},
    {"mods.themeHelp", "L'apparence d'Apple Music. Le Marketplace en propose d'autres."},
    {"mods.themeNone", "Aucun (l'apparence d'Apple Music)"},
    {"tweaks.title", "Apple Music"},
    {"tweaks.subtitle", "Ce qu'Ampify change dans le comportement d'Apple Music. Un changement s'applique tout de suite."},
    {"tweaks.notifications", "Notifications"},
    {"tweaks.notificationsHelp", "Comme celle de chaque nouveau morceau. Le réglage d'Apple est dans Réglages, Général."},
    {"tweaks.notifications.apple", "Comme Apple Music les montre"},
    {"tweaks.notifications.background", "Seulement quand Apple Music n'est pas devant"},
    {"tweaks.notifications.never", "Jamais"},
    {"tweaks.player", "Lecteur plein écran"},
    {"tweaks.playerHelp", "Où « À l'écoute » et le bouton plein écran d'Apple ouvrent le lecteur."},
    {"tweaks.player.window", "Par-dessus la fenêtre d'Apple Music"},
    {"tweaks.player.replace", "À la place de la fenêtre d'Apple Music"},
    {"tweaks.player.workArea", "Tout l'écran, barre des tâches visible"},
    {"tweaks.player.screen", "Tout l'écran (comme Apple)"},
    {"tweaks.close", "Fermer la fenêtre"},
    {"tweaks.closeHelp", "Dans la zone de notification, la musique continue : son icône rouvre la fenêtre ou quitte."},
    {"tweaks.close.quit", "Quitte Apple Music"},
    {"tweaks.close.tray", "La garde dans la zone de notification"},
    {"tray.show", "Afficher Apple Music"},
    {"tray.quit", "Quitter Apple Music"},
    {"mods.report", "Rapport de diagnostic"},
    {"mods.reportHelp", "Un zip de l'état et des journaux d'Ampify, sans vos noms, dossiers ni musique, à joindre à un signalement."},
    {"mods.report.run", "Exporter un rapport"},
    {"mods.report.running", "Rapport en cours…"},
    {"mods.report.done", "Rapport exporté"},
    {"mods.report.failed", "Le rapport a échoué"},
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
