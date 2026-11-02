#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "css.hpp"
#include "theme.hpp"
#include "tree.hpp"

namespace ampify::theme {

// sidebar.json and home.json, Spicetify's sidebar_config and home_config: which entries of the sidebar (or sections of
// the home page) are hidden, and in which order they come. The files sit in the Ampify folder, next to settings.txt:
//
//   {
//     "version": 1,
//     "reorder": true,
//     "items": [
//       {"name": "ListenNowItem", "hidden": false},
//       {"name": "RadioItem", "hidden": true}
//     ]
//   }
//
// Each item becomes rules of the session: `hidden` gives `visibility: collapsed`, and with "reorder": true the place in
// the list gives `order: <index>`. The sidebar targets NavigationViewItem#<name>; the home page targets #<name>.
// The names are the x:Name of the elements: read them in inspector.txt or inspector.json on the PC.

enum class LayoutKind { Sidebar, Home };

struct LayoutItem {
    std::string name;
    bool hidden = false;
};

struct LayoutConfig {
    bool reorder = false;  // false: only hides, the order of the app stays
    std::vector<LayoutItem> items;
};

// The file name in the Ampify folder: sidebar.json or home.json.
std::string_view LayoutFileName(LayoutKind kind);

// Reads a layout file. Bad items are skipped and reported (one message each). Nothing when the text is not a
// JSON object with an "items" list.
std::optional<LayoutConfig> ParseLayout(std::string_view json, std::vector<std::string>& problems);

// The text of the file, one item per line, as ParseLayout reads it back.
std::string WriteLayout(const LayoutConfig& config);

// The generator. Keeps the items of `existing`, in their order and with their state, then adds each name of `names`
// it does not hold yet, visible, in the order given. An item that is not in `names` is kept: the page may simply be
// closed. A new file reorders (its list is complete); an existing file keeps its choice.
LayoutConfig GenerateLayout(const std::optional<LayoutConfig>& existing, const std::vector<std::string>& names);

// The x:Name of every element matching `selector`, in tree order, each once. With `NavigationViewItem` it lists the
// entries of the sidebar, which is what the generator needs on the PC.
std::vector<std::string> NamesMatching(const std::vector<NodeInfo>& nodes, const Selector& selector);

// The rules of one file, added to `out` (whose id and name stay as they are). Their source is the file name.
void AddLayoutRules(const LayoutConfig& config, LayoutKind kind, Theme& out);

}  // namespace ampify::theme
