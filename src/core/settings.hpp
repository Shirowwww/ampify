#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace ampify {

// The words of a switch, in any case: 1, true, on, yes and 0, false, off, no. Nothing for any other text.
std::optional<bool> ParseToggle(std::string_view text);

// The longest text an element's setting may hold, in characters (docs/extensions/manifest.md).
inline constexpr size_t kMaxSettingText = 200;

// One line with no space at either end: settings.txt gives it back as it was written.
bool KeepsItsValue(std::string_view text);

// Persistent settings: a plain text file with one `key = value` per line, editable by hand. Blank lines and lines
// starting with '#' are ignored.
class Settings {
public:
    // A missing file is not an error: it yields empty settings.
    bool Load(const std::string& path);

    // Writes to a temporary file first, then replaces the target, so a crash mid-write
    // never leaves a half-written settings file behind.
    bool Save(const std::string& path) const;

    std::optional<std::string> Get(const std::string& key) const;
    std::string GetString(const std::string& key, const std::string& fallback) const;

    // Reads the words ParseToggle accepts; anything else gives the fallback.
    bool GetBool(const std::string& key, bool fallback) const;
    int GetInt(const std::string& key, int fallback) const;
    double GetDouble(const std::string& key, double fallback) const;

    // A key or a value with a line break is refused (false): saved, it would forge a line, and a key, of its own.
    bool Set(const std::string& key, const std::string& value);
    void SetBool(const std::string& key, bool value);
    void SetInt(const std::string& key, int value);

    void Remove(const std::string& key);

    const std::map<std::string, std::string>& Values() const { return m_values; }

    // Changes keys of the file as it is now and keeps every other key: the kernel, the Ampify page and the command line
    // share settings.txt, and saving an older copy would undo another writer's change. A file that exists but cannot
    // be read is left alone. Writers of one process take turns.
    static bool UpdateFile(const std::string& path, const std::function<void(Settings&)>& change, std::string& error);
    // One key; false with an error, and nothing written, for a key or a value that Set refuses.
    static bool UpdateFile(const std::string& path, const std::string& key, const std::string& value,
                           std::string& error);

    // The keys set and removed since Load, written onto the file as it is now (UpdateFile): a copy loaded early, then
    // changed, does not undo what another writer saved in between.
    bool SaveChanges(const std::string& path, std::string& error) const;

private:
    std::map<std::string, std::string> m_values;
    std::map<std::string, std::optional<std::string>> m_changes;  // nothing for a removed key
};

}  // namespace ampify
