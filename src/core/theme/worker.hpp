#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "../settings.hpp"
#include "scheme_choice.hpp"
#include "session.hpp"

namespace ampify::theme {

// The worker side of the themes: on each pass it reads settings.txt and the Ampify folder, decides what the interface
// should show, and loads it again only when something changed. Portable: the Windows service (src/win/theme_service.cpp)
// calls it from its worker thread and posts the request to the interface thread, where ThemeSession applies it.
//
// What the Windows side must give it:
//   SetSystemDark(dark)       the Windows mode, read before each pass, for theme.scheme = auto: the value
//                             AppsUseLightTheme of HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize
//                             (0 means dark)
//   Tick(settings, minute)    the settings just read, and the local time in minutes from midnight (GetLocalTime),
//                             for theme.schedule
//
// Ampify folder layout read here: themes\<id>\, snippets\<id>\, resource-keys.txt, sidebar.json, home.json.

struct ThemeSettings;

// Everything a load reads, gathered once per pass. The worker loads again exactly when it differs from the last one
// (a new generation), so an input added here can never be forgotten by the comparison.
struct LoadInputs {
    struct Snippet {
        std::string id;
        std::map<std::string, std::string> values;  // snippets.<id>.<key>
        bool operator==(const Snippet&) const = default;
    };

    std::string active;  // the theme's folder name, empty for none
    std::string scheme;  // as ChooseScheme names it
    ThemeParts parts;
    std::optional<Argb> accent;
    GlassLevel glass = GlassLevel::Full;
    std::map<std::string, std::string> values;  // themes.<active>.<key>
    std::vector<Snippet> snippets;              // the enabled ones, in id order
    std::map<std::string, std::string> artwork;  // the artwork's colours, only when a user.css names them
    std::optional<std::string> sidebar;          // sidebar.json and home.json, as text
    std::optional<std::string> home;
    std::optional<std::set<std::string>> allowedKeys;  // resource-keys.txt
    std::vector<std::string> signatures;  // with theme.live_reload, one FolderSignature per folder read

    bool operator==(const LoadInputs&) const = default;
};

class ThemeWorker {
public:
    explicit ThemeWorker(std::filesystem::path kit);

    void SetSystemDark(bool dark) { m_systemDark = dark; }

    // The colours of the artwork playing (art-1...). A theme or snippet whose user.css names --art- is loaded again
    // when they change; the others are not.
    void SetArtwork(std::map<std::string, std::string> variables) { m_artwork = std::move(variables); }

    // The request for this pass. Its generation changes when its LoadInputs do: the active theme, the chosen scheme (by
    // setting, by schedule or by Windows mode), the parts, the accent, the glass level, the theme's and the enabled
    // snippets' settings, the artwork's colours a user.css names, sidebar.json, home.json, resource-keys.txt, and with
    // theme.live_reload any file of the theme or of an enabled snippet.
    SessionRequest Tick(const Settings& settings, int minute);

private:
    LoadInputs ReadInputs(const Settings& settings, const ThemeSettings& wanted, int minute);
    SessionRequest Load(const LoadInputs& inputs) const;

    std::filesystem::path m_kit;
    bool m_systemDark = false;
    std::map<std::string, std::string> m_artwork;
    std::optional<LoadInputs> m_loaded;
    SessionRequest m_request;
    SchemeChoice m_scheme;
};

// The resource keys that resources.xaml may define, one per line of resource-keys.txt (lines starting with # are
// comments). Nothing when the file is missing: the check is then skipped and the problems say so.
std::optional<std::set<std::string>> ParseAllowedKeys(const std::optional<std::string>& text);

}  // namespace ampify::theme
