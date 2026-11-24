#pragma once

// What the applied theme and snippets ask of Apple's mini player window, their --mini-window variable. "compact": while
// its lyrics and Up Next panes and its album view are closed, the window ends under the song's row, and the progress
// line and the buttons stand over that row (the Minimal mini player snippet shows them only under the pointer).
// Published by the theme service, read by the interface's.

#include <atomic>
#include <map>
#include <string>

#include "text_util.hpp"

namespace ampify {

enum class MiniWindowLayout { Apple, Compact };

inline MiniWindowLayout MiniWindowLayoutOf(const std::map<std::string, std::string>& variables) {
    auto found = variables.find("mini-window");
    if (found == variables.end()) return MiniWindowLayout::Apple;
    std::string value = ToLowerAscii(TrimSpace(found->second));
    if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front()) {
        value = value.substr(1, value.size() - 2);
    }
    return value == "compact" ? MiniWindowLayout::Compact : MiniWindowLayout::Apple;
}

inline std::atomic<MiniWindowLayout>& PublishedMiniWindowLayoutSlot() {
    static std::atomic<MiniWindowLayout> layout{MiniWindowLayout::Apple};
    return layout;
}

inline void PublishMiniWindowLayout(MiniWindowLayout layout) {
    PublishedMiniWindowLayoutSlot().store(layout);
}

inline MiniWindowLayout PublishedMiniWindowLayout() {
    return PublishedMiniWindowLayoutSlot().load();
}

}  // namespace ampify
