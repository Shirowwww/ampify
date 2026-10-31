#pragma once

#include <map>
#include <optional>
#include <string>

namespace ampify {

// Persistent settings: a plain text file with one `key = value` per line,
// readable and editable by hand. Blank lines and lines starting with '#' are ignored.
class Settings {
public:
    // A missing file is not an error: it simply yields empty settings.
    bool Load(const std::string& path);

    // Writes to a temporary file first, then replaces the target, so a crash mid-write
    // never leaves a half-written settings file behind.
    bool Save(const std::string& path) const;

    std::optional<std::string> Get(const std::string& key) const;
    std::string GetString(const std::string& key, const std::string& fallback) const;

    // Accepts 1/0, true/false, on/off, yes/no. Anything else gives the fallback.
    bool GetBool(const std::string& key, bool fallback) const;
    int GetInt(const std::string& key, int fallback) const;
    double GetDouble(const std::string& key, double fallback) const;

    void Set(const std::string& key, const std::string& value);
    void SetBool(const std::string& key, bool value);
    void SetInt(const std::string& key, int value);

    // Forgets the key. A missing key is not an error.
    void Remove(const std::string& key);

    // Every key, sorted by name, as read from the file and set since.
    const std::map<std::string, std::string>& Values() const { return m_values; }

private:
    std::map<std::string, std::string> m_values;
};

}  // namespace ampify
