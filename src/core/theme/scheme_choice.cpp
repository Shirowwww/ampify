#include "scheme_choice.hpp"

#include <algorithm>
#include <cctype>

#include "../text_util.hpp"

namespace ampify::theme {

namespace {

// "7:00" or "07:00" gives 420. Nothing for anything else.
int ParseTime(std::string_view text) {
    size_t colon = text.find(':');
    if (colon == std::string_view::npos || colon == 0 || colon > 2 || text.size() - colon != 3) return -1;
    std::string_view hours = text.substr(0, colon);
    std::string_view minutes = text.substr(colon + 1);
    auto digits = [](std::string_view part) {
        return std::all_of(part.begin(), part.end(), [](char c) { return c >= '0' && c <= '9'; });
    };
    if (!digits(hours) || !digits(minutes)) return -1;
    int h = std::stoi(std::string(hours));
    int m = std::stoi(std::string(minutes));
    if (h > 23 || m > 59) return -1;
    return h * 60 + m;
}

std::string TimeText(int minute) {
    std::string text = std::to_string(minute / 60) + ":" + (minute % 60 < 10 ? "0" : "") + std::to_string(minute % 60);
    return text.size() == 4 ? "0" + text : text;
}

bool IsNamed(const std::string& scheme, std::initializer_list<const char*> exact, std::initializer_list<const char*> parts) {
    std::string name = ToLowerAscii(scheme);
    for (const char* word : exact) {
        if (name == word) return true;
    }
    for (const char* word : parts) {
        if (name.find(word) != std::string::npos) return true;
    }
    return false;
}

bool IsDark(const std::string& scheme) {
    return IsNamed(scheme, {"dark", "night"}, {"dark", "night"});
}

bool IsLight(const std::string& scheme) {
    return IsNamed(scheme, {"light", "day"}, {"light"});
}

// The name as colors.ini writes it, or empty when the theme has no such scheme (names are case-insensitive).
std::string FindScheme(const std::vector<std::string>& schemes, const std::string& wanted) {
    for (const std::string& scheme : schemes) {
        if (ToLowerAscii(scheme) == ToLowerAscii(wanted)) return scheme;
    }
    return {};
}

}  // namespace

Schedule ParseSchedule(std::string_view text) {
    Schedule schedule;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find_first_of(",;", start);
        if (end == std::string_view::npos) end = text.size();
        std::string_view piece = TrimSpace(text.substr(start, end - start));
        start = end + 1;
        if (piece.empty()) continue;

        size_t equals = piece.find('=');
        int minute = equals == std::string_view::npos ? -1 : ParseTime(TrimSpace(piece.substr(0, equals)));
        std::string scheme = equals == std::string_view::npos ? std::string() : std::string(TrimSpace(piece.substr(equals + 1)));
        if (minute < 0 || scheme.empty()) {
            schedule.problems.push_back("theme.schedule: \"" + std::string(piece) + "\" ignored (write HH:MM=Scheme)");
            continue;
        }
        bool replaced = false;
        for (ScheduleEntry& entry : schedule.entries) {
            if (entry.minute != minute) continue;
            schedule.problems.push_back("theme.schedule: " + TimeText(minute) + " given twice, the last one counts");
            entry.scheme = scheme;
            replaced = true;
        }
        if (!replaced) schedule.entries.push_back({minute, scheme});
    }
    std::stable_sort(schedule.entries.begin(), schedule.entries.end(),
                     [](const ScheduleEntry& a, const ScheduleEntry& b) { return a.minute < b.minute; });
    return schedule;
}

std::string ScheduledScheme(const Schedule& schedule, int minute) {
    if (schedule.entries.empty()) return {};
    const ScheduleEntry* current = &schedule.entries.back();  // still in force from the previous day
    for (const ScheduleEntry& entry : schedule.entries) {
        if (entry.minute <= minute) current = &entry;
    }
    return current->scheme;
}

std::string SystemScheme(const std::vector<std::string>& schemes, bool dark) {
    if (dark) {
        for (const std::string& scheme : schemes) {
            if (IsDark(scheme)) return scheme;
        }
        return {};
    }
    for (const std::string& scheme : schemes) {
        if (IsLight(scheme)) return scheme;
    }
    for (const std::string& scheme : schemes) {
        if (!IsDark(scheme)) return scheme;
    }
    return {};
}

SchemeChoice ChooseScheme(const std::string& scheme, const std::string& schedule, const std::vector<std::string>& schemes,
                          const SchemeInputs& inputs, const std::string& defaultScheme) {
    SchemeChoice choice;
    std::string wanted = std::string(TrimSpace(scheme));

    Schedule parsed = ParseSchedule(schedule);
    choice.problems = parsed.problems;
    if (!parsed.entries.empty()) {
        wanted = ScheduledScheme(parsed, inputs.minute);
        const ScheduleEntry* entry = &parsed.entries.back();
        for (const ScheduleEntry& candidate : parsed.entries) {
            if (candidate.minute <= inputs.minute) entry = &candidate;
        }
        choice.reason = "schedule " + TimeText(entry->minute);
    }
    if (wanted.empty() && !TrimSpace(defaultScheme).empty()) {
        wanted = std::string(TrimSpace(defaultScheme));
        choice.reason = "the theme's default";
    }

    if (ToLowerAscii(wanted) == "auto") {
        choice.scheme = SystemScheme(schemes, inputs.systemDark);
        std::string mode = inputs.systemDark ? "dark" : "light";
        choice.reason += (choice.reason.empty() ? "" : ", ") + std::string("Windows ") + mode + " mode";
        if (choice.scheme.empty() && !schemes.empty()) {
            choice.problems.push_back("no " + mode + " scheme in colors.ini (Dark, Night, Light, Day...), the first one is used");
        }
        return choice;
    }

    if (wanted.empty()) {
        if (choice.reason.empty()) choice.reason = "first scheme";
        return choice;
    }
    choice.scheme = FindScheme(schemes, wanted);
    if (choice.reason.empty()) choice.reason = "theme.scheme";
    if (choice.scheme.empty()) {
        // LoadTheme says it too, but only the schedule knows where the name came from.
        if (!parsed.entries.empty()) choice.problems.push_back("theme.schedule: scheme \"" + wanted + "\" not found in colors.ini");
        choice.scheme = wanted;  // LoadTheme reports it and falls back to the first scheme
    }
    return choice;
}

bool IsDarkScheme(const std::string& scheme) {
    return IsDark(scheme);
}

std::optional<bool> AppModeOf(std::optional<bool> dark, const std::string& scheme) {
    if (dark) return dark;
    if (IsDark(scheme)) return true;
    if (IsLight(scheme)) return false;
    return std::nullopt;
}

std::optional<WindowMaterial> WindowMaterialOf(std::string_view value) {
    std::string name = ToLowerAscii(TrimSpace(value));
    if (name == "mica") return WindowMaterial::Mica;
    if (name == "mica-alt") return WindowMaterial::MicaAlt;
    if (name == "acrylic") return WindowMaterial::Acrylic;
    return std::nullopt;
}

}  // namespace ampify::theme
