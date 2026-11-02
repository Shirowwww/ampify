#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "../core/settings.hpp"
#include "element.hpp"

namespace ampify {

// Extensions built into the kernel. They have no folder, only a settings key.
inline constexpr std::string_view kBuiltinExtensions[] = {"atmos-leveler"};

// Folder of a kind inside the kit: extension -> extensions, theme -> themes, app -> apps, snippet -> snippets.
// Empty for an unknown kind.
std::string_view FolderOfKind(std::string_view kind);

// The settings key that switches an extension, an app or a snippet on: extensions.<id>.enabled and so on.
// A theme has no such key: `theme.id` names the one active theme.
std::string EnabledKey(std::string_view kind, std::string_view id);

// How reload.txt names an element in its requests (src/core/reload.hpp): `app:<id>` for an app, `<id>` otherwise.
std::string ReloadTarget(std::string_view kind, std::string_view id);

// Above this size, AppendReloadRequests starts reload.txt again with only the new lines.
inline constexpr size_t kReloadFileLimit = 64 * 1024;

// The folders of the kit, under %USERPROFILE%\Ampify (docs/cli.md, "Ampify files").
class KitLayout {
public:
    explicit KitLayout(std::filesystem::path root) : m_root(std::move(root)) {}

    const std::filesystem::path& Root() const { return m_root; }
    std::filesystem::path Settings() const { return m_root / "settings.txt"; }
    std::filesystem::path Folder(std::string_view kind) const { return m_root / FolderOfKind(kind); }
    std::filesystem::path Element(std::string_view kind, std::string_view id) const {
        return Folder(kind) / std::string(id);
    }
    std::filesystem::path Active() const { return m_root / "active.txt"; }
    std::filesystem::path Reload() const { return m_root / "reload.txt"; }
    std::filesystem::path Backups() const { return m_root / "backups"; }
    std::filesystem::path Downloads() const { return m_root / "downloads"; }
    std::filesystem::path Cache() const { return m_root / "cache"; }
    std::filesystem::path Updates() const { return m_root / "updates"; }
    std::filesystem::path Journal() const { return m_root / "logs"; }
    std::filesystem::path Profiles() const { return m_root / "profiles"; }
    std::filesystem::path SafeModeState() const { return m_root / "state.txt"; }

private:
    std::filesystem::path m_root;
};

// An element found in the kit folders. `errors` is empty when the element is valid.
struct InstalledElement {
    std::string kind;
    std::string id;  // the folder name
    std::optional<ElementManifest> manifest;
    std::vector<std::string> errors;

    bool Valid() const { return manifest.has_value() && errors.empty(); }
};

// Every element in the kit folders, ordered by kind then id. Folders whose name starts with '.' are
// work in progress (installs, replacements) and are skipped.
std::vector<InstalledElement> ListInstalled(const KitLayout& kit);

bool IsEnabled(const Settings& settings, std::string_view kind, std::string_view id);
void SetEnabled(Settings& settings, std::string_view kind, std::string_view id, bool enabled);

// What the kernel should load, as computed from the settings and the valid elements of the kit.
struct ActiveItem {
    std::string kind;
    std::string id;
    std::string version;  // "builtin" for an extension built into the kernel
    std::string scheme;   // themes only, empty otherwise
};

struct ActiveReport {
    std::vector<ActiveItem> items;
    std::vector<std::string> problems;  // one per problem: an enabled element that cannot load
};

ActiveReport ComputeActive(const KitLayout& kit, const Settings& settings);

// The content of active.txt: one line per item, `kind id version [scheme=...]`, after a comment header.
std::string FormatActive(const ActiveReport& report);

// Writes through a temporary file, so a crash mid-write never leaves a half-written file. Creates the folder.
bool WriteTextFile(const std::filesystem::path& file, const std::string& text, std::string& error);

// Adds lines to reload.txt, such as "start demo" or "rescan". The kernel reads only the lines added since its last
// pass, so a request is never lost by a later write, unless the file was over kReloadFileLimit and starts again.
bool AppendReloadRequests(const KitLayout& kit, const std::vector<std::string>& lines, std::string& error);

}  // namespace ampify
