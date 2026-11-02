#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ampify::theme {

// Which colour scheme of colors.ini to use, from the settings, the Windows mode (light or dark) and the time of day.
// Pure functions: the Windows side gives the mode and the local time, the tests give any.
//
//   theme.schedule = 07:00=Light,19:00=Dark   the scheme follows the local time (it wins over theme.scheme)
//   theme.scheme   = auto                     the scheme follows the Windows mode
//   theme.scheme   = Dark                     this scheme
//   theme.scheme   =                          the theme's defaultScheme (manifest.json), or the first scheme
//
// For `auto`, a dark scheme is a name such as Dark, Sombre, Night or Nuit (or one that contains dark, sombre, night or
// nuit), a light scheme a name such as Light, Clair, Day or Jour (or one that contains light or clair). In light mode,
// with no light name, the first scheme that is not dark is used: [Base] with [Dark] works as is.

struct ScheduleEntry {
    int minute = 0;      // from midnight, 0 to 1439
    std::string scheme;  // a scheme name, or `auto`
};

struct Schedule {
    std::vector<ScheduleEntry> entries;  // sorted by time
    std::vector<std::string> problems;   // one per bad entry; the good entries are kept
};

// `HH:MM=Scheme`, separated by commas or semicolons. An empty text gives an empty schedule.
Schedule ParseSchedule(std::string_view text);

// The scheme of the last entry at or before `minute`. Before the first entry of the day, the last one of the previous
// day still holds. Empty when the schedule is empty.
std::string ScheduledScheme(const Schedule& schedule, int minute);

// The scheme of `schemes` that suits the Windows mode, as described above. Empty when `schemes` is empty, or when dark
// mode is asked and no scheme is dark (then the first scheme, the default, stays).
std::string SystemScheme(const std::vector<std::string>& schemes, bool dark);

// True when the name is a dark scheme by the rule above (Dark, Sombre, Night, Nuit). The app is then switched to its
// own dark mode under the theme, so the brushes the theme does not name match the ones it does.
bool IsDarkScheme(const std::string& scheme);

// The app mode a theme asks for: dark or light from its background when it has one, else from its scheme's name, else
// none, and Apple Music keeps its own (a theme that only hides buttons must not turn the app light).
std::optional<bool> AppModeOf(std::optional<bool> dark, const std::string& scheme);

// What the Windows side reports, read on each pass of the worker.
struct SchemeInputs {
    bool systemDark = false;  // Windows apps in dark mode (AppsUseLightTheme = 0)
    int minute = 0;           // local time, minutes from midnight
};

struct SchemeChoice {
    std::string scheme;                 // the name to pass to LoadTheme; empty takes the first scheme
    std::string reason;                 // English, for the log: "schedule 19:00", "Windows dark mode"...
    std::vector<std::string> problems;  // a bad schedule entry, a scheme that colors.ini does not have
};

// `scheme` and `schedule` are the values of theme.scheme and theme.schedule. `schemes` are the sections of colors.ini,
// `defaultScheme` the one the theme's manifest names, used when neither setting picks a scheme.
SchemeChoice ChooseScheme(const std::string& scheme, const std::string& schedule, const std::vector<std::string>& schemes,
                          const SchemeInputs& inputs, const std::string& defaultScheme = {});

}  // namespace ampify::theme
