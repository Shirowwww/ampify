#pragma once

// The kinds of element and how one element is named: in its manifest, in the kit folders, in settings.txt, in the host
// and in reload.txt. Every place that needs one of those names asks here, so a new kind is one row of kElementKinds.

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace ampify {

class Settings;

enum class ElementKind { Extension, Theme, App, Snippet };

// What the manifest of a kind may or must hold (docs/extensions/manifest.md, "Kinds").
struct ElementKindTraits {
    enum class Main { Required, Optional, Refused };

    ElementKind kind;
    std::string_view name;    // as manifest.json's "kind" writes it
    std::string_view folder;  // its folder in the kit, and the first part of its keys in settings.txt
    bool declaresSettings;
    Main main;
    // At least one of the two is required, a manifest field or a file of the folder ("user.css"); empty for none.
    std::array<std::string_view, 2> style;
};

// In the order `ampify list` shows the kinds.
inline constexpr std::array<ElementKindTraits, 4> kElementKinds = {{
    {ElementKind::Extension, "extension", "extensions", true, ElementKindTraits::Main::Required, {}},
    {ElementKind::Theme, "theme", "themes", true, ElementKindTraits::Main::Optional, {"colors", "resources"}},
    {ElementKind::App, "app", "apps", false, ElementKindTraits::Main::Required, {}},
    {ElementKind::Snippet, "snippet", "snippets", true, ElementKindTraits::Main::Refused, {"resources", "user.css"}},
}};

// The names and the folders of kElementKinds, in its order, for the loops that walk every kind.
inline constexpr std::array<std::string_view, kElementKinds.size()> kElementKindNames = [] {
    std::array<std::string_view, kElementKinds.size()> names{};
    for (size_t i = 0; i < kElementKinds.size(); i++) names[i] = kElementKinds[i].name;
    return names;
}();
inline constexpr std::array<std::string_view, kElementKinds.size()> kElementKindFolders = [] {
    std::array<std::string_view, kElementKinds.size()> folders{};
    for (size_t i = 0; i < kElementKinds.size(); i++) folders[i] = kElementKinds[i].folder;
    return folders;
}();

const ElementKindTraits& TraitsOf(ElementKind kind);
std::string_view ToString(ElementKind kind);
std::optional<ElementKind> KindNamed(std::string_view name);    // "theme" -> Theme
std::optional<ElementKind> KindOfFolder(std::string_view folder);  // "themes" -> Theme

// The id of an element, which is also its folder name: [a-z0-9][a-z0-9-]{1,39}.
bool IsElementId(std::string_view id);

// The key under an element's prefix that switches it on; no declared setting may take it.
inline constexpr std::string_view kEnabledKey = "enabled";

// A key a manifest may declare and a card or @setting may name: [a-z0-9][a-z0-9-]{0,31}, other than kEnabledKey.
bool IsDeclarableSettingKey(std::string_view key);

// One element: its kind and its id.
struct ElementRef {
    ElementKind kind = ElementKind::Extension;
    std::string id;

    // How the host and reload.txt name it: app:<id> for an app, the id otherwise, so an app and an extension of the
    // same id stay apart.
    std::string HostId() const;
    std::string_view Folder() const { return TraitsOf(kind).folder; }
    std::string SettingsPrefix() const;  // "<folder>.<id>.", before each of its keys in settings.txt
    std::string EnabledKey() const;      // SettingsPrefix() + kEnabledKey

    bool operator==(const ElementRef&) const = default;
};

// The host id of an app starts with this.
inline constexpr std::string_view kAppHostPrefix = "app:";

// Whether the user has the element on: the active theme for a theme, its enabled key for the others, which is on by
// default only for an element of kKitElements that starts on.
bool IsElementOn(const Settings& settings, const ElementRef& element);

}  // namespace ampify
