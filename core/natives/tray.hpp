#pragma once

// The optional host service behind AMK.tray (permission ui.tray, setting tray.enabled). The Windows host keeps one
// notification area icon with a menu, on its own thread (win/tray.*). A click on an element's item comes back as
// dispatch.tray({"to": <id>, "item": <n>}), a left click on the icon as dispatch.tray({"click": true}).

#include <string>
#include <string_view>
#include <vector>

namespace amk {

struct TrayItem {
    int id = 0;  // chosen by the element's prelude, from 1
    std::string label;
    bool separator = false;
    bool checked = false;
    bool enabled = true;
};

inline constexpr size_t kMaxTrayItems = 20;
inline constexpr size_t kMaxTrayLabel = 64;

struct TrayHost {
    static constexpr std::string_view kCapabilityName = "tray";
    virtual ~TrayHost() = default;

    // Replaces the items of this element in the menu. An empty list removes them. False when the icon is off.
    virtual bool SetTrayMenu(const std::string& elementId, const std::vector<TrayItem>& items) = 0;
    virtual void RemoveTrayMenu(const std::string& elementId) = 0;
};

}  // namespace amk
