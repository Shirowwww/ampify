#include "player_controls.hpp"

#if defined(AMPIFY_WITH_XAML)

#include "../core/ui_watchdog.hpp"
#include "amp_item.hpp"
#include "amp_player.hpp"
#include "winrt_abi.hpp"
#include "win_common.hpp"
#ifdef GetCurrentTime
#undef GetCurrentTime  // windows.h macro that breaks Storyboard::GetCurrentTime in the projection
#endif
#include "xaml_chrome.hpp"

#include "../core/amp_abi.hpp"
#include "../core/player_modes.hpp"
#include "../core/player_queue.hpp"
#include "../core/player_state.hpp"

#include <inspectable.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Provider.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace ampify::win {

namespace {

namespace dispatching = winrt::Microsoft::UI::Dispatching;
namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = xaml::Controls;
namespace media = xaml::Media;
namespace peers = xaml::Automation::Peers;
namespace provider = xaml::Automation::Provider;

constexpr int kSearchLimit = 6000;  // elements walked in one window to find a button
constexpr auto kLyricsSearchEvery = std::chrono::seconds(2);  // while Apple's lyrics view is missing
constexpr auto kFollowStep = std::chrono::milliseconds(400);
constexpr int kRemeasureTicks = 10;  // the transport row's sides, measured again every 4 seconds
constexpr double kRowGap = 8;        // between a side of the transport row and the transport: "···" touched shuffle

// A button of the main window and the name of its copy in the player.
struct Mirror {
    const wchar_t* source;
    const wchar_t* copy;
};
constexpr Mirror kShuffle{L"ShuffleButton", L"AmpifyShuffleButton"};
constexpr Mirror kRepeat{L"RepeatButton", L"AmpifyRepeatButton"};

// A width or maximum width of the transport's parents that Ampify widened: Apple's own value, and what Ampify wrote.
// Apple sets its value again when the window is resized; the next check sees it and widens it again.
struct Widened {
    winrt::weak_ref<xaml::FrameworkElement> element;
    bool maxWidth = false;
    double apple = 0;
    double written = 0;
};

// Interface thread only. Never freed: releasing WinRT objects while the process exits can crash it.
struct Follow {
    dispatching::DispatcherQueueTimer timer{nullptr};
    winrt::weak_ref<xaml::UIElement> main;
    // The main window's two buttons, found once: walking its thousands of elements twice a step cost a few ms each.
    winrt::weak_ref<xaml::FrameworkElement> shuffleSource;
    winrt::weak_ref<xaml::FrameworkElement> repeatSource;
    // Held strongly: a weak reference to an element Ampify created never resolves inside Apple Music.
    controls::Button shuffle{nullptr};
    controls::Button repeat{nullptr};
    winrt::weak_ref<controls::Panel> stack;
    winrt::weak_ref<xaml::FrameworkElement> model;  // the player's button the copies take their size from
    winrt::weak_ref<xaml::FrameworkElement> transport;
    winrt::weak_ref<controls::Grid> row;  // TransportGrid: the volume, the transport and the lyrics button
    bool rowFitted = false;
    xaml::GridLength rowMiddle{};
    xaml::Thickness rowMargin{};
    double rowSeen = -1;  // the row's width when its sides were last measured
    int rowTicks = 0;
    std::vector<Widened> widened;
    double added = 0;  // the width of the two buttons
};

std::atomic<bool> g_shown{true};  // apple.playerControls, written by the settings worker

Follow& State() {
    static Follow* state = new Follow();
    return *state;
}

std::vector<xaml::DependencyObject> Below(const xaml::DependencyObject& root) {
    ampify::ActivityScope activity("player buttons tree search");
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

// The fill of a dot that is a shape with a plain colour, else nothing.
media::SolidColorBrush DotFill(const xaml::FrameworkElement& dot) {
    auto shape = dot ? dot.try_as<xaml::Shapes::Shape>() : nullptr;
    return shape && shape.Fill() ? shape.Fill().try_as<media::SolidColorBrush>() : nullptr;
}

bool SameBrushColour(const media::Brush& first, const media::Brush& second) {
    auto a = first.try_as<media::SolidColorBrush>();
    auto b = second.try_as<media::SolidColorBrush>();
    return a && b && a.Color() == b.Color();
}

// The copy's own icon. An AppBarButton's template has a FontIcon of its own (SubItemChevron, collapsed) above the
// icon in the tree: found first, it took the colour and the glyph, and the copies stayed white.
controls::FontIcon CopyIcon(const controls::Button& copy) {
    if (auto bar = copy.try_as<controls::AppBarButton>()) return bar.Icon().try_as<controls::FontIcon>();
    return FindFirst<controls::FontIcon>(copy);
}

std::string PaintText(const media::Brush& brush) {
    auto solid = brush.try_as<media::SolidColorBrush>();
    if (!solid) return brush ? "not a plain colour" : "none";
    return ColourText(solid.Color());
}

Journal* g_journal = nullptr;  // AddPlayerControls's, for the reports of Sync

// Once per change of a button's state: what Apple shows on its own button, and what the copy takes.
void ReportColour(const xaml::FrameworkElement& source, bool on, const media::Brush& icon, const media::Brush& taken) {
    static std::map<std::wstring, std::string> seen;
    std::string line = std::string(on ? "on" : "off") + ", Apple's icon " + PaintText(icon) + ", the copy " +
                       (on ? PaintText(taken) : std::string("the player's"));
    std::string& last = seen[std::wstring(source.Name())];
    if (!g_journal || last == line) return;
    last = line;
    g_journal->Write("Apple player: " + winrt::to_string(source.Name()) + " " + line);
}

// The copy takes the source's glyph (repeat one has its own), name and state, shown by Apple's dot under the icon.
void Sync(const controls::Button& copy, const xaml::FrameworkElement& source) {
    auto glyph = FindFirst<controls::FontIcon>(source);
    auto icon = CopyIcon(copy);
    if (glyph && icon && icon.Glyph() != glyph.Glyph()) icon.Glyph(glyph.Glyph());
    winrt::hstring name = xaml::Automation::AutomationProperties::GetName(source);
    if (!name.empty() && xaml::Automation::AutomationProperties::GetName(copy) != name) {
        xaml::Automation::AutomationProperties::SetName(copy, name);
        controls::ToolTipService::SetToolTip(copy, winrt::box_value(name));
    }
    bool on = IsOn(source);
    if (glyph && icon) {
        // On, the icon takes the colour Apple gives its own (the accent, in 1.1792), or its dot's when Apple leaves the
        // icon in the button's colour and the copy has no dot; off, the player's style colours it as its neighbours.
        // Taken from the dot first, the copies showed no accent when on.
        media::Brush colour = glyph.Foreground();
        auto control = source.try_as<controls::Control>();
        if (!FindFirst<xaml::Shapes::Ellipse>(copy) && control && SameBrushColour(colour, control.Foreground())) {
            // Not a clear dot: while Apple switches the mode, it read #00FFFFFF and the icon vanished.
            auto dot = FindNamed(source, std::wstring(source.Name()) + L"Dot").try_as<xaml::Shapes::Ellipse>();
            auto fill = dot ? dot.Fill().try_as<media::SolidColorBrush>() : nullptr;
            if (fill && fill.Color().A > 0) colour = fill;
        }
        if (on) icon.Foreground(colour);
        else icon.ClearValue(controls::IconElement::ForegroundProperty());
        ReportColour(source, on, glyph.Foreground(), colour);
    }
    if (auto dot = FindFirst<xaml::Shapes::Ellipse>(copy)) {
        dot.Visibility(on ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
    }
}

// The main window's button behind a copy, looked for again only once Apple has replaced it.
xaml::FrameworkElement SourceOf(const Mirror& mirror) {
    Follow& state = State();
    auto& weak = mirror.source == kShuffle.source ? state.shuffleSource : state.repeatSource;  // a copy of kShuffle too
    auto source = weak.get();
    if (source && source.XamlRoot()) return source;
    source = FindNamed(state.main.get(), mirror.source);
    weak = source ? winrt::make_weak(source) : winrt::weak_ref<xaml::FrameworkElement>{};
    return source;
}

void SyncAll() {
    Follow& state = State();
    for (const auto& [copy, mirror] : {std::pair{state.shuffle, &kShuffle}, std::pair{state.repeat, &kRepeat}}) {
        // The player is gone or hidden (Apple keeps its window once closed): no walk through the main window.
        auto root = copy ? copy.XamlRoot() : nullptr;
        if (!root || !root.IsHostVisible()) continue;
        if (auto source = SourceOf(*mirror)) Sync(copy, source);
    }
}

// Presses the main window's button through its automation peer: Apple runs its own command and keeps one state.
void Press(const Mirror& mirror) {
    auto source = SourceOf(mirror);
    auto peer = source ? peers::FrameworkElementAutomationPeer::CreatePeerForElement(source) : nullptr;
    if (!peer) return;
    if (auto toggle = peer.GetPattern(peers::PatternInterface::Toggle).try_as<provider::IToggleProvider>()) {
        toggle.Toggle();
    } else if (auto invoke = peer.GetPattern(peers::PatternInterface::Invoke).try_as<provider::IInvokeProvider>()) {
        invoke.Invoke();
    }
    SyncAll();
}

double ModelWidth(const xaml::FrameworkElement& model) {
    return std::isnan(model.Width()) ? model.ActualWidth() : model.Width();
}

// A button with the style of the player's own buttons beside it, so its pointer halo has their size, and the source's
// icon and dot. The main window's style draws a larger halo, which the player's row cut.
controls::Button MakeCopy(const xaml::FrameworkElement& model, const xaml::FrameworkElement& source,
                          const Mirror& mirror) {
    // The player's buttons are AppBarButtons in recent Apple Music versions, Buttons before: the copy has its model's
    // type, as a style refuses any other ("Cannot apply a Style with TargetType ...").
    auto bar = model.try_as<controls::AppBarButton>();
    controls::Button copy = bar ? controls::AppBarButton() : controls::Button();
    copy.Name(mirror.copy);
    if (auto control = model.try_as<controls::Button>(); control && control.Style()) copy.Style(control.Style());
    if (auto control = model.try_as<controls::Control>()) copy.Padding(control.Padding());
    // A neighbour sized by its content has no Width: its laid-out size keeps the copies as large as it. Not laid out
    // yet, it has none: SizeCopies gives it to them later, as a size of 0 hid them.
    double width = ModelWidth(model);
    double height = std::isnan(model.Height()) ? model.ActualHeight() : model.Height();
    if (width > 0) copy.Width(width);
    if (height > 0) copy.Height(height);
    copy.Margin(model.Margin());
    copy.VerticalAlignment(xaml::VerticalAlignment::Center);
    controls::Grid content;
    controls::FontIcon icon;
    if (auto glyph = FindFirst<controls::FontIcon>(source)) {
        // The FontIcon's font is a path relative to the main window's page: the text that draws it has the full one.
        auto drawn = FindFirst<controls::TextBlock>(glyph);
        icon.FontFamily(drawn ? drawn.FontFamily() : glyph.FontFamily());
        // The player draws its icons larger than the main window: the size of the neighbour's icon, when it has one.
        auto neighbour = FindFirst<controls::FontIcon>(model);
        auto neighbourText = neighbour ? FindFirst<controls::TextBlock>(neighbour) : nullptr;
        double size = neighbourText ? neighbourText.FontSize() : neighbour ? neighbour.FontSize() : 0;
        icon.FontSize(size > 0 ? size : drawn ? drawn.FontSize() : glyph.FontSize());
        icon.Glyph(glyph.Glyph());
    }
    if (bar) {
        // Its template draws the icon alone, without a place for Apple's dot: the icon's colour shows the mode.
        copy.as<controls::AppBarButton>().Icon(icon);
    } else {
        content.Children().Append(icon);
    }
    auto sourceDot = FindNamed(source, std::wstring(mirror.source) + L"Dot").try_as<xaml::Shapes::Ellipse>();
    if (!bar && sourceDot) {
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
    if (!bar) copy.Content(content);
    copy.Click([mirror](auto const&, auto const&) noexcept {
        try {
            Press(mirror);
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });
    Sync(copy, source);
    return copy;
}

// The player sizes its transport for three buttons (a maximum width of 120): left so, it would cut the two added ones
// by half, shuffle on its left and repeat on its right. Up to the transport's grid, every fixed width makes room.
std::string MakeRoom(const xaml::FrameworkElement& stack, double added) {
    Follow& state = State();
    state.widened.clear();
    state.added = added;
    std::string widened;
    xaml::FrameworkElement child = stack;
    for (int level = 0; level < 6 && child; level++) {
        auto parent = xaml::Media::VisualTreeHelper::GetParent(child).try_as<xaml::FrameworkElement>();
        if (!parent) break;
        std::string name = " " + winrt::to_string(parent.Name());
        if (!std::isnan(parent.Width())) {
            parent.Width(parent.Width() + added);
            state.widened.push_back({winrt::make_weak(parent), false, parent.Width() - added, parent.Width()});
            widened += name + " width";
        }
        if (std::isfinite(parent.MaxWidth())) {
            parent.MaxWidth(parent.MaxWidth() + added);  // TransportControl: 120, three buttons of 40
            state.widened.push_back({winrt::make_weak(parent), true, parent.MaxWidth() - added, parent.MaxWidth()});
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

// Apple splits the transport's row in three equal columns: at most window sizes the middle one held three buttons, not
// five. The middle column takes the transport's width, and the row reaches past its own column, evenly, only when the
// volume or the lyrics button would no longer fit beside it.
void FitTransportRow() {
    Follow& state = State();
    auto row = state.row.get();
    auto transport = state.transport.get();
    if (!row || !transport || state.added <= 0) return;
    auto columns = row.ColumnDefinitions();
    if (columns.Size() != 3) return;
    if (!state.rowFitted) {
        state.rowFitted = true;
        state.rowMiddle = columns.GetAt(1).Width();
        state.rowMargin = row.Margin();
        columns.GetAt(1).Width(xaml::GridLengthHelper::Auto());
    }
    // A side's DesiredSize is cut to its column, which once hid the "···" button: measured unbounded, it says what it
    // needs. Only when the row was resized, or every few seconds (Apple shows or hides "···"), as it lays the row out.
    double width = row.ActualWidth();
    if (width == state.rowSeen && ++state.rowTicks < kRemeasureTicks) return;
    state.rowSeen = width;
    state.rowTicks = 0;
    double side = 0;
    for (const auto& item : row.Children()) {
        auto element = item.try_as<xaml::FrameworkElement>();
        if (!element || controls::Grid::GetColumn(element) == 1) continue;
        element.Measure({std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()});
        side = std::max(side, static_cast<double>(element.DesiredSize().Width));
        element.InvalidateMeasure();
    }
    xaml::Thickness margin = row.Margin();
    const xaml::Thickness& apple = state.rowMargin;
    double room = row.ActualWidth() + margin.Left + margin.Right - apple.Left - apple.Right;
    double reach = std::max(0.0, transport.DesiredSize().Width + 2 * (side + kRowGap) - room) / 2;
    if (std::abs(margin.Left - (apple.Left - reach)) < 0.5 && std::abs(margin.Right - (apple.Right - reach)) < 0.5) return;
    row.Margin({apple.Left - reach, margin.Top, apple.Right - reach, margin.Bottom});
}

// Apple's adaptive layout writes its widths again when the window is resized, which cut the two buttons or hid them:
// a value that is no longer Ampify's is Apple's new one, widened in turn.
void KeepRoom() {
    for (Widened& entry : State().widened) {
        auto element = entry.element.get();
        if (!element) continue;
        double now = entry.maxWidth ? element.MaxWidth() : element.Width();
        if (std::isnan(now) || !std::isfinite(now) || now == entry.written) continue;
        entry.apple = now;
        entry.written = now + State().added;
        if (entry.maxWidth) element.MaxWidth(entry.written);
        else element.Width(entry.written);
    }
}

// Opened before its first layout, the player has not sized its buttons yet: the copies take their size, and the
// transport its room, once it has.
void SizeCopies() {
    Follow& state = State();
    if (state.added > 0) return;
    auto model = state.model.get();
    auto stack = state.stack.get();
    double width = model ? ModelWidth(model) : 0;
    if (!stack || width <= 0) return;
    double height = std::isnan(model.Height()) ? model.ActualHeight() : model.Height();
    for (const auto& copy : {state.shuffle, state.repeat}) {
        if (!copy) continue;
        copy.Width(width);
        if (height > 0) copy.Height(height);
    }
    MakeRoom(stack, 2 * width);
}

// apple.playerControls turned off while the player is open: the two buttons go, and Apple's widths come back.
void RemoveCopies() {
    Follow& state = State();
    auto stack = state.stack.get();
    for (const auto& copy : {state.shuffle, state.repeat}) {
        uint32_t index = 0;
        if (copy && stack && stack.Children().IndexOf(copy, index)) stack.Children().RemoveAt(index);
    }
    for (const Widened& entry : state.widened) {
        auto element = entry.element.get();
        if (!element) continue;
        if (entry.maxWidth) element.MaxWidth(entry.apple);
        else element.Width(entry.apple);
    }
    state.widened.clear();
    if (auto row = state.row.get(); row && state.rowFitted) {
        row.ColumnDefinitions().GetAt(1).Width(state.rowMiddle);
        xaml::Thickness margin = row.Margin();
        row.Margin({state.rowMargin.Left, margin.Top, state.rowMargin.Right, margin.Bottom});
    }
    state.rowFitted = false;
    state.shuffle = nullptr;
    state.repeat = nullptr;
}

void StartFollowing(const xaml::UIElement& playerRoot) {
    Follow& state = State();
    if (state.timer) return;
    state.timer = playerRoot.DispatcherQueue().CreateTimer();
    state.timer.Interval(kFollowStep);
    state.timer.IsRepeating(true);
    state.timer.Tick([](auto const&, auto const&) noexcept {
        try {
            if (!g_shown) return RemoveCopies();
            SizeCopies();
            KeepRoom();
            FitTransportRow();
            SyncAll();  // a mode changed elsewhere (menu, keyboard, the main window)
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });
    state.timer.Start();
}

}  // namespace

void AddPlayerControls(const xaml::UIElement& playerRoot, const xaml::UIElement& mainRoot, Journal& journal) noexcept {
    try {
        g_journal = &journal;
        if (!playerRoot || !mainRoot || !g_shown) return;
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
        state.shuffleSource = winrt::make_weak(shuffleSource);
        state.repeatSource = winrt::make_weak(repeatSource);
        auto shuffle = MakeCopy(model, shuffleSource, kShuffle);
        auto repeat = MakeCopy(model, repeatSource, kRepeat);
        stack.Children().InsertAt(0, shuffle);
        stack.Children().Append(repeat);
        state.shuffle = shuffle;
        state.repeat = repeat;
        state.stack = winrt::make_weak(stack.try_as<controls::Panel>());
        state.model = winrt::make_weak(model);
        state.transport = winrt::make_weak(transport);
        auto row = xaml::Media::VisualTreeHelper::GetParent(transport).try_as<controls::Grid>();
        state.row = row && row.Name() == L"TransportGrid" ? winrt::make_weak(row) : winrt::weak_ref<controls::Grid>{};
        state.rowFitted = false;
        state.rowSeen = -1;
        state.added = 0;
        double width = ModelWidth(model);
        std::string room = width > 0 ? MakeRoom(stack, 2 * width) : " once the player has laid out its buttons";
        StartFollowing(playerRoot);
        journal.Write("Apple player: shuffle and repeat added around its transport buttons (room made:" + room + ")");
    } catch (const winrt::hresult_error& error) {
        journal.Write("Apple player: shuffle and repeat not added (" + winrt::to_string(error.message()) + ")");
    } catch (...) {
        journal.Write("Apple player: shuffle and repeat not added");
    }
}

void SetPlayerControlsShown(bool shown) noexcept {
    g_shown = shown;
}

namespace {

// The icon Apple's repeat button shows in "repeat one" (its other states share one icon, the dot tells on from off).
// Read from the button itself: the log says "the repeat button shows icon U+XXXX" whenever the icon changes.
constexpr unsigned kRepeatOneGlyph = 0x10029F;  // Apple Music 1.1792; the others share U+10029E

std::string BrushText(const media::Brush& brush) {
    auto solid = brush ? brush.try_as<media::SolidColorBrush>() : nullptr;
    if (!solid) return "none";
    char opacity[16];
    std::snprintf(opacity, sizeof(opacity), "*%.2f", solid.Opacity());
    return ColourText(solid.Color()) + opacity;
}

// How the repeat button looks, to tell its states apart: its dot (shown or not, how opaque, which colour) and its icon.
std::string DescribeRepeat(const xaml::FrameworkElement& button, const controls::FontIcon& icon) {
    std::string text = "name=" + winrt::to_string(xaml::Automation::AutomationProperties::GetName(button));
    if (auto dot = FindNamed(button, std::wstring(button.Name()) + L"Dot")) {
        text += " dot=" + std::to_string(static_cast<int>(dot.Visibility())) + "/" + std::to_string(dot.Opacity());
        if (auto shape = dot.try_as<xaml::Shapes::Shape>()) text += "/" + BrushText(shape.Fill());
    } else {
        text += " dot=none";
    }
    if (icon) text += " icon=" + BrushText(icon.Foreground());
    return text;
}

// Apple keeps the repeat button's dot "visible" in every state and only makes its fill transparent while repeat is
// off (seen on 1.1792: #00FFFFFF off, the accent colour on). A button without such a dot falls back to IsOn.
bool RepeatOn(const xaml::FrameworkElement& button) {
    auto dot = FindNamed(button, std::wstring(button.Name()) + L"Dot");
    if (!DotFill(dot)) return IsOn(button);
    return DotLit(dot);
}


std::mutex g_volumeProblemLock;
std::string g_volumeProblem;

void NoteVolumeProblem(std::string text) {
    std::lock_guard<std::mutex> guard(g_volumeProblemLock);
    g_volumeProblem = std::move(text);
}

// A button of the main window, found by name from its content: the player need not have been opened.
xaml::FrameworkElement MainWindowButton(std::wstring_view name) {
    xaml::Window window = AppWindowOf(WindowRole::Main);
    auto content = window ? window.Content() : nullptr;
    return content ? FindNamed(content, name) : nullptr;
}

}  // namespace

bool DotLit(const xaml::FrameworkElement& dot) noexcept {
    try {
        if (!dot || dot.Visibility() != xaml::Visibility::Visible || dot.Opacity() <= 0) return false;
        media::SolidColorBrush fill = DotFill(dot);
        return !fill || (fill.Color().A > 0 && fill.Opacity() > 0);
    } catch (...) {
        return false;  // a dot being torn down: read again at the next pass
    }
}

MainPlayerModes ReadMainPlayerModes() noexcept {
    MainPlayerModes modes;
    try {
        if (auto shuffle = MainWindowButton(kShuffle.source)) modes.shuffle = IsOn(shuffle) ? 1 : 0;
        if (auto repeat = MainWindowButton(kRepeat.source)) {
            auto icon = FindFirst<controls::FontIcon>(repeat);
            if (icon && !icon.Glyph().empty()) {
                // Apple's icons are in the private-use planes: two UTF-16 units make one code point.
                winrt::hstring glyph = icon.Glyph();
                bool pair = glyph.size() >= 2 && glyph[0] >= 0xD800 && glyph[0] < 0xDC00 && glyph[1] >= 0xDC00 &&
                            glyph[1] < 0xE000;
                modes.repeatGlyph = pair ? 0x10000u + ((static_cast<unsigned>(glyph[0]) - 0xD800u) << 10) +
                                               (static_cast<unsigned>(glyph[1]) - 0xDC00u)
                                         : static_cast<unsigned>(glyph[0]);
            }
            bool one = modes.repeatGlyph != 0 && modes.repeatGlyph == kRepeatOneGlyph;
            modes.repeat = !RepeatOn(repeat) ? 0 : (one ? 2 : 1);
            modes.detail = DescribeRepeat(repeat, icon);
        }
    } catch (...) {
        // Nothing may escape into Apple Music's XAML frame.
    }
    return modes;
}

namespace {

struct LyricsViewSeen {
    winrt::weak_ref<xaml::FrameworkElement> element;
    std::chrono::steady_clock::time_point nextSearch{};
};

// The main window's lyrics view, or null. Walking the window's thousands of elements costs a few ms: the view is kept,
// and looked for again at most every kLyricsSearchEvery while it is missing (closed, or never opened).
xaml::FrameworkElement MainLyricsView(LyricsViewSeen& seen) {
    if (auto element = seen.element.get(); element && element.IsLoaded()) return element;
    auto now = std::chrono::steady_clock::now();
    if (now < seen.nextSearch) return nullptr;
    seen.nextSearch = now + kLyricsSearchEvery;
    xaml::Window window = AppWindowOf(WindowRole::Main);
    auto content = window ? window.Content() : nullptr;
    if (!content) return nullptr;
    const winrt::hstring wanted = winrt::to_hstring(amp::kClassKaraokeLyricsView);
    for (const auto& item : Below(content)) {
        if (winrt::get_class_name(item) != wanted) continue;
        if (auto element = item.try_as<xaml::FrameworkElement>()) {
            seen.element = winrt::make_weak(element);
            return element;
        }
    }
    return nullptr;
}

}  // namespace

long long ReadLyricsLineStart() noexcept {
    try {
        static auto* seen = new LyricsViewSeen();
        xaml::FrameworkElement view = MainLyricsView(*seen);
        if (!view) return -1;
        rt::Ref unknown(winrt::detach_abi(view.as<winrt::Windows::Foundation::IUnknown>()));
        rt::Ref viewInterface;
        rt::Ref inspectable;
        rt::Ref model;
        rt::Ref lineObject;
        rt::Ref line;
        if (!rt::Query(unknown, rt::GuidOf(amp::kIidKaraokeLyricsView), viewInterface) ||
            FAILED(rt::CallSlot(viewInterface, amp::kKaraokeLyricsViewViewModel, inspectable.Put())) ||
            !inspectable || !rt::Query(inspectable, rt::GuidOf(amp::kIidKaraokeLyricsViewModel), model) ||
            FAILED(rt::CallSlot(model, amp::kKaraokeLyricsViewModelCurrentLine, lineObject.Put())) || !lineObject ||
            !rt::Query(lineObject, rt::GuidOf(amp::kIidKaraokeLyricsLine), line)) {
            return -1;
        }
        int64_t ticks = 0;  // Windows.Foundation.TimeSpan, passed through the out pointer
        if (FAILED(rt::CallSlot(line, amp::kKaraokeLyricsLineStartTime, &ticks)) || ticks < 0) return -1;
        return TicksToMs(ticks);
    } catch (...) {
        return -1;
    }
}

namespace {

// The INowPlayingItem Apple's player holds now, or an empty Ref.
rt::Ref PlayingItem() {
    rt::Ref statics, current, player, itemObject, item;
    rt::Activation activation =
        rt::GetActivationFactory(L"AMP.Services.Player", rt::GuidOf(amp::kIidPlayerStatics), statics);
    if (FAILED(activation.result) || FAILED(rt::CallSlot(statics, amp::slot::kPlayerStaticsCurrent, current.Put())) ||
        !current || !rt::QueryAny(current, amp::kIidsPlayer, player) ||
        FAILED(rt::CallSlot(player, amp::slot::kPlayerNowPlayingItem, itemObject.Put())) || !itemObject ||
        !rt::Query(itemObject, rt::GuidOf(amp::kIidsNowPlayingItem[0]), item)) {
        return {};
    }
    return item;
}

// IArtwork of the item Apple's player holds now, or an empty Ref.
rt::Ref PlayingArtwork() {
    rt::Ref item = PlayingItem();
    rt::Ref artworkObject, artwork;
    if (!item || FAILED(rt::CallSlot(item, amp::slot::kItemArtwork, artworkObject.Put())) || !artworkObject ||
        !rt::Query(artworkObject, rt::GuidOf(amp::kIidArtwork), artwork)) {
        return {};
    }
    return artwork;
}

// An IArtwork drawn by Apple's own loader (UIBroker.GetImageSource) at about `pixels` wide, or null.
media::ImageSource BrokerImage(const rt::Ref& artwork, double pixels) {
    if (!artwork) return nullptr;
    rt::Ref broker, image;
    rt::Activation activation = rt::GetActivationFactory(winrt::to_hstring(amp::kClassUiBroker).c_str(),
                                                         rt::GuidOf(amp::kIidUiBrokerStatics), broker);
    if (FAILED(activation.result) ||
        FAILED(rt::CallSlot(broker, amp::kUiBrokerGetImageSource, artwork.Get(), 1.0, pixels, pixels, image.Put())) ||
        !image) {
        return nullptr;
    }
    media::ImageSource source{nullptr};
    winrt::copy_from_abi(source, image.Get());
    return source;
}

}  // namespace

media::ImageSource PlayingArtworkSource(double pixels) noexcept {
    try {
        return BrokerImage(PlayingArtwork(), pixels);
    } catch (...) {
        return nullptr;
    }
}

media::ImageSource QueueArtworkImage(uint64_t itemId, double pixels) noexcept {
    try {
        return BrokerImage(QueuedArtwork(itemId), pixels);
    } catch (...) {
        return nullptr;
    }
}

std::string PlayingCatalogId() noexcept {
    try {
        rt::Ref item = PlayingItem();
        rt::Ref descriptor = item ? ContentDescriptorOf(item) : rt::Ref{};
        std::string id = descriptor ? rt::ReadString(descriptor, amp::slot::kDescriptorAdamId) : std::string();
        return IsCatalogId(id) ? id : std::string();
    } catch (...) {
        return {};
    }
}

bool SetMainPlayerVolume(double gain) noexcept {
    try {
        auto control = MainWindowButton(L"VolumeControl");
        if (!control) {
            NoteVolumeProblem("no VolumeControl in the main window");
            return false;
        }
        rt::Ref unknown(winrt::detach_abi(control.as<winrt::Windows::Foundation::IUnknown>()));
        rt::Ref volume;
        if (!rt::Query(unknown, rt::GuidOf(amp::kIidVolumeControl), volume)) {
            NoteVolumeProblem("the VolumeControl is not the interface this Apple Music version has");
            return false;
        }
        double position = SliderPositionOfGain(gain);
        if (FAILED(rt::CallSlot(volume.Get(), amp::kVolumeControlPutLevel, position))) {
            NoteVolumeProblem("the volume control refused the level");
            return false;
        }
        double now = -1;
        if (SUCCEEDED(rt::CallSlot(volume.Get(), amp::kVolumeControlGetLevel, &now)) && std::abs(now - position) < 0.01) {
            return true;
        }
        NoteVolumeProblem("the volume control did not keep the level: asked " + std::to_string(position) + ", holds " +
                          std::to_string(now));
    } catch (...) {
        NoteVolumeProblem("an exception while setting the volume");
    }
    return false;
}

std::string TakeMainPlayerVolumeProblem() {
    std::lock_guard<std::mutex> guard(g_volumeProblemLock);
    return std::exchange(g_volumeProblem, std::string());
}

bool PressMainPlayerButton(bool shuffle) noexcept {
    try {
        auto source = MainWindowButton(shuffle ? kShuffle.source : kRepeat.source);
        auto peer = source ? peers::FrameworkElementAutomationPeer::CreatePeerForElement(source) : nullptr;
        if (!peer) return false;
        if (auto toggle = peer.GetPattern(peers::PatternInterface::Toggle).try_as<provider::IToggleProvider>()) {
            toggle.Toggle();
            return true;
        }
        if (auto invoke = peer.GetPattern(peers::PatternInterface::Invoke).try_as<provider::IInvokeProvider>()) {
            invoke.Invoke();
            return true;
        }
    } catch (...) {
        // Nothing may escape into Apple Music's XAML frame.
    }
    return false;
}

}  // namespace ampify::win

#endif
