#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ampify {

enum class ElementKind { Extension, Theme, App, Snippet };

// A setting an extension declares: the Ampify card in Apple Music's settings shows it, and Ampify.config.get(key)
// gives its default until the user changes it. The value lives in settings.txt as extensions.<id>.<key>.
struct SettingSpec {
    enum class Type { Toggle, Choice, Text, Number };
    std::string key;
    std::string label;
    Type type = Type::Toggle;
    std::string defaultValue;          // as settings.txt holds it: true or false, the number, or the text
    std::vector<std::string> options;  // a choice's values
    std::optional<double> min;         // a number's bounds
    std::optional<double> max;
};

// The fields of a manifest.json that the kernel uses. The rules are those of docs/extensions/manifest.md, which
// tools/validate.py also enforces. The kernel checks them again, because a folder can be edited by hand.
struct Manifest {
    std::string id;
    std::string name;
    std::string version;
    ElementKind kind = ElementKind::Extension;
    std::string author;
    std::string description;
    std::string kitApi;
    std::optional<std::string> homepage;
    std::optional<std::string> license;
    std::optional<std::string> main;
    std::optional<std::string> colors;
    std::optional<std::string> defaultScheme;
    std::optional<std::string> resources;
    std::optional<std::string> icon;
    std::vector<std::string> permissions;
    std::vector<std::string> appVersions;
    // Where the element runs: "ui" (AppleMusic.exe), "engine" (AMPLibraryAgent.exe), or both.
    std::vector<std::string> processes = {"ui"};  // the interface only, unless the manifest asks for the engine too
    // "type": "module" runs main as an ES module (static import). "script", the default, runs it as a classic script.
    bool module = false;
    std::vector<SettingSpec> settings;  // in manifest order

    bool HasPermission(std::string_view permission) const;
    const SettingSpec* Setting(std::string_view key) const;
    bool RunsIn(std::string_view process) const;

    // The hosts named by `network:<host>` permissions, in manifest order.
    std::vector<std::string> NetworkHosts() const;
};

struct ManifestResult {
    std::optional<Manifest> manifest;  // set only when there are no errors
    std::vector<std::string> errors;   // English messages, one per problem
    std::vector<std::string> warnings; // unknown fields are ignored, with a warning
};

// Parses and checks the text of a manifest.json. `folderName` must match `id`. `hasFile` tells whether a file
// named in the manifest exists in the element's folder; it is a plain name such as "main.js", never a path.
ManifestResult ParseManifest(std::string_view text, std::string_view folderName, int supportedKitApiMajor,
                             const std::function<bool(const std::string&)>& hasFile);

// The rules of the settings field alone, for `ampify validate`, which reads the other fields itself.
std::vector<std::string> SettingsErrors(std::string_view manifestText);

// Accepts `storage`, `player.read`, ..., or `network:<host>` with a lowercase host name.
bool IsKnownPermission(std::string_view permission);

std::string_view ToString(ElementKind kind);

// The text a number takes in settings.txt, as JavaScript's String(n) writes ordinary values: 12, 0.5.
std::string SettingNumberText(double value);

}  // namespace ampify
