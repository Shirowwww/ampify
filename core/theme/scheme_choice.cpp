#include "scheme_choice.hpp"

#include <algorithm>
#include <cctype>

namespace amk::theme {

namespace {

std::string_view Trim(std::string_view text) {
    size_t first = 0;
    size_t last = text.size();
    while (first < last && std::isspace(static_cast<unsigned char>(text[first]))) first++;
    while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1]))) last--;
    return text.substr(first, last - first);
}

std::string Lowered(std::string_view text) {
    std::string result(text);
    for (char& c : result) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return result;
}

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
    std::string name = Lowered(scheme);
    for (const char* word : exact) {
        if (name == word) return true;
    }
    for (const char* word : parts) {
        if (name.find(word) != std::string::npos) return true;
    }
    return false;
}

bool IsDark(const std::string& scheme) {
    return IsNamed(scheme, {"dark", "sombre", "night", "nuit"}, {"dark", "sombre", "night", "nuit"});
}

bool IsLight(const std::string& scheme) {
    return IsNamed(scheme, {"light", "clair", "day", "jour"}, {"light", "clair"});
}

// The name as colors.ini writes it, or empty when the theme has no such scheme (names are case-insensitive).
std::string FindScheme(const std::vector<std::string>& schemes, const std::string& wanted) {
    for (const std::string& scheme : schemes) {
        if (Lowered(scheme) == Lowered(wanted)) return scheme;
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
        std::string_view piece = Trim(text.substr(start, end - start));
        start = end + 1;
        if (piece.empty()) continue;

        size_t equals = piece.find('=');
        int minute = equals == std::string_view::npos ? -1 : ParseTime(Trim(piece.substr(0, equals)));
        std::string scheme = equals == std::string_view::npos ? std::string() : std::string(Trim(piece.substr(equals + 1)));
        if (minute < 0 || scheme.empty()) {
            schedule.problems.push_back("theme.schedule : « " + std::string(piece) + " » ignoré (écrire HH:MM=Schéma)");
            continue;
        }
        bool replaced = false;
        for (ScheduleEntry& entry : schedule.entries) {
            if (entry.minute != minute) continue;
            schedule.problems.push_back("theme.schedule : " + TimeText(minute) + " donné deux fois, le dernier compte");
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
                          const SchemeInputs& inputs) {
    SchemeChoice choice;
    std::string wanted = std::string(Trim(scheme));

    Schedule parsed = ParseSchedule(schedule);
    choice.problems = parsed.problems;
    if (!parsed.entries.empty()) {
        wanted = ScheduledScheme(parsed, inputs.minute);
        const ScheduleEntry* entry = &parsed.entries.back();
        for (const ScheduleEntry& candidate : parsed.entries) {
            if (candidate.minute <= inputs.minute) entry = &candidate;
        }
        choice.reason = "horaire " + TimeText(entry->minute);
    }

    if (Lowered(wanted) == "auto") {
        choice.scheme = SystemScheme(schemes, inputs.systemDark);
        std::string mode = inputs.systemDark ? "sombre" : "clair";
        choice.reason += (choice.reason.empty() ? "" : ", ") + std::string("mode ") + mode + " de Windows";
        if (choice.scheme.empty() && !schemes.empty()) {
            choice.problems.push_back("aucun schéma " + mode + " dans colors.ini (Dark, Sombre, Light, Clair...), le premier est utilisé");
        }
        return choice;
    }

    if (wanted.empty()) {
        if (choice.reason.empty()) choice.reason = "premier schéma";
        return choice;
    }
    choice.scheme = FindScheme(schemes, wanted);
    if (choice.reason.empty()) choice.reason = "theme.scheme";
    if (choice.scheme.empty()) {
        // LoadTheme says it too, but only the schedule knows where the name came from.
        if (!parsed.entries.empty()) choice.problems.push_back("theme.schedule : schéma « " + wanted + " » absent de colors.ini");
        choice.scheme = wanted;  // LoadTheme reports it and falls back to the first scheme
    }
    return choice;
}

}  // namespace amk::theme
