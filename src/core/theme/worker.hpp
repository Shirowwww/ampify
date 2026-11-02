#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>

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
class ThemeWorker {
public:
    explicit ThemeWorker(std::filesystem::path kit);

    void SetSystemDark(bool dark) { m_systemDark = dark; }

    // The colours of the artwork playing (art-1...). A theme or snippet whose user.css names --art- is loaded again
    // when they change; the others are not.
    void SetArtwork(std::map<std::string, std::string> variables) { m_artwork = std::move(variables); }

    // The request for this pass. Its generation changes when the active theme, the chosen scheme (by setting, by
    // schedule or by Windows mode), the parts, the active theme's settings (themes.<id>.<key>), the enabled snippets,
    // sidebar.json, home.json or resource-keys.txt change, and with theme.live_reload when a file of the theme or of an
    // enabled snippet changes.
    SessionRequest Tick(const Settings& settings, int minute);

private:
    std::filesystem::path m_kit;
    bool m_systemDark = false;
    std::map<std::string, std::string> m_artwork;
    std::string m_lastKey;
    bool m_hasKey = false;
    SessionRequest m_request;
    SchemeChoice m_scheme;
};

// The resource keys that resources.xaml may define, one per line in resource-keys.txt (lines starting with # are
// comments). Nothing when the file is missing: the check is then skipped and the problems say so.
std::optional<std::set<std::string>> ReadAllowedKeys(const std::filesystem::path& file);

}  // namespace ampify::theme
