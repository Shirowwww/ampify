#pragma once

// The order of the sidebar's entries and of the buttons at the right of the playback bar, chosen on Ampify's
// settings page. The sidebar keeps its order through sidebar.json, which the theme engine applies (`order`); the
// buttons through apple.order.playbar, which this keeps in place in the main window (core/user_order.hpp).

#if defined(AMPIFY_WITH_XAML)

#include <string>
#include <vector>

namespace ampify::win {

// An entry as the settings page lists it: its key (what the order is written with) and what it says.
struct OrderEntry {
    std::string key;
    std::string label;
};

// The top-level entries of the sidebar, as they stand now. Key: the x:Name, else Apple's AutomationId.
std::vector<OrderEntry> SidebarEntries() noexcept;

// The children of the panel at the right of the playback bar, as they stand now. Key: the x:Name (an Ampify button
// without its control number, which changes as elements load), else the type.
std::vector<OrderEntry> PlaybarEntries() noexcept;

// apple.order.playbar, from the settings (apple_tweak.cpp), and the pass that keeps it in the main window. With no
// order, the pass puts back the one Apple gave the buttons, if it had moved them.
void SetPlaybarOrder(std::vector<std::string> keys) noexcept;
void KeepPlaybarOrder() noexcept;

// The order Apple gave its sidebar and its buttons before anything moved them, by key; empty while nothing moved.
// "Apple's order" on the settings page shows it again without a restart.
void NoteSidebarOrder(const std::vector<std::string>& keys) noexcept;  // the theme engine, before its first move
std::vector<std::string> AppleSidebarOrder() noexcept;
std::vector<std::string> ApplePlaybarOrder() noexcept;

}  // namespace ampify::win

#endif
