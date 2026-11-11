#pragma once

// The order the user gives to the entries of Apple Music's sidebar and to the buttons at the right of the playback
// bar (apple.order.sidebar, apple.order.playbar): the entries' keys, joined by '|'. Apple's entries have no x:Name in
// Apple Music 1.1792, so an entry's key is what it says (its label), and a button's its name or its type. Pure
// arithmetic, for src/win/user_order.cpp and its tests.

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ampify {

inline constexpr char kOrderSeparator = '|';

// The keys of a setting, in order; empty ones are dropped, so an empty setting is Apple's own order.
inline std::vector<std::string> ParseOrder(std::string_view text) {
    std::vector<std::string> keys;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find(kOrderSeparator, start);
        if (end == std::string_view::npos) end = text.size();
        if (end > start) keys.emplace_back(text.substr(start, end - start));
        start = end + 1;
    }
    return keys;
}

inline std::string JoinOrder(const std::vector<std::string>& keys) {
    std::string text;
    for (const std::string& key : keys) {
        if (key.empty() || key.find(kOrderSeparator) != std::string::npos) continue;
        if (!text.empty()) text += kOrderSeparator;
        text += key;
    }
    return text;
}

// The new order of `current` (the indices of its items, in the order they should show): the items `wanted` names
// take the places such items hold now, in the wanted order; the others keep their place. An entry added later (an
// app installed after the order was chosen) so stays where Apple or Ampify puts it, and a named entry that went is
// skipped. Two items with the same key keep their order between them.
inline std::vector<std::size_t> UserOrder(const std::vector<std::string>& current,
                                          const std::vector<std::string>& wanted) {
    std::vector<std::size_t> order(current.size());
    std::vector<std::size_t> slots;   // the places of the named items, left to right
    std::vector<std::size_t> named;   // those items, by their rank in `wanted`
    std::vector<std::size_t> ranks;
    for (std::size_t index = 0; index < current.size(); index++) {
        order[index] = index;
        auto found = std::find(wanted.begin(), wanted.end(), current[index]);
        if (found == wanted.end()) continue;
        slots.push_back(index);
        named.push_back(index);
        ranks.push_back(static_cast<std::size_t>(found - wanted.begin()));
    }
    std::vector<std::size_t> byRank(named.size());
    for (std::size_t i = 0; i < byRank.size(); i++) byRank[i] = i;
    std::stable_sort(byRank.begin(), byRank.end(), [&](std::size_t a, std::size_t b) { return ranks[a] < ranks[b]; });
    for (std::size_t i = 0; i < slots.size(); i++) order[slots[i]] = named[byRank[i]];
    return order;
}

}  // namespace ampify
