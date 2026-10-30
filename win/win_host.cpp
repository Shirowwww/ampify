#include "win_host.hpp"

#include <windows.h>

#include <fstream>
#include <sstream>
#include <system_error>

namespace amk {

namespace {

// The file holds a plain text key, so it is read and written as bytes.
std::optional<std::string> ReadWholeFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::nullopt;
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

}  // namespace

WinHost::WinHost(std::string process, std::filesystem::path kitDirectory, Settings& settings, Journal& journal)
    : m_process(std::move(process)), m_kit(std::move(kitDirectory)), m_settings(settings), m_journal(journal) {}

std::string WinHost::Process() const { return m_process; }

std::string WinHost::Locale() const {
    wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
    if (GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH) == 0) return "fr-FR";
    std::string narrow;
    for (const wchar_t* c = name; *c; ++c) narrow += static_cast<char>(*c);  // locale names are ASCII
    return narrow;
}

std::optional<std::string> WinHost::ReadSetting(const std::string& key) {
    return m_settings.Get(key);
}

void WinHost::WriteSetting(const std::string& key, const std::string& value) {
    m_settings.Set(key, value);
    m_settings.Save((m_kit / "settings.txt").string());
}

std::optional<std::string> WinHost::ReadStorage(const std::string& elementId) {
    return ReadWholeFile(m_kit / "storage" / (elementId + ".json"));
}

bool WinHost::WriteStorage(const std::string& elementId, const std::string& json) {
    std::error_code error;
    std::filesystem::create_directories(m_kit / "storage", error);
    std::filesystem::path target = m_kit / "storage" / (elementId + ".json");
    std::filesystem::path temporary = target;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) return false;
        file << json;
    }
    // Replace the file in one step, so that a crash mid-write keeps the previous storage.
    std::filesystem::rename(temporary, target, error);
    return !error;
}

PlayerSnapshot WinHost::Player() { return {}; }

bool WinHost::Control(PlayerCommand command, int64_t seekMs) {
    (void)command;
    (void)seekMs;
    return false;
}

void WinHost::SetGainDb(const std::string& elementId, double db) {
    (void)elementId;
    (void)db;  // not connected yet (M4): the gain stays at 0 dB
}

void WinHost::Notify(const std::string& elementId, const std::string& title, const std::string& text) {
    // Windows notifications are not connected yet: the message goes to the journal for now.
    m_journal.Write("[" + elementId + "] notification : " + title + (text.empty() ? "" : " - " + text));
}

bool WinHost::RegisterKey(const std::string& elementId, const std::string& combo) {
    (void)elementId;
    (void)combo;
    return false;  // global shortcuts are not connected yet
}

void WinHost::UnregisterKeys(const std::string& elementId) { (void)elementId; }

HttpResponse WinHost::HttpGet(const std::string& url) {
    (void)url;
    return {0, "réseau non branché dans cette version du noyau"};
}

}  // namespace amk
