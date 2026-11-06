#include "settings.hpp"

#include <cctype>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <system_error>

#include "text_util.hpp"

namespace ampify {

namespace {

// The file was written in text mode, so Windows gave it CRLF lines; the bytes stay the same.
#ifdef _WIN32
constexpr const char* kLineEnd = "\r\n";
#else
constexpr const char* kLineEnd = "\n";
#endif

}  // namespace

bool Settings::Load(const std::string& path) {
    m_values.clear();

    // Linux opens a directory as a stream that reads nothing: it must fail like on Windows, or a save would follow.
    std::error_code ignored;
    if (std::filesystem::exists(path, ignored) && !std::filesystem::is_regular_file(path, ignored)) return false;
    std::ifstream in(path);
    if (!in) return !std::filesystem::exists(path);

    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        if (first) line = std::string(WithoutBom(line));
        first = false;
        std::string trimmed(TrimSpace(line));
        if (trimmed.empty() || trimmed[0] == '#') continue;

        size_t equals = trimmed.find('=');
        if (equals == std::string::npos) continue;

        std::string key(TrimSpace(std::string_view(trimmed).substr(0, equals)));
        std::string value(TrimSpace(std::string_view(trimmed).substr(equals + 1)));
        if (!key.empty()) m_values[key] = value;
    }
    return !in.bad();
}

bool Settings::Save(const std::string& path) const {
    std::string text = std::string("# Ampify settings. Edit by hand if you like.") + kLineEnd;
    for (const auto& [key, value] : m_values) text += key + " = " + value + kLineEnd;
    return WriteFileAtomically(std::filesystem::path(path), text) == WriteResult::Written;
}

std::optional<std::string> Settings::Get(const std::string& key) const {
    auto it = m_values.find(key);
    if (it == m_values.end()) return std::nullopt;
    return it->second;
}

std::string Settings::GetString(const std::string& key, const std::string& fallback) const {
    return Get(key).value_or(fallback);
}

bool Settings::GetBool(const std::string& key, bool fallback) const {
    std::optional<std::string> value = Get(key);
    if (!value) return fallback;

    std::string lowered = ToLowerAscii(*value);
    if (lowered == "1" || lowered == "true" || lowered == "on" || lowered == "yes") return true;
    if (lowered == "0" || lowered == "false" || lowered == "off" || lowered == "no") return false;
    return fallback;
}

int Settings::GetInt(const std::string& key, int fallback) const {
    std::optional<std::string> value = Get(key);
    if (!value) return fallback;

    int result = fallback;
    const char* first = value->data();
    const char* last = first + value->size();
    auto [end, error] = std::from_chars(first, last, result);
    if (error != std::errc() || end != last) return fallback;
    return result;
}

double Settings::GetDouble(const std::string& key, double fallback) const {
    std::optional<std::string> value = Get(key);
    if (!value) return fallback;

    double result = fallback;
    const char* first = value->data();
    const char* last = first + value->size();
    auto [end, error] = std::from_chars(first, last, result);
    if (error != std::errc() || end != last) return fallback;
    return result;
}

void Settings::Set(const std::string& key, const std::string& value) {
    m_values[key] = value;
}

void Settings::SetBool(const std::string& key, bool value) {
    Set(key, value ? "1" : "0");
}

void Settings::SetInt(const std::string& key, int value) {
    Set(key, std::to_string(value));
}

void Settings::Remove(const std::string& key) {
    m_values.erase(key);
}

bool Settings::UpdateFile(const std::string& path, const std::function<void(Settings&)>& change, std::string& error) {
    static std::mutex turns;
    std::lock_guard<std::mutex> lock(turns);
    Settings current;
    if (!current.Load(path)) {
        error = "cannot read: " + path;
        return false;
    }
    change(current);
    if (!current.Save(path)) {
        error = "cannot write: " + path;
        return false;
    }
    return true;
}

bool Settings::UpdateFile(const std::string& path, const std::string& key, const std::string& value,
                          std::string& error) {
    return UpdateFile(path, [&](Settings& settings) { settings.Set(key, value); }, error);
}

}  // namespace ampify
