#include "kit.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <system_error>

namespace amk {

namespace {

constexpr std::array<std::string_view, 4> kKinds = {"extension", "theme", "app", "snippet"};

bool IsBuiltinExtension(std::string_view id) {
    return std::find(std::begin(kBuiltinExtensions), std::end(kBuiltinExtensions), id) !=
           std::end(kBuiltinExtensions);
}

// Splits `extensions.<id>.enabled` into its kind and its id. Empty kind when the key has another shape.
std::pair<std::string, std::string> ParseEnabledKey(const std::string& key) {
    static const std::pair<std::string_view, std::string_view> kPrefixes[] = {
        {"extensions.", "extension"}, {"apps.", "app"}, {"snippets.", "snippet"}};
    constexpr std::string_view kSuffix = ".enabled";
    for (const auto& [prefix, kind] : kPrefixes) {
        if (key.size() <= prefix.size() + kSuffix.size()) continue;
        if (key.compare(0, prefix.size(), prefix) != 0) continue;
        if (key.compare(key.size() - kSuffix.size(), kSuffix.size(), kSuffix) != 0) continue;
        return {std::string(kind), key.substr(prefix.size(), key.size() - prefix.size() - kSuffix.size())};
    }
    return {};
}

}  // namespace

std::string_view FolderOfKind(std::string_view kind) {
    if (kind == "extension") return "extensions";
    if (kind == "theme") return "themes";
    if (kind == "app") return "apps";
    if (kind == "snippet") return "snippets";
    return {};
}

std::string EnabledKey(std::string_view kind, std::string_view id) {
    std::string_view folder = FolderOfKind(kind);
    return std::string(folder) + "." + std::string(id) + ".enabled";
}

std::vector<InstalledElement> ListInstalled(const KitLayout& kit) {
    std::vector<InstalledElement> elements;
    for (std::string_view kind : kKinds) {
        std::filesystem::path folder = kit.Folder(kind);
        std::error_code error;
        if (!std::filesystem::is_directory(folder, error)) continue;

        std::vector<std::string> ids;
        for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {
            if (!entry.is_directory(error)) continue;
            std::string name = entry.path().filename().string();
            if (name.empty() || name[0] == '.') continue;
            ids.push_back(name);
        }
        std::sort(ids.begin(), ids.end());

        for (const std::string& id : ids) {
            FolderValidation validation = ValidateElementFolder(folder / id);
            InstalledElement element;
            element.kind = std::string(kind);
            element.id = id;
            element.manifest = validation.manifest;
            element.errors = validation.errors;
            elements.push_back(std::move(element));
        }
    }
    return elements;
}

bool IsEnabled(const Settings& settings, std::string_view kind, std::string_view id) {
    if (kind == "theme") return settings.GetString("theme.id", "") == id;
    return settings.GetBool(EnabledKey(kind, id), false);
}

void SetEnabled(Settings& settings, std::string_view kind, std::string_view id, bool enabled) {
    if (kind == "theme") {
        if (enabled) {
            settings.Set("theme.id", std::string(id));
        } else if (settings.GetString("theme.id", "") == id) {
            settings.Set("theme.id", "");
        }
        return;
    }
    settings.SetBool(EnabledKey(kind, id), enabled);
}

ActiveReport ComputeActive(const KitLayout& kit, const Settings& settings) {
    ActiveReport report;

    for (std::string_view id : kBuiltinExtensions) {
        if (IsEnabled(settings, "extension", id)) report.items.push_back({"extension", std::string(id), "builtin", ""});
    }

    std::string themeId = settings.GetString("theme.id", "");
    bool themeFound = false;
    for (const InstalledElement& element : ListInstalled(kit)) {
        if (element.kind == "theme" && element.id == themeId) themeFound = true;
        if (!IsEnabled(settings, element.kind, element.id)) continue;

        if (!element.Valid()) {
            report.problems.push_back(element.kind + " " + element.id + " : " + element.errors.front());
            continue;
        }
        ActiveItem item{element.kind, element.id, element.manifest->version, ""};
        if (element.kind == "theme") {
            item.scheme = settings.GetString("theme.scheme", element.manifest->defaultScheme.value_or(""));
        }
        report.items.push_back(std::move(item));
    }

    if (!themeId.empty() && !themeFound) {
        report.problems.push_back("theme " + themeId + " : dossier introuvable dans le kit");
    }

    // A key that says "on" for something the kit does not have is reported, not silently ignored.
    for (const auto& [key, value] : settings.Values()) {
        auto [kind, id] = ParseEnabledKey(key);
        if (kind.empty() || !IsValidId(id) || !settings.GetBool(key, false)) continue;
        if (kind == "extension" && IsBuiltinExtension(id)) continue;
        bool installed = std::any_of(report.items.begin(), report.items.end(),
                                     [&](const ActiveItem& item) { return item.kind == kind && item.id == id; });
        if (!installed && !std::filesystem::exists(kit.Element(kind, id))) {
            report.problems.push_back(kind + " " + id + " : activé, mais absent du kit");
        }
    }
    return report;
}

std::string FormatActive(const ActiveReport& report) {
    std::string text = "# Généré par amk apply. Le noyau relit ce fichier au démarrage d'Apple Music.\n";
    for (const ActiveItem& item : report.items) {
        text += item.kind + " " + item.id + " " + item.version;
        if (!item.scheme.empty()) text += " scheme=" + item.scheme;
        text += "\n";
    }
    return text;
}

bool WriteTextFile(const std::filesystem::path& file, const std::string& text, std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    if (ec) {
        error = "dossier impossible à créer : " + file.parent_path().string();
        return false;
    }

    std::filesystem::path temporary = file;
    temporary += ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out << text;
        if (!out) {
            error = "écriture impossible : " + file.string();
            return false;
        }
    }
    std::filesystem::rename(temporary, file, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
        error = "remplacement impossible : " + file.string();
        return false;
    }
    return true;
}

}  // namespace amk
