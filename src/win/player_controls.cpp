#include "player_controls.hpp"

#if defined(AMPIFY_WITH_XAML)

#include <winrt/Windows.Foundation.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Provider.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>

#include <chrono>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace ampify::win {

namespace {

namespace dispatching = winrt::Microsoft::UI::Dispatching;
namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = xaml::Controls;
namespace peers = xaml::Automation::Peers;
namespace provider = xaml::Automation::Provider;

constexpr int kSearchLimit = 6000;  // elements walked in one window to find a button
constexpr auto kFollowStep = std::chrono::milliseconds(400);

// A button of the main window and the name of its copy in the player.
struct Mirror {
    const wchar_t* source;
    const wchar_t* copy;
};
constexpr Mirror kShuffle{L"ShuffleButton", L"AmpifyShuffleButton"};
constexpr Mirror kRepeat{L"RepeatButton", L"AmpifyRepeatButton"};

// Interface thread only. Never freed: releasing WinRT objects while the process exits can crash it.
struct Follow {
    dispatching::DispatcherQueueTimer timer{nullptr};
    winrt::weak_ref<xaml::UIElement> main;
    winrt::weak_ref<controls::Primitives::ButtonBase> shuffle;
    winrt::weak_ref<controls::Primitives::ButtonBase> repeat;
};

Follow& State() {
    static Follow* state = new Follow();
    return *state;
}

std::vector<xaml::DependencyObject> Below(const xaml::DependencyObject& root) {
    std::vector<xaml::DependencyObject> queue{root};
    for (std::size_t i = 0; i < queue.size() && queue.size() < static_cast<std::size_t>(kSearchLimit); i++) {
        int count = xaml::Media::VisualTreeHelper::GetChildrenCount(queue[i]);
        for (int child = 0; child < count; child++) queue.push_back(xaml::Media::VisualTreeHelper::GetChild(queue[i], child));
    }
    return queue;
}

xaml::FrameworkElement FindNamed(const xaml::DependencyObject& root, std::wstring_view name) {
    if (!root) return nullptr;
    for (const auto& item : Below(root)) {
        auto element = item.try_as<xaml::FrameworkElement>();
        if (element && element.Name() == name) return element;
    }
    return nullptr;
}

template <typename T>
T FindFirst(const xaml::DependencyObject& root) {
    for (const auto& item : Below(root)) {
        if (auto found = item.try_as<T>(); found && item != root) return found;
    }
    return nullptr;
}

// On when Apple checks the button or shows its dot ("ShuffleButtonDot", "RepeatButtonDot").
bool IsOn(const xaml::FrameworkElement& source) {
    if (auto toggle = source.try_as<controls::Primitives::ToggleButton>()) {
        if (auto checked = toggle.IsChecked()) return checked.Value();
    }
    auto dot = FindNamed(source, std::wstring(source.Name()) + L"Dot");
    return dot && dot.Visibility() == xaml::Visibility::Visible && dot.Opacity() > 0;
}

// The copy takes the source's glyph (repeat one has its own), name and state, shown by Apple's dot under the icon.
void Sync(const controls::Primitives::ButtonBase& copy, const xaml::FrameworkElement& source) {
    auto glyph = FindFirst<controls::FontIcon>(source);
    auto icon = FindFirst<controls::FontIcon>(copy);
    if (glyph && icon && icon.Glyph() != glyph.Glyph()) icon.Glyph(glyph.Glyph());
    winrt::hstring name = xaml::Automation::AutomationProperties::GetName(source);
    if (!name.empty() && xaml::Automation::AutomationProperties::GetName(copy) != name) {
        xaml::Automation::AutomationProperties::SetName(copy, name);
        controls::ToolTipService::SetToolTip(copy, winrt::box_value(name));
    }
    bool on = IsOn(source);
    if (auto toggle = copy.try_as<controls::Primitives::ToggleButton>()) {
        toggle.IsChecked(on);  // its style colours the checked state
    } else if (glyph && icon) {
        // Apple colours repeat's icon itself while it is on; off, the style's colours come back.
        if (on) icon.Foreground(glyph.Foreground());
        else icon.ClearValue(controls::IconElement::ForegroundProperty());
    }
    if (auto dot = FindFirst<xaml::Shapes::Ellipse>(copy)) {
        dot.Visibility(on ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
    }
}

void SyncAll() {
    Follow& state = State();
    auto main = state.main.get();
    for (const auto& [weak, mirror] : {std::pair{state.shuffle, kShuffle}, std::pair{state.repeat, kRepeat}}) {
        auto copy = weak.get();
        // The player is gone or hidden (Apple keeps its window once closed): no walk through the main window.
        auto root = copy ? copy.XamlRoot() : nullptr;
        if (!root || !root.IsHostVisible()) continue;
        if (auto source = FindNamed(main, mirror.source)) Sync(copy, source);
    }
}

// Presses the main window's button through its automation peer: Apple runs its own command and keeps one state.
void Press(const Mirror& mirror) {
    auto source = FindNamed(State().main.get(), mirror.source);
    auto peer = source ? peers::FrameworkElementAutomationPeer::CreatePeerForElement(source) : nullptr;
    if (!peer) return;
    if (auto toggle = peer.GetPattern(peers::PatternInterface::Toggle).try_as<provider::IToggleProvider>()) {
        toggle.Toggle();
    } else if (auto invoke = peer.GetPattern(peers::PatternInterface::Invoke).try_as<provider::IInvokeProvider>()) {
        invoke.Invoke();
    }
    SyncAll();
}

// The same kind of button as the source, with its style, icon and dot, sized like the player's buttons beside it.
controls::Primitives::ButtonBase MakeCopy(const xaml::FrameworkElement& model, const xaml::FrameworkElement& source,
                                          const Mirror& mirror) {
    controls::Primitives::ButtonBase copy = source.try_as<controls::Primitives::ToggleButton>()
                                                ? controls::Primitives::ButtonBase(controls::Primitives::ToggleButton())
                                                : controls::Primitives::ButtonBase(controls::Button());
    copy.Name(mirror.copy);
    if (auto control = source.try_as<controls::Control>()) copy.Style(control.Style());
    copy.Width(model.Width());
    copy.Height(model.Height());
    copy.Margin(model.Margin());
    copy.VerticalAlignment(xaml::VerticalAlignment::Center);
    controls::Grid content;
    controls::FontIcon icon;
    if (auto glyph = FindFirst<controls::FontIcon>(source)) {
        // The FontIcon's font is a path relative to the main window's page: the text that draws it has the full one.
        auto drawn = FindFirst<controls::TextBlock>(glyph);
        icon.FontFamily(drawn ? drawn.FontFamily() : glyph.FontFamily());
        icon.FontSize(drawn ? drawn.FontSize() : glyph.FontSize());
        icon.Glyph(glyph.Glyph());
    }
    content.Children().Append(icon);
    if (auto sourceDot = FindNamed(source, std::wstring(mirror.source) + L"Dot").try_as<xaml::Shapes::Ellipse>()) {
        xaml::Shapes::Ellipse dot;
        dot.Width(sourceDot.Width());
        dot.Height(sourceDot.Height());
        dot.Margin(sourceDot.Margin());
        dot.Fill(sourceDot.Fill());
        dot.HorizontalAlignment(sourceDot.HorizontalAlignment());
        dot.VerticalAlignment(sourceDot.VerticalAlignment());
        controls::Grid::SetRow(dot, controls::Grid::GetRow(sourceDot));
        content.Children().Append(dot);
    }
    copy.Content(content);
    copy.Click([mirror](auto const&, auto const&) noexcept {
        try {
            Press(mirror);
        } catch (...) {
        }
    });
    Sync(copy, source);
    return copy;
}

// The player sizes its transport for three buttons (a maximum width of 120): left so, it would cut the two added ones
// by half, shuffle on its left and repeat on its right. Up to the transport's grid, every fixed width makes room.
std::string MakeRoom(const xaml::FrameworkElement& stack, double added) {
    std::string widened;
    xaml::FrameworkElement child = stack;
    for (int level = 0; level < 6 && child; level++) {
        auto parent = xaml::Media::VisualTreeHelper::GetParent(child).try_as<xaml::FrameworkElement>();
        if (!parent) break;
        std::string name = " " + winrt::to_string(parent.Name());
        if (!std::isnan(parent.Width())) {
            parent.Width(parent.Width() + added);
            widened += name + " width";
        }
        if (std::isfinite(parent.MaxWidth())) {
            parent.MaxWidth(parent.MaxWidth() + added);  // TransportControl: 120, three buttons of 40
            widened += name + " max width";
        }
        if (auto grid = parent.try_as<controls::Grid>()) {
            auto columns = grid.ColumnDefinitions();
            auto column = static_cast<uint32_t>(controls::Grid::GetColumn(child));
            if (column < columns.Size() && columns.GetAt(column).Width().GridUnitType == xaml::GridUnitType::Pixel) {
                columns.GetAt(column).Width(xaml::GridLengthHelper::Auto());
                widened += " " + winrt::to_string(grid.Name()) + " column";
            }
            if (grid.Name() == L"TransportGrid") break;
        }
        child = parent;
    }
    return widened.empty() ? " nothing" : widened;
}

void StartFollowing(const xaml::UIElement& playerRoot) {
    Follow& state = State();
    if (state.timer) return;
    state.timer = playerRoot.DispatcherQueue().CreateTimer();
    state.timer.Interval(kFollowStep);
    state.timer.IsRepeating(true);
    state.timer.Tick([](auto const&, auto const&) noexcept {
        try {
            SyncAll();  // a mode changed elsewhere (menu, keyboard, the main window)
        } catch (...) {
        }
    });
    state.timer.Start();
}

}  // namespace

void AddPlayerControls(const xaml::UIElement& playerRoot, const xaml::UIElement& mainRoot, Journal& journal) noexcept {
    try {
        if (!playerRoot || !mainRoot) return;
        auto transport = FindNamed(playerRoot, L"TransportControl");
        auto stack = transport ? FindFirst<controls::StackPanel>(transport) : nullptr;
        auto model = stack && stack.Children().Size() > 0 ? stack.Children().GetAt(0).try_as<xaml::FrameworkElement>()
                                                         : nullptr;
        if (!model) {
            journal.Write("Apple player: its transport buttons were not found, no shuffle nor repeat added");
            return;
        }
        if (FindNamed(stack, kShuffle.copy)) return;  // this content has them already
        auto shuffleSource = FindNamed(mainRoot, kShuffle.source);
        auto repeatSource = FindNamed(mainRoot, kRepeat.source);
        if (!shuffleSource || !repeatSource) {
            journal.Write("Apple player: the main window's shuffle or repeat button was not found, none added");
            return;
        }
        Follow& state = State();
        state.main = winrt::make_weak(mainRoot);
        auto shuffle = MakeCopy(model, shuffleSource, kShuffle);
        auto repeat = MakeCopy(model, repeatSource, kRepeat);
        stack.Children().InsertAt(0, shuffle);
        stack.Children().Append(repeat);
        std::string room = MakeRoom(stack, 2 * (std::isnan(model.Width()) ? model.ActualWidth() : model.Width()));
        state.shuffle = winrt::make_weak(shuffle);
        state.repeat = winrt::make_weak(repeat);
        StartFollowing(playerRoot);
        journal.Write("Apple player: shuffle and repeat added around its transport buttons (room made:" + room + ")");
    } catch (const winrt::hresult_error& error) {
        journal.Write("Apple player: shuffle and repeat not added (" + winrt::to_string(error.message()) + ")");
    } catch (...) {
        journal.Write("Apple player: shuffle and repeat not added");
    }
}

}  // namespace ampify::win

#endif
