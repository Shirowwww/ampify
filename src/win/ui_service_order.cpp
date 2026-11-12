#if defined(AMPIFY_WITH_XAML)

// The Order group of the Look tab: the sidebar's entries, the entries Ampify adds under them and the buttons at the
// right of the playback bar, each in a list that takes a drag, or the arrows beside it for the keyboard. The
// sidebar's order goes to sidebar.json, which the theme engine applies with `order`; the added entries' and the
// buttons' to apple.order.sidebarEntries and apple.order.playbar (win/user_order.cpp).

#include "ui_service_detail.hpp"

#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

#include "../core/i18n.hpp"
#include "../core/settings.hpp"
#include "../core/settings_catalog.hpp"
#include "../core/theme/layout.hpp"
#include "../core/text_util.hpp"
#include "../core/ui_render/mods.hpp"
#include "../core/user_order.hpp"
#include "user_order.hpp"

namespace ampify::win::ui_service_detail {

namespace {

namespace animation = xaml::Media::Animation;

constexpr double kListHeight = 300;
constexpr wchar_t kUpGlyph[] = L"";    // Segoe Fluent Icons: ChevronUp
constexpr wchar_t kDownGlyph[] = L"";  // ChevronDown

// What a list orders: its entries, the order they had first, its names on the page and where the order is kept.
struct OrderKind {
    std::vector<OrderEntry> (*entries)() noexcept;
    std::vector<std::string> (*first)() noexcept;
    const char* setting;  // nullptr: sidebar.json
    const char* name;     // the list's name, before "Up", "Down" and "Reset"
    const char* label;
    const char* help;
    const char* reset;    // the button that gives the first order back: Apple's, where Apple made it
    const char* journal;
    bool elementNames;    // an Ampify button reads as its element's name: its own text can be a value ("55 %")
};

constexpr OrderKind kSidebar{SidebarEntries, AppleSidebarOrder, nullptr, "AMPIFY_OrderSidebar",
                             "order.sidebar", "order.sidebarHelp", "order.reset", "sidebar", false};
constexpr OrderKind kSidebarAdded{SidebarAddedEntries, FirstSidebarAddedOrder, kSidebarEntriesOrderKey,
                                  "AMPIFY_OrderSidebarAdded", "order.sidebarAdded", "order.sidebarAddedHelp",
                                  "settings.reset", "entries added to the sidebar", false};
constexpr OrderKind kPlaybar{PlaybarEntries, ApplePlaybarOrder, kPlaybarOrderKey, "AMPIFY_OrderPlaybar",
                             "order.playbar", "order.playbarHelp", "order.reset", "playback bar's buttons", true};

struct OrderList {
    UiService::Shared* shared = nullptr;
    const OrderKind* kind = nullptr;
    controls::ListView list{nullptr};
    winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> items{nullptr};
    std::vector<std::pair<winrt::Windows::Foundation::IInspectable, std::string>> keys;  // each item's key
};

std::vector<std::string> KeysInOrder(const OrderList& order) {
    std::vector<std::string> keys;
    for (const auto& item : order.items) {
        for (const auto& [box, key] : order.keys) {
            if (winrt::get_abi(box) == winrt::get_abi(item)) keys.push_back(key);
        }
    }
    return keys;
}

// sidebar.json keeps the entries it hides; the order follows the list, the entries it holds besides come after.
bool WriteSidebar(const std::filesystem::path& kit, const std::vector<std::string>& keys, bool reorder) {
    namespace th = theme;
    const std::filesystem::path file = kit / std::string(th::LayoutFileName(th::LayoutKind::Sidebar));
    std::optional<th::LayoutConfig> existing;
    if (std::ifstream in(file, std::ios::binary); in) {
        std::stringstream text;
        text << in.rdbuf();
        std::vector<std::string> problems;
        existing = th::ParseLayout(text.str(), problems);
    }
    th::LayoutConfig config;
    config.reorder = reorder;
    for (const std::string& key : keys) {
        th::LayoutItem item{key, false};
        if (existing) {
            for (const th::LayoutItem& old : existing->items) {
                if (old.name == key) item.hidden = old.hidden;
            }
        }
        config.items.push_back(item);
    }
    if (existing) {
        for (const th::LayoutItem& old : existing->items) {
            if (std::find(keys.begin(), keys.end(), old.name) == keys.end()) config.items.push_back(old);
        }
    }
    // Through a temporary file: the theme engine may read sidebar.json at any pass, never half written.
    return WriteFileAtomically(file, th::WriteLayout(config)) == WriteResult::Written;
}

// The list in its first order again, when Ampify moved something since Apple Music started.
void ShowAppleOrder(OrderList& order) {
    const std::vector<std::string> apple = order.kind->first();
    if (apple.empty()) return;
    const std::vector<std::string> keys = KeysInOrder(order);
    std::vector<winrt::Windows::Foundation::IInspectable> items(order.items.begin(), order.items.end());
    if (items.size() != keys.size()) return;
    const std::vector<std::size_t> placed = UserOrder(keys, apple);
    order.items.Clear();
    for (std::size_t index : placed) order.items.Append(items[index]);
}

void Save(OrderList& order, bool reset) {
    UiService::Shared& shared = *order.shared;
    if (reset) ShowAppleOrder(order);
    const std::vector<std::string> keys = KeysInOrder(order);
    bool written = false;
    std::string error;
    if (!order.kind->setting) {
        // Apple's order is written out when Ampify moved the entries, so the sidebar takes it back now.
        written = WriteSidebar(shared.kit, keys, !reset || !AppleSidebarOrder().empty());
        if (!written) error = "(sidebar.json could not be replaced)";
    } else {
        written = Settings::UpdateFile((shared.kit / "settings.txt").string(), order.kind->setting,
                                       reset ? std::string() : JoinOrder(keys), error);
    }
    shared.journal.Write(std::string("Settings: order of the ") + order.kind->journal +
                         (reset ? " put back as it was" : " changed") + (written ? "" : ", not written " + error));
}

// Moves the selected entry one place, keeps it selected, and writes the order.
void Step(const std::shared_ptr<OrderList>& order, int by) {
    const int32_t index = order->list.SelectedIndex();
    const int32_t target = index + by;
    if (index < 0 || target < 0 || target >= static_cast<int32_t>(order->items.Size())) return;
    auto item = order->items.GetAt(static_cast<uint32_t>(index));
    order->items.RemoveAt(static_cast<uint32_t>(index));
    order->items.InsertAt(static_cast<uint32_t>(target), item);
    order->list.SelectedIndex(target);
    Save(*order, false);
}

controls::Button IconButton(UiService::Shared& shared, const wchar_t* glyph, const std::string& labelKey,
                            const std::string& name) {
    controls::Button button;
    if (shared.probe.button) button.Style(shared.probe.button);
    controls::FontIcon icon;
    icon.FontFamily(xaml::Media::FontFamily(L"Segoe Fluent Icons,Segoe MDL2 Assets"));
    icon.FontSize(12);
    icon.Glyph(glyph);
    button.Content(icon);
    Name(button, name, i18n::Text(labelKey));
    controls::ToolTipService::SetToolTip(button, winrt::box_value(Wide(i18n::Text(labelKey))));
    return button;
}

// What the list shows for an entry: an element's button by the element's name (its own text can be a value, such as
// "55 %"), an unnamed control by its type without "Button" (AirPlayButton reads AirPlay), else what the entry says.
std::string ShownLabel(const OrderEntry& entry, const std::vector<uirender::ModEntry>& mods) {
    constexpr std::string_view kElementPrefix = "AMPIFY_";
    if (entry.key.starts_with(kElementPrefix)) {
        const std::string id = entry.key.substr(kElementPrefix.size());
        for (const uirender::ModEntry& mod : mods) {
            if (mod.id == id) return mod.NameIn(i18n::Language());
        }
    }
    if (entry.label == entry.key && entry.key.ends_with("Button") && entry.key.size() > 6) {
        return entry.key.substr(0, entry.key.size() - 6);
    }
    return entry.label;
}

void AddOrderRow(UiService::Shared& shared, SettingsNav& nav, const controls::StackPanel& rows, const OrderKind& kind,
                 std::size_t fewest) {
    const std::vector<OrderEntry> entries = kind.entries();
    if (entries.size() < fewest) return;
    auto order = std::make_shared<OrderList>();
    order->shared = &shared;
    order->kind = &kind;
    order->items = winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>();
    const std::vector<uirender::ModEntry> mods =
        kind.elementNames ? uirender::ListMods(shared.kit) : std::vector<uirender::ModEntry>{};
    for (const OrderEntry& entry : entries) {
        auto box = winrt::box_value(Wide(ShownLabel(entry, mods)));
        order->items.Append(box);
        order->keys.emplace_back(box, entry.key);
    }
    controls::ListView list;
    order->list = list;
    list.SelectionMode(controls::ListViewSelectionMode::Single);
    list.CanDragItems(true);
    list.CanReorderItems(true);
    list.AllowDrop(true);
    list.MaxHeight(kListHeight);
    list.Width(kSettingControlWidth);
    list.ItemsSource(order->items);
    animation::TransitionCollection transitions;  // the entries slide to their new places
    transitions.Append(animation::RepositionThemeTransition());
    list.ItemContainerTransitions(transitions);
    const std::string prefix = kind.name;
    Name(list, prefix, i18n::Text(kind.label));
    list.DragItemsCompleted([order](auto const&, auto const&) noexcept {
        try {
            Save(*order, false);
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });

    controls::StackPanel buttons;
    buttons.Spacing(4);
    buttons.VerticalAlignment(xaml::VerticalAlignment::Top);
    auto up = IconButton(shared, kUpGlyph, "order.up", prefix + "Up");
    auto down = IconButton(shared, kDownGlyph, "order.down", prefix + "Down");
    controls::Button reset;
    if (shared.probe.button) reset.Style(shared.probe.button);
    reset.Content(winrt::box_value(Wide(i18n::Text(kind.reset))));
    Name(reset, prefix + "Reset", i18n::Text(kind.reset));
    up.Click([order](auto const&, auto const&) noexcept {
        try {
            Step(order, -1);
        } catch (...) {
        }
    });
    down.Click([order](auto const&, auto const&) noexcept {
        try {
            Step(order, 1);
        } catch (...) {
        }
    });
    reset.Click([order](auto const&, auto const&) noexcept {
        try {
            Save(*order, true);
        } catch (...) {
        }
    });
    buttons.Children().Append(up);
    buttons.Children().Append(down);
    buttons.Children().Append(reset);

    controls::StackPanel control;
    control.Orientation(controls::Orientation::Horizontal);
    control.Spacing(8);
    control.Children().Append(list);
    control.Children().Append(buttons);
    const std::string label = i18n::Text(kind.label);
    const std::string help = i18n::Text(kind.help);
    AddSettingsRow(nav, rows, SettingsLabelledRow(shared, label, help, control), {label, help});
}

}  // namespace

void AddOrderRows(UiService::Shared& shared, SettingsNav& nav, const controls::StackPanel& rows) {
    AddOrderRow(shared, nav, rows, kSidebar, 0);
    AddOrderRow(shared, nav, rows, kSidebarAdded, 2);  // one entry has nothing to trade places with
    AddOrderRow(shared, nav, rows, kPlaybar, 0);
}

}  // namespace ampify::win::ui_service_detail

#endif  // AMPIFY_WITH_XAML
