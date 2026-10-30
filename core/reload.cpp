#include "reload.hpp"

#include <set>

namespace amk {

namespace {

constexpr std::string_view kRestart = "restart ";

std::string_view Trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
    return text;
}

}  // namespace

std::vector<std::string> ParseRestartRequests(std::string_view text) {
    std::vector<std::string> ids;
    std::set<std::string> seen;
    while (!text.empty()) {
        size_t end = text.find('\n');
        std::string_view line = Trim(text.substr(0, end));
        text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
        if (line.substr(0, kRestart.size()) != kRestart) continue;

        std::string id(Trim(line.substr(kRestart.size())));
        if (id.empty() || !seen.insert(id).second) continue;
        ids.push_back(id);
    }
    return ids;
}

std::vector<std::string> ReloadWatcher::Changed(const std::string& text) {
    if (!m_last) {
        m_last = text;
        return {};
    }
    if (*m_last == text) return {};
    m_last = text;
    return ParseRestartRequests(text);
}

}  // namespace amk
