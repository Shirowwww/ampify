#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "element_ref.hpp"
#include "json.hpp"
#include "permissions.hpp"

namespace ampify {

// A setting an extension or a theme declares: the Ampify page shows it. An extension reads it
// with Ampify.config.get(key), a theme's user.css with var(--setting-<key>) and @setting blocks. The value lives in
// settings.txt as extensions.<id>.<key> or themes.<id>.<key>.
struct SettingSpec {
    enum class Type { Toggle, Choice, Text, Number };
    std::string key;
    std::string label;
    Type type = Type::Toggle;
    std::string defaultValue;  // as settings.txt holds it: true or false, the number, or the text
    std::vector<std::string> options;
    std::optional<double> min;
    std::optional<double> max;
    std::string group;        // empty for none
    std::string description;  // empty for none
};

// "toggle", "choice", "text" or "number", as the manifest and the Marketplace page write a setting's type.
std::string_view ToString(SettingSpec::Type type);

// The name and description of an element in one language, from the manifest's "translations" field. The languages are
// those Ampify speaks (kTranslationLanguages); the English name and description stay the fallback.
struct ManifestText {
    std::string name;
    std::string description;
    std::map<std::string, std::string> settings;  // label of a declared setting, by its key
    std::map<std::string, std::string> options;   // text of a choice's option, by its stored value
    std::map<std::string, std::string> groups;    // heading of a group, by its name
    std::map<std::string, std::string> settingDescriptions;  // by the setting's key
};

// The fields that a manifest.json and an entry of catalog.json share.
struct ElementInfo {
    std::string id;
    std::string name;
    std::string version;
    ElementKind kind = ElementKind::Extension;
    std::string author;
    std::string description;
    std::map<std::string, ManifestText> translations;
    std::string kitApi;
    std::optional<std::string> homepage;
    std::optional<std::string> license;
    std::vector<std::string> permissions;
    std::vector<std::string> tags;  // Marketplace labels, read when authoring

    ElementRef Ref() const { return {kind, id}; }
    bool HasPermission(std::string_view permission) const;
};

// A manifest.json, as docs/extensions/manifest.md describes it. The schemas and tools/validate.py check the same rules
// before publication; the kernel checks them again, because a folder can be edited by hand.
struct Manifest : ElementInfo {
    std::optional<std::string> main;
    std::optional<std::string> colors;
    std::optional<std::string> defaultScheme;
    // A theme's place for the playback bar (top, bottom or centred) while apple.playbar leaves it to the theme.
    std::optional<std::string> playbar;
    // The oldest Ampify release this version runs on: the catalog lists it for that release and later only.
    std::optional<std::string> minAmpify;
    std::optional<std::string> resources;
    std::optional<std::string> icon;
    std::optional<std::string> preview;  // Marketplace image at the root of the folder, read when authoring
    std::vector<std::string> appVersions;
    // Where the element runs: "ui" (AppleMusic.exe), "engine" (AMPLibraryAgent.exe), or both.
    std::vector<std::string> processes = {"ui"};  // the interface only, unless the manifest asks for the engine too
    // "type": "module" runs main as an ES module (static import). "script", the default, runs it as a classic script.
    bool module = false;
    std::vector<SettingSpec> settings;

    const SettingSpec* Setting(std::string_view key) const;
    bool RunsIn(std::string_view process) const;

    // The hosts named by `network:<host>` permissions, in manifest order.
    std::vector<std::string> NetworkHosts() const;
};

// Loading takes what the kernel has always loaded. Authoring, for the command line, also refuses unknown fields and
// checks what only publication needs: the Marketplace fields (tags, preview) and the length of an address.
enum class ManifestMode { Load, Author };

struct ManifestResult {
    std::optional<Manifest> manifest;  // set only when there are no errors
    // Every field that could be read, for the readers that show what they can of a manifest with errors (the theme
    // engine, the Ampify page). Its settings are empty when the settings field has an error.
    Manifest partial;
    std::vector<std::string> errors;          // English messages, one per problem, each starting with its field
    std::vector<std::string> warnings;        // unknown fields while loading, and keys written twice
    std::vector<std::string> settingsErrors;  // those of `errors` that come from the settings field
};

// Parses and checks the text of a manifest.json. `folderName` must match `id`, unless it is empty. `hasFile` tells
// whether a file named in the manifest exists in the element's folder (a plain name such as "main.js"); without it,
// files are not checked.
ManifestResult ParseManifest(std::string_view text, std::string_view folderName, int supportedKitApiMajor,
                             const std::function<bool(const std::string&)>& hasFile,
                             ManifestMode mode = ManifestMode::Load);

// The fields of ElementInfo in `object`, under the rules of `mode`. Each message starts with `where` (such as
// "extensions[2]"), empty at the top level. Unknown fields are left to the caller.
void ReadElementInfo(const Json& object, std::string_view where, ManifestMode mode, int supportedKitApiMajor,
                     ElementInfo& info, std::vector<std::string>& errors);

// The settings a manifest declares, as ParseManifest reads them, for the theme engine; none when one breaks a rule.
std::vector<SettingSpec> DeclaredSettings(std::string_view manifestText, std::vector<std::string>* errors = nullptr);

// The value a setting has now: `stored` (from settings.txt) when it fits the spec, else the default. A toggle comes
// back as true or false, a number as ShortestNumberText writes it.
std::string SettingValue(const SettingSpec& spec, const std::optional<std::string>& stored);

// Why `stored` does not fit the spec (a toggle that is not on or off, a choice not offered, a number out of its
// bounds), empty when it fits. SettingValue takes the default for such a value.
std::string SettingProblem(const SettingSpec& spec, std::string_view stored);

// The rules of single fields, for the command line's own arguments.
bool IsReleaseVersion(std::string_view text);  // MAJOR.MINOR.PATCH
bool IsSchemeName(std::string_view text);      // a colour scheme's name, as defaultScheme takes it
bool IsMainFileName(std::string_view text);    // a .js file at the root of the folder, as main takes it
// https:// and no space, of `maxLength` characters at most when it is not 0.
bool IsHttpsAddress(std::string_view text, size_t maxLength = 0);

// The longest address authoring accepts: a homepage, a download, a catalog.
inline constexpr size_t kMaxAddressLength = 512;

}  // namespace ampify
