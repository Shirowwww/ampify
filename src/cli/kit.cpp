#include "kit.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <system_error>

#include "../core/kit_layout.hpp"
#include "../core/settings_catalog.hpp"
#include "../core/text_util.hpp"

namespace ampify {

namespace {

// Splits `extensions.<id>.enabled` into its kind and its id. Empty kind when the key has another shape. A theme has no
// such key: theme.active names the one on.
std::pair<std::string, std::string> ParseEnabledKey(const std::string& key) {
    const std::string suffix = "." + std::string(kEnabledKey);
    for (const ElementKindTraits& traits : kElementKinds) {
        if (traits.kind == ElementKind::Theme) continue;
        const std::string prefix = std::string(traits.folder) + ".";
        if (key.size() <= prefix.size() + suffix.size() || !key.starts_with(prefix) || !key.ends_with(suffix)) continue;
        return {std::string(traits.name), key.substr(prefix.size(), key.size() - prefix.size() - suffix.size())};
    }
    return {};
}

}  // namespace

std::string_view FolderOfKind(std::string_view kind) {
    std::optional<ElementKind> known = KindNamed(kind);
    return known ? TraitsOf(*known).folder : std::string_view();
}

std::vector<InstalledElement> ListInstalled(const KitLayout& kit) {
    std::vector<InstalledElement> elements;
    for (std::string_view kind : kElementKindNames) {
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
    std::optional<ElementKind> known = KindNamed(kind);
    return known && IsElementOn(settings, {*known, std::string(id)});
}

void SetEnabled(Settings& settings, std::string_view kind, std::string_view id, bool enabled) {
    if (kind == "theme") {
        if (enabled) {
            settings.Set("theme.active", std::string(id));
        } else if (ActiveThemeId(settings) == id) {
            settings.Set("theme.active", "");
        }
        return;
    }
    if (std::optional<ElementKind> known = KindNamed(kind)) {
        settings.SetBool(ElementRef{*known, std::string(id)}.EnabledKey(), enabled);
    }
}

std::string ReloadTarget(std::string_view kind, std::string_view id) {
    return ElementRef{KindNamed(kind).value_or(ElementKind::Extension), std::string(id)}.HostId();
}

bool AppendReloadRequests(const KitLayout& kit, const std::vector<std::string>& lines, std::string& error) {
    std::string text;
    for (const std::string& line : lines) text += line + "\n";

    std::error_code ec;
    uintmax_t size = std::filesystem::file_size(kit.Reload(), ec);
    if (ec || size > kReloadFileLimit) return WriteTextFile(kit.Reload(), text, error);

    // One write per call, so the kernel sees whole lines; a line it reads half-written waits for its newline.
    std::ofstream out(kit.Reload(), std::ios::binary | std::ios::app);
    out << text;
    out.flush();
    if (!out) {
        error = "cannot write: " + kit.Reload().string();
        return false;
    }
    return true;
}

ActiveReport ComputeActive(const KitLayout& kit, const Settings& settings) {
    ActiveReport report;

    for (const KitElement& element : kKitElements) {
        if (!IsBuiltInExtension(element.id) || !IsEnabled(settings, "extension", element.id)) continue;
        report.items.push_back({"extension", std::string(element.id), "builtin", ""});
    }

    std::string themeId = ActiveThemeId(settings);
    bool themeFound = false;
    for (const InstalledElement& element : ListInstalled(kit)) {
        if (element.kind == "theme" && element.id == themeId) themeFound = true;
        if (!IsEnabled(settings, element.kind, element.id)) continue;

        if (!element.Valid()) {
            report.problems.push_back(element.kind + " " + element.id + ": " + element.errors.front());
            continue;
        }
        ActiveItem item{element.kind, element.id, element.manifest->version, ""};
        if (element.kind == "theme") {
            item.scheme = settings.GetString("theme.scheme", element.manifest->defaultScheme.value_or(""));
        }
        report.items.push_back(std::move(item));
    }

    if (!themeId.empty() && !themeFound) {
        report.problems.push_back("theme " + themeId + ": folder not found in Ampify");
    }

    // A key that says "on" for something the kit does not have is reported, not silently ignored.
    for (const auto& [key, value] : settings.Values()) {
        auto [kind, id] = ParseEnabledKey(key);
        if (kind.empty() || !IsElementId(id) || !settings.GetBool(key, false)) continue;
        if (kind == "extension" && IsBuiltInExtension(id)) continue;
        bool installed = std::any_of(report.items.begin(), report.items.end(),
                                     [&](const ActiveItem& item) { return item.kind == kind && item.id == id; });
        if (!installed && !std::filesystem::exists(kit.Element(kind, id))) {
            report.problems.push_back(kind + " " + id + ": enabled, but missing from Ampify");
        }
    }
    return report;
}

std::string FormatActive(const ActiveReport& report) {
    std::string text = "# Written by ampify apply. The kernel reads this file again when Apple Music starts.\n";
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
        error = "cannot create folder: " + file.parent_path().string();
        return false;
    }

    switch (WriteFileAtomically(file, text)) {
        case WriteResult::CannotWrite: error = "cannot write: " + file.string(); return false;
        case WriteResult::CannotReplace: error = "cannot replace: " + file.string(); return false;
        case WriteResult::Written: break;
    }
    return true;
}

}  // namespace ampify
