#include "reliability.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <optional>
#include <system_error>
#include <utility>

#include "text_util.hpp"

namespace ampify {

namespace {

constexpr uint64_t kMegabyte = 1024 * 1024;

struct ConflictRule {
    const char* id;
    const char* name;
    const char* prefix;  // of the lower-case image name
    const char* exact;   // or the whole lower-case image name
    const char* advice;
};

// MSI Afterburner alone draws nothing in other programs: its on-screen display is RivaTuner's, so only RTSS counts.
constexpr std::array<ConflictRule, 2> kConflicts = {{
    {"3dconnexion", "3Dconnexion 3DxWare (SpaceMouse, CadMouse)", "3dx", nullptr,
     "Apple Music closes as soon as the driver reaches it. Add <ExecutableName>AppleMusic.exe</ExecutableName> to "
     "C:\\Program Files\\3Dconnexion\\3DxWare\\3DxWinCore\\Cfg\\IgnoredApps.xml (as an administrator), then start "
     "Apple Music again"},
    {"rivatuner", "RivaTuner Statistics Server (MSI Afterburner's on-screen display)", nullptr, "rtss.exe",
     "add AppleMusic.exe as an application profile in RivaTuner with Application detection level set to None"},
}};

bool Matches(const ConflictRule& rule, const std::string& image) {
    if (rule.exact) return image == rule.exact;
    std::string_view prefix(rule.prefix);
    return image.size() > prefix.size() && image.compare(0, prefix.size(), prefix) == 0 &&
           image.size() > 4 && image.compare(image.size() - 4, 4, ".exe") == 0;
}

}  // namespace

std::string SlowCallLog::Record(std::string_view what, int64_t ms, bool onInterfaceThread) {
    if (ms < kSlowCallMs) return {};
    std::lock_guard<std::mutex> guard(m_lock);
    if (m_lines >= kSlowCallLines) return {};
    ++m_lines;
    std::string line = "Network: " + std::string(what) + " took " + std::to_string(ms) + " ms";
    if (onInterfaceThread) line += ", on the interface thread";
    if (m_lines == kSlowCallLines) line += " (further slow network calls are not logged)";
    return line;
}

MemoryWatch::MemoryWatch(std::string process) : m_process(std::move(process)) {}

std::string MemoryWatch::Check(uint64_t privateBytes) {
    if (privateBytes == 0) return {};
    bool first = m_nextStep == 0;
    if (!first && privateBytes < m_nextStep) return {};
    m_nextStep = (privateBytes / kMemoryStepBytes + 1) * kMemoryStepBytes;
    std::string size = std::to_string(privateBytes / kMegabyte) + " MB";
    return "Memory: " + m_process + (first ? " uses " : " grew to ") + size;
}

std::vector<KnownConflict> FindKnownConflicts(const std::vector<std::string>& processes, bool threeDxIgnoresAppleMusic) {
    std::vector<KnownConflict> found;
    for (const ConflictRule& rule : kConflicts) {
        if (threeDxIgnoresAppleMusic && std::string_view(rule.id) == "3dconnexion") continue;
        for (const std::string& process : processes) {
            if (!Matches(rule, ToLowerAscii(process))) continue;
            found.push_back({rule.id, rule.name, rule.advice});
            break;
        }
    }
    return found;
}

bool IgnoredAppsListsAppleMusic(std::string_view xml) {
    const std::string text = ToLowerAscii(xml);
    constexpr std::string_view open = "<executablename>";
    constexpr std::string_view close = "</executablename>";
    for (size_t at = text.find(open); at != std::string::npos; at = text.find(open, at + 1)) {
        size_t begin = at + open.size();
        size_t end = text.find(close, begin);
        if (end == std::string::npos) return false;
        if (TrimSpace(std::string_view(text).substr(begin, end - begin)) == "applemusic.exe") return true;
    }
    return false;
}

bool ThreeDxIgnoresAppleMusic(const std::filesystem::path& programFiles) {
    std::optional<std::string> text =
        ReadWholeFile(programFiles / "3Dconnexion" / "3DxWare" / "3DxWinCore" / "Cfg" / "IgnoredApps.xml");
    return text && IgnoredAppsListsAppleMusic(*text);
}

std::vector<std::string> RecentAppleCrashDumps(const std::filesystem::path& folder, int days) {
    namespace fs = std::filesystem;
    const auto since = fs::file_time_type::clock::now() - std::chrono::hours(24) * days;
    std::vector<std::pair<fs::file_time_type, std::string>> found;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(folder, ec)) {
        std::string name = ToLowerAscii(entry.path().filename().string());
        bool apple = name.rfind("applemusic.exe.", 0) == 0 || name.rfind("amplibraryagent.exe.", 0) == 0;
        if (!apple || entry.path().extension() != ".dmp") continue;
        fs::file_time_type written = entry.last_write_time(ec);
        if (!ec && written >= since) found.emplace_back(written, entry.path().filename().string());
    }
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<std::string> names;
    for (const auto& item : found) names.push_back(item.second);
    return names;
}

}  // namespace ampify
