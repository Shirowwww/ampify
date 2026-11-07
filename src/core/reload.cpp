#include "reload.hpp"

#include <algorithm>

#include "element_ref.hpp"
#include "text_util.hpp"

namespace ampify {

namespace {

std::optional<ReloadRequest> ParseLine(std::string_view line) {
    line = TrimSpace(line);
    if (line == "rescan") return ReloadRequest{ReloadRequest::Action::Rescan, ""};

    static const std::pair<std::string_view, ReloadRequest::Action> kVerbs[] = {
        {"start ", ReloadRequest::Action::Start},
        {"stop ", ReloadRequest::Action::Stop},
        {"restart ", ReloadRequest::Action::Restart},
    };
    for (const auto& [verb, action] : kVerbs) {
        if (line.substr(0, verb.size()) != verb) continue;
        std::string_view target = TrimSpace(line.substr(verb.size()));
        if (!IsReloadTarget(target)) return std::nullopt;
        return ReloadRequest{action, std::string(target)};
    }
    return std::nullopt;
}

// The part of `text` up to and including its last newline.
std::string_view CompleteLines(std::string_view text) {
    size_t end = text.rfind('\n');
    return end == std::string_view::npos ? std::string_view() : text.substr(0, end + 1);
}

}  // namespace

bool IsReloadTarget(std::string_view target) {
    if (target.starts_with(kAppHostPrefix)) target.remove_prefix(kAppHostPrefix.size());
    return IsElementId(target);
}

std::vector<ReloadRequest> ParseReloadRequests(std::string_view text) {
    std::vector<ReloadRequest> requests;
    text = CompleteLines(text);
    while (!text.empty()) {
        size_t end = text.find('\n');
        std::optional<ReloadRequest> request = ParseLine(text.substr(0, end));
        text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
        if (!request || std::find(requests.begin(), requests.end(), *request) != requests.end()) continue;
        requests.push_back(std::move(*request));
    }
    return requests;
}

std::vector<ReloadRequest> ReloadWatcher::Requests(const std::string& text) {
    std::string_view complete = CompleteLines(text);
    if (!m_consumed) {
        m_consumed = std::string(complete);
        return {};
    }
    if (complete == *m_consumed) return {};

    std::string_view added = complete;
    if (complete.size() > m_consumed->size() && complete.substr(0, m_consumed->size()) == *m_consumed) {
        added = complete.substr(m_consumed->size());
    }
    std::vector<ReloadRequest> requests = ParseReloadRequests(added);
    m_consumed = std::string(complete);
    return requests;
}

}  // namespace ampify
