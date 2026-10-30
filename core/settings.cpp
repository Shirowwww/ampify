#include "settings.hpp"

#include <cctype>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace amk {

namespace {

std::string Trim(const std::string& text) {
    size_t first = 0;
    size_t last = text.size();
    while (first < last && std::isspace(static_cast<unsigned char>(text[first]))) first++;
    while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1]))) last--;
    return text.substr(first, last - first);
}

}  // namespace

bool Settings::Load(const std::string& path) {
    m_values.clear();

    std::ifstream in(path);
    if (!in) return !std::filesystem::exists(path);

    std::string line;
    while (std::getline(in, line)) {
        std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;

        size_t equals = trimmed.find('=');
        if (equals == std::string::npos) continue;

        std::string key = Trim(trimmed.substr(0, equals));
        std::string value = Trim(trimmed.substr(equals + 1));
        if (!key.empty()) m_values[key] = value;
    }
    return true;
}

bool Settings::Save(const std::string& path) const {
    std::filesystem::path target(path);
    std::filesystem::path temporary = target;
    temporary += ".tmp";

    {
        std::ofstream out(temporary, std::ios::trunc);
        if (!out) return false;
        out << "# Apple Music Mod Kit settings. Edit by hand if you like.\n";
        for (const auto& [key, value] : m_values) out << key << " = " << value << "\n";
        if (!out) return false;
    }

    std::error_code error;
    std::filesystem::rename(temporary, target, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
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

    std::string lowered;
    for (char c : *value) lowered += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
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

}  // namespace amk
