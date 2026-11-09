// The extensions' entries in Apple Music's menus: its main menu and the menus of items (ui.menu).

#if defined(AMPIFY_WITH_XAML)

#include <set>
#include <string_view>
#include <vector>

#include "ui_service_detail.hpp"

namespace ampify::win::ui_service_detail {

namespace {

using MenuItems = winrt::Windows::Foundation::Collections::IVector<controls::MenuFlyoutItemBase>;

constexpr wchar_t kMenuEntryTag[] = L"AMPIFY_MENU_ENTRY";
constexpr auto kMenuPoll = std::chrono::milliseconds(50);
constexpr int kMenuTries = 10;  // half a second after the click or the key
constexpr double kFlyoutMargin = 8;

bool IsMenuEntry(const controls::MenuFlyoutItemBase& item) {
    return item && winrt::unbox_value_or<winrt::hstring>(item.Tag(), L"") == kMenuEntryTag;
}

bool HasMenuEntries(const MenuItems& items) {
    for (const controls::MenuFlyoutItemBase& item : items) {
        if (IsMenuEntry(item)) return true;
    }
    return false;
}

// True when `node` is inside the element named `name`.
bool InsideNamed(xaml::DependencyObject node, std::wstring_view name) {
    for (int depth = 0; node && depth < 64; depth++) {
        if (auto element = node.try_as<xaml::FrameworkElement>(); element && element.Name() == name) return true;
        node = media::VisualTreeHelper::GetParent(node);
    }
    return false;
}

std::vector<std::string> MenuTexts(const MenuItems& items) {
    std::vector<std::string> texts;
    for (const controls::MenuFlyoutItemBase& item : items) {
        if (auto entry = item.try_as<controls::MenuFlyoutItem>()) texts.push_back(Utf8(entry.Text()));
        else if (auto submenu = item.try_as<controls::MenuFlyoutSubItem>()) texts.push_back(Utf8(submenu.Text()));
    }
    return texts;
}

// The style Apple Music gives its own items of that exact class, so Ampify's entries look like them.
xaml::Style StyleOf(const MenuItems& items, std::wstring_view className) {
    for (const controls::MenuFlyoutItemBase& item : items) {
        if (winrt::get_class_name(item) != className) continue;
        if (xaml::Style style = item.Style()) return style;
    }
    return nullptr;
}

// What a menu is about: the item's name, taken from the menu's own "Play «Name»" entry, and what Apple Music's data
// says about it (its identifiers and kind), read once when the menu opens.
struct MenuTarget {
    std::string title;
    std::optional<AmpItemInfo> item;
};

// The item the menu is about: the first data context, from the element the click came from upwards, that Apple Music
// ties to a content descriptor and that carries the menu's own name (the row of a song list, an album's card...), not
// the page's, which has a descriptor too.
std::optional<AmpItemInfo> MenuItemInfo(xaml::DependencyObject node, const std::string& title) {
    constexpr int kMaxDepth = 16;
    for (int depth = 0; node && depth < kMaxDepth; depth++) {
        auto element = node.try_as<xaml::FrameworkElement>();
        if (auto data = element ? element.DataContext() : nullptr) {
            winrt::com_ptr<::IUnknown> unknown = data.as<::IUnknown>();
            if (auto info = ReadItemInfo(rt::Ref(unknown.detach()), title)) return info;
        }
        node = media::VisualTreeHelper::GetParent(node);
    }
    return std::nullopt;
}

std::string MenuPayload(const UiControl& control, const MenuTarget& target) {
    Json::Object fields{{"id", Json(static_cast<double>(control.id))}, {"title", Json(target.title)}};
    if (target.item) {
        Json::Object item{{"kind", Json(target.item->kind)}};
        if (!target.item->id.empty()) item.emplace_back("id", Json(target.item->id));
        if (!target.item->canonicalId.empty()) item.emplace_back("canonicalId", Json(target.item->canonicalId));
        fields.emplace_back("item", Json(std::move(item)));
    }
    return Json(std::move(fields)).Dump();
}

// A context entry with kinds goes only in the menu of an item Apple's data names, of one of those kinds.
bool ShowsFor(const UiControl& control, const MenuTarget& target) {
    return ShowsForItem(control, target.item ? std::string_view(target.item->kind) : std::string_view());
}

// The entries of the open menu: those for its item, and no submenu left without entries.
std::vector<UiControl> EntriesFor(const std::vector<UiControl>& entries, const MenuTarget& target) {
    std::set<int> filled;  // the submenus that keep at least one entry
    for (const UiControl& control : entries) {
        if (control.parent != 0 && ShowsFor(control, target)) filled.insert(control.parent);
    }
    std::vector<UiControl> shown;
    for (const UiControl& control : entries) {
        bool submenu = !control.clickable && control.parent == 0;
        if (ShowsFor(control, target) && (!submenu || filled.count(control.id) > 0)) shown.push_back(control);
    }
    return shown;
}

controls::MenuFlyoutItem MakeMenuEntry(const UiEventSink& sink, const UiControl& control, const MenuTarget& target,
                                       const xaml::Style& style) {
    controls::MenuFlyoutItem item;
    if (style) item.Style(style);
    item.Text(Wide(control.label));
    item.Tag(winrt::box_value(winrt::hstring(kMenuEntryTag)));
    item.IsEnabled(!control.disabled);
    if (uirender::IsGlyphIcon(control.icon)) {
        controls::FontIcon icon;
        icon.Glyph(Wide(control.icon));
        item.Icon(icon);
    }
    Name(item, uirender::ElementName(control.elementId, "control-" + std::to_string(control.id)), control.label);
    std::string payload = MenuPayload(control, target);
    std::string elementId = control.elementId;
    item.Click([sink, elementId, payload](IInspectable const&, xaml::RoutedEventArgs const&) noexcept {
        try {
            if (sink) sink(UiEvent{elementId, 0, false, payload, dispatch::kMenuClick});
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });
    return item;
}

// A submenu: its own entries are the controls whose parent is it. Apple Music's submenus give it their style.
controls::MenuFlyoutSubItem MakeMenuSubmenu(const UiEventSink& sink, const UiControl& control,
                                            const std::vector<UiControl>& entries, const MenuTarget& target,
                                            const xaml::Style& itemStyle, const xaml::Style& submenuStyle) {
    controls::MenuFlyoutSubItem submenu;
    if (submenuStyle) submenu.Style(submenuStyle);
    submenu.Text(Wide(control.label));
    submenu.Tag(winrt::box_value(winrt::hstring(kMenuEntryTag)));
    submenu.IsEnabled(!control.disabled);
    if (uirender::IsGlyphIcon(control.icon)) {
        controls::FontIcon icon;
        icon.Glyph(Wide(control.icon));
        submenu.Icon(icon);
    }
    Name(submenu, uirender::ElementName(control.elementId, "control-" + std::to_string(control.id)), control.label);
    for (const UiControl& child : entries) {
        if (child.parent == control.id && ShowsFor(child, target)) {
            submenu.Items().Append(MakeMenuEntry(sink, child, target, itemStyle));
        }
    }
    return submenu;
}

// A separator, then the entries, at the end of an open menu. Apple Music keeps a menu's items from one opening to the
// next and fills them again from its own list, so Ampify's leave once the menu has closed.
void AppendMenuEntries(const UiEventSink& sink, const primitives::Popup& popup, const MenuItems& items,
                       const std::vector<UiControl>& allEntries, const MenuTarget& target) {
    const std::vector<UiControl> entries = EntriesFor(allEntries, target);
    if (entries.empty()) return;
    xaml::Style itemStyle = StyleOf(items, L"Microsoft.UI.Xaml.Controls.MenuFlyoutItem");
    xaml::Style submenuStyle = StyleOf(items, L"Microsoft.UI.Xaml.Controls.MenuFlyoutSubItem");
    controls::MenuFlyoutSeparator separator;
    if (xaml::Style lineStyle = StyleOf(items, L"Microsoft.UI.Xaml.Controls.MenuFlyoutSeparator")) separator.Style(lineStyle);
    separator.Tag(winrt::box_value(winrt::hstring(kMenuEntryTag)));
    items.Append(separator);
    for (const UiControl& control : entries) {
        if (control.parent != 0) continue;  // it is shown inside its submenu
        if (control.clickable) items.Append(MakeMenuEntry(sink, control, target, itemStyle));
        else items.Append(MakeMenuSubmenu(sink, control, entries, target, itemStyle, submenuStyle));
    }

    auto token = std::make_shared<winrt::event_token>();
    *token = popup.Closed([items, token](IInspectable const& sender, IInspectable const&) noexcept {
        try {
            if (auto closed = sender.try_as<primitives::Popup>()) closed.Closed(*token);
            // Once the close has run its course. A menu opened in the meantime is looked for after this, in order.
            dispatching::DispatcherQueue::GetForCurrentThread().TryEnqueue(
                dispatching::DispatcherQueuePriority::Low, [items]() noexcept {
                    try {
                        for (std::uint32_t index = items.Size(); index-- > 0;) {
                            if (IsMenuEntry(items.GetAt(index))) items.RemoveAt(index);
                        }
                    } catch (...) {
                        // A handler runs inside Apple Music's XAML frame: nothing may escape it.
                    }
                });
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });
}

// The menu the last click or key opened gets the extensions' entries: Apple Music's main menu (opened from its
// header, Grid#PaneHeaderGrid) the "app" ones, the menu of an item (a song, an album, a playlist: the menus with a
// "Play «Name»" entry) the "context" ones, with that item's name. True once that menu has them, or when nothing is to
// be added.
bool AddMenuEntries(UiService::Shared& shared, WindowContext& context) {
    xaml::UIElement input = context.menuInput.get();
    xaml::XamlRoot root = input ? input.XamlRoot() : nullptr;
    if (!root) return true;
    bool app = InsideNamed(context.menuSource.get(), L"PaneHeaderGrid");
    const std::vector<UiControl>& entries = app ? context.appEntries : context.contextEntries;
    if (entries.empty()) return true;
    for (const primitives::Popup& popup : media::VisualTreeHelper::GetOpenPopupsForXamlRoot(root)) {
        auto presenter = popup.Child().try_as<controls::MenuFlyoutPresenter>();
        if (!presenter) continue;
        MenuItems items = presenter.ItemsSource().try_as<MenuItems>();
        if (!items) {
            shared.Once("menu-items", "Interface: a menu whose items are not MenuFlyoutItemBase, left as it is");
            continue;
        }
        if (HasMenuEntries(items)) return true;  // this opening has them already
        std::string title = app ? std::string() : uirender::QuotedMenuName(MenuTexts(items));
        if (!app && title.empty()) continue;  // a menu of options, or one not filled in yet
        MenuTarget target{title, std::nullopt};
        if (!app) {
            target.item = MenuItemInfo(context.menuSource.get(), title);
            shared.Once(target.item ? "menu-item-known" : "menu-item-unknown",
                        target.item ? "Interface: an item's menu knows its content descriptor (identifier and kind)"
                                    : "Interface: an item's menu has no content descriptor behind it, entries get the name only");
        }
        AppendMenuEntries(shared.sink, popup, items, entries, target);
        shared.Once(app ? "menu-app-open" : "menu-context-open",
                    app ? "Interface: entries added to the main menu" : "Interface: entries added to an item's menu");
        return true;
    }
    return false;
}

// Apple Music opens the volume flyout from the button's left edge: with the toggles against the window's right edge, it
// ran past the window. A flyout that would moves left, a margin short of that edge. True once an open flyout has its
// size, so a click that opens none keeps looking for half a second.
bool KeepFlyoutInside(UiService::Shared& shared, const WindowContext& context) {
    xaml::UIElement input = context.menuInput.get();
    xaml::XamlRoot root = input ? input.XamlRoot() : nullptr;
    if (!root) return true;
    double room = root.Size().Width - kFlyoutMargin;
    for (const primitives::Popup& popup : media::VisualTreeHelper::GetOpenPopupsForXamlRoot(root)) {
        auto presenter = popup.Child().try_as<controls::FlyoutPresenter>();
        if (!presenter) continue;
        double width = presenter.ActualWidth();
        if (width <= 0) return false;
        if (popup.HorizontalOffset() + width > room) {
            popup.HorizontalOffset(std::max(0.0, room - width));
            shared.Once("flyout-inside", "Interface: a flyout that ran past the window's right edge moved left");
        }
        return true;
    }
    return false;
}

void LookForMenu(const WeakShared& weakShared, const WeakContext& weakContext) noexcept {
    try {
        SharedPtr shared = weakShared.lock();
        std::shared_ptr<WindowContext> context = weakContext.lock();
        if (!shared || !context) return;
        bool menuDone = AddMenuEntries(*shared, *context);
        bool flyoutDone = KeepFlyoutInside(*shared, *context);
        if ((menuDone && flyoutDone) || ++context->menuTries >= kMenuTries) {
            if (context->menuTimer) context->menuTimer.Stop();
            return;
        }
        if (!context->menuTimer) {
            context->menuTimer = context->dispatcher.CreateTimer();
            context->menuTimer.Interval(kMenuPoll);
            context->menuTimer.Tick([weakShared, weakContext](auto const&, auto const&) noexcept {
                LookForMenu(weakShared, weakContext);
            });
        }
        if (!context->menuTimer.IsRunning()) context->menuTimer.Start();
    } catch (...) {
        // Nothing may escape into Apple Music's XAML frame.
    }
}

}  // namespace

// Follows the clicks and keys of the window's content, handled ones too (the button that opens a menu marks its click
// handled), and looks for the menu or flyout they open once the input has been dealt with.
void FollowMenus(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context) {
    xaml::Window window = context->window.get();
    xaml::UIElement content = window ? window.Content() : nullptr;
    if (!content || context->menuInput.get() == content) return;
    WeakShared weakShared = shared.weak_from_this();
    WeakContext weakContext = context;
    auto look = [weakShared, weakContext](IInspectable const&, auto const& args) noexcept {
        try {
            std::shared_ptr<WindowContext> current = weakContext.lock();
            if (!current) return;
            auto source = args.OriginalSource().template try_as<xaml::DependencyObject>();
            current->menuSource = source ? winrt::make_weak(source) : winrt::weak_ref<xaml::DependencyObject>();
            current->menuTries = 0;
            current->dispatcher.TryEnqueue(dispatching::DispatcherQueuePriority::Low, [weakShared, weakContext]() noexcept {
                LookForMenu(weakShared, weakContext);
            });
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    };
    content.AddHandler(xaml::UIElement::PointerReleasedEvent(), winrt::box_value(xaml::Input::PointerEventHandler(look)),
                       true);
    // KeyDown rather than KeyUp: a menu opened by the menu key takes the focus, and the key's release goes to it.
    content.AddHandler(xaml::UIElement::KeyDownEvent(), winrt::box_value(xaml::Input::KeyEventHandler(look)), true);
    context->menuInput = winrt::make_weak(content);
}

}  // namespace ampify::win::ui_service_detail

#endif  // AMPIFY_WITH_XAML
