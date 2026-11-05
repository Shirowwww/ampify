#pragma once
// The sidebar's groups (Library, Playlists) fold with the pane: the expanded ones are collapsed, and the same ones
// open again. Remembered by key, so a group that was already collapsed stays closed.

#include <algorithm>
#include <utility>
#include <vector>

namespace ampify {

template <typename Key>
class GroupMemory {
public:
    // Collapses what is expanded among `groups` (key, expanded) and returns those keys; they are remembered.
    std::vector<Key> Fold(const std::vector<std::pair<Key, bool>>& groups) {
        std::vector<Key> collapse;
        for (const auto& [key, expanded] : groups) {
            if (!expanded) continue;
            collapse.push_back(key);
            if (!Contains(key)) remembered_.push_back(key);
        }
        return collapse;
    }

    // The remembered keys, forgotten: the caller expands them again.
    std::vector<Key> Unfold() {
        std::vector<Key> expand = std::move(remembered_);
        remembered_.clear();
        return expand;
    }

    bool Holds() const {
        return !remembered_.empty();
    }

private:
    bool Contains(const Key& key) const {
        return std::find(remembered_.begin(), remembered_.end(), key) != remembered_.end();
    }

    std::vector<Key> remembered_;
};

}  // namespace ampify
