// Written against the C++/WinRT projection of Windows App SDK 1.8. Apple's interfaces are called by vtable slot, with
// the identifiers and slots of src/core/amp_abi.hpp (checked against knowledge/winmd/AMP.UI.Views.txt by a test).

#if defined(AMPIFY_WITH_XAML)

#include "apple_player.hpp"

// The WinRT headers come before windows.h: its GetClassName macro would rename IAutomationPeer::GetClassName in some
// of them and not in others (the error the CI's build without UNICODE gave).
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Microsoft.UI.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Provider.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.System.h>

#include <windows.h>
#include <dwmapi.h>
#include <unknwn.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../core/amp_abi.hpp"
#include "../core/ui_watchdog.hpp"
#include "player_controls.hpp"
#include "sharp_artwork.hpp"
#include "winrt_abi.hpp"
#include "xaml_host.hpp"

namespace ampify::win {

namespace {

namespace dispatching = winrt::Microsoft::UI::Dispatching;
namespace windowing = winrt::Microsoft::UI::Windowing;
namespace xaml = winrt::Microsoft::UI::Xaml;

constexpr wchar_t kPlayerWindowProperty[] = L"FullScreenPlayerWindow";  // Apple's name for it (AMP.Utils)
constexpr auto kPollStep = std::chrono::milliseconds(50);
constexpr int kPollTicks = 60;  // 3 s for Apple to create the window and give it its FullScreen presenter
constexpr auto kReportDelay = std::chrono::seconds(1);
constexpr auto kWatchStep = std::chrono::milliseconds(250);  // while laid out: notices the close soon enough
constexpr int kPressReports = 3;     // presses described in the log, to see what takes them
constexpr int kSearchLimit = 4000;   // elements walked to find Apple's close button
constexpr int kSettleTicks = 4;      // ticks the main window is watched after a close (a second)
constexpr int kCaptionEvery = 2;    // ticks between two layouts of the caption regions (half a second)
constexpr int kArtworkEvery = 4;    // ticks between two looks for the cover's image (a second)
constexpr int kDragStart = 4;       // pixels the pointer moves, button down, before the window follows: a click stays one

// Interface thread only. Never freed: releasing WinRT objects while the process exits can crash it.
struct Placement {
    dispatching::DispatcherQueueTimer timer{nullptr};
    int ticks = 0;
    uirender::PlayerMode mode = uirender::PlayerMode::Window;
    bool laid = false;           // Ampify placed the player: the timer now follows it until it closes
    bool everLaid = false;       // Ampify placed it once: Apple then shows it again with Ampify's presenter
    bool reportPending = false;  // the log says where the window is a second after it was placed
    std::chrono::steady_clock::time_point reportAt;
    bool mainWasZoomed = false;  // the main window the player replaced was maximized
    RECT lastRect{};             // where the player was last seen, for the main window it replaced
    RECT laidRect{};             // where the player settled once laid out: still there if the person moved nothing
    bool playerZoomed = false;   // the player was last seen maximized
    HWND settleMain = nullptr;   // the main window put back where the player was, while it settles (Settle)
    int settleTicks = 0;
    HWND main = nullptr;
    winrt::weak_ref<xaml::UIElement> mainRoot;  // the main window's content, whose shuffle and repeat the player gets
    Journal* journal = nullptr;
    winrt::weak_ref<xaml::UIElement> hookedRoot;  // the content that already has the press and Escape handlers
    int pressReports = 0;
    int dragReports = 0;
    bool dragArmed = false;  // the left button went down outside the controls
    bool dragging = false;   // past kDragStart: the window follows the pointer
    POINT pressCursor{};     // screen coordinates of the press
    POINT windowOrigin{};    // the window's top left corner at the press
    bool captionSet = false;      // the background is a caption for Windows (SetCaption): Ampify moves nothing itself
    bool captionRefused = false;  // SetCaption failed once: logged once
    int captionTicks = 0;         // the regions are laid out again every few ticks
    int artworkTicks = 0;         // the cover is looked for again every few ticks (SharpenPlayerArtwork)
    bool systemMoving = false;      // inside the system's move loop (OnMove)
    bool systemMoveFailed = false;  // the loop did not start once: the drags move the window by hand
    // The row of the progress line and its two times, and Apple's margin on it (WidenScrubberRow).
    winrt::weak_ref<xaml::FrameworkElement> scrubberRow;
    xaml::Thickness scrubberMargin{};
};

Placement& State() {
    static Placement* state = new Placement();
    return *state;
}

// The main window hidden while the player takes its place; any thread may show it again (ShowMainWindowAgain).
std::atomic<HWND>& HiddenMain() {
    static std::atomic<HWND> hidden{nullptr};
    return hidden;
}

using MethodNoArgument = HRESULT(STDMETHODCALLTYPE*)(void*);
using MethodOut = HRESULT(STDMETHODCALLTYPE*)(void*, void**);

template <typename Fn>
Fn Slot(const winrt::com_ptr<::IUnknown>& object, int slot) {
    return reinterpret_cast<Fn>((*reinterpret_cast<void***>(object.get()))[slot]);
}

winrt::com_ptr<::IUnknown> Query(::IUnknown* object, const char* iid) {
    winrt::com_ptr<::IUnknown> out;
    if (object) object->QueryInterface(rt::GuidOf(iid), out.put_void());
    return out;
}

// Shows Apple's player. Empty on success, otherwise what stopped it.
std::string ShowFullScreenPlayer() {
    auto factory = winrt::get_activation_factory<winrt::Windows::Foundation::IActivationFactory>(
        winrt::to_hstring(amp::kFullScreenPlayerClass));
    auto statics = Query(static_cast<::IUnknown*>(winrt::get_abi(factory)), amp::kIidFullScreenPlayerStatics);
    if (!statics) return "IFullScreenPlayerStatics refused (another Apple Music version?)";

    winrt::com_ptr<::IUnknown> instance;
    HRESULT result = Slot<MethodOut>(statics, amp::slot::kFullScreenPlayerStaticsInstance)(statics.get(), instance.put_void());
    if (FAILED(result) || !instance) return "FullScreenPlayer.Instance failed (" + HresultText(result) + ")";
    auto player = Query(instance.get(), amp::kIidFullScreenPlayer);
    if (!player) return "IFullScreenPlayer refused (another Apple Music version?)";

    // Apple gives the player its view model at start. Without one, Show is not called: what it would do is unknown.
    winrt::com_ptr<::IUnknown> viewModel;
    Slot<MethodOut>(player, amp::slot::kFullScreenPlayerViewModel)(player.get(), viewModel.put_void());
    if (!viewModel) return "the player has no view model yet";

    result = Slot<MethodNoArgument>(player, amp::slot::kFullScreenPlayerShow)(player.get());
    if (FAILED(result)) return "FullScreenPlayer.Show failed (" + HresultText(result) + ")";
    return "";
}

HWND HwndOf(const xaml::Window& window) {
    return reinterpret_cast<HWND>(static_cast<std::uintptr_t>(window.AppWindow().Id().Value));
}

// The rectangle the user sees: without the invisible resize borders of Windows 11.
RECT VisibleRect(HWND window) {
    RECT rect{};
    if (FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect)))) GetWindowRect(window, &rect);
    return rect;
}

std::string RectText(const RECT& rect) {
    return std::to_string(rect.right - rect.left) + "x" + std::to_string(rect.bottom - rect.top) + " at " +
           std::to_string(rect.left) + "," + std::to_string(rect.top);
}

// Places `window` so that its visible rectangle is `target`, whatever its invisible borders are.
void PlaceVisible(HWND window, const RECT& target) {
    for (int pass = 0; pass < 2; pass++) {
        RECT outer{};
        GetWindowRect(window, &outer);
        RECT shown = VisibleRect(window);
        int left = target.left - (shown.left - outer.left);
        int top = target.top - (shown.top - outer.top);
        int width = (target.right - target.left) + (shown.left - outer.left) + (outer.right - shown.right);
        int height = (target.bottom - target.top) + (shown.top - outer.top) + (outer.bottom - shown.bottom);
        SetWindowPos(window, HWND_TOP, left, top, width, height, SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
    }
}

const char* PresenterName(windowing::AppWindowPresenterKind kind) {
    switch (kind) {
        case windowing::AppWindowPresenterKind::FullScreen: return "full screen";
        case windowing::AppWindowPresenterKind::Overlapped: return "overlapped";
        case windowing::AppWindowPresenterKind::CompactOverlay: return "compact overlay";
        default: return "default";
    }
}

// True when the press landed on something that takes it itself: a button, a slider, a list item, a text field.
bool OnControl(xaml::DependencyObject node, const xaml::DependencyObject& root) {
    namespace primitives = xaml::Controls::Primitives;
    for (int depth = 0; node && node != root && depth < 64; depth++) {
        if (node.try_as<primitives::ButtonBase>() || node.try_as<primitives::RangeBase>() ||
            node.try_as<primitives::SelectorItem>() || node.try_as<primitives::Thumb>() ||
            node.try_as<xaml::Controls::TextBox>()) {
            return true;
        }
        node = xaml::Media::VisualTreeHelper::GetParent(node);
    }
    return false;
}

windowing::AppWindow AppWindowOf(HWND window) {
    return windowing::AppWindow::GetFromWindowId(
        winrt::Microsoft::UI::WindowId{static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(window))});
}

// The rectangles, in the window's client pixels, of what takes a press itself, without going into it: a button, a
// slider, a list row, a text field, or a list that scrolls (the lyrics).
void ControlRects(const xaml::UIElement& root, double scale, std::vector<winrt::Windows::Graphics::RectInt32>& out) {
    namespace primitives = xaml::Controls::Primitives;
    std::vector<xaml::DependencyObject> queue{root};
    for (std::size_t i = 0; i < queue.size() && i < static_cast<std::size_t>(kSearchLimit); i++) {
        auto element = queue[i].try_as<xaml::FrameworkElement>();
        if (element && (element.Visibility() != xaml::Visibility::Visible || element.ActualWidth() <= 0)) continue;
        auto scroller = queue[i].try_as<xaml::Controls::ScrollViewer>();
        bool takes = queue[i].try_as<primitives::ButtonBase>() || queue[i].try_as<primitives::RangeBase>() ||
                     queue[i].try_as<primitives::SelectorItem>() || queue[i].try_as<xaml::Controls::TextBox>() ||
                     (scroller && scroller.ScrollableHeight() > 0);
        if (element && takes && element != root) {
            auto box = element.TransformToVisual(root).TransformBounds(
                {0, 0, static_cast<float>(element.ActualWidth()), static_cast<float>(element.ActualHeight())});
            out.push_back({static_cast<int32_t>(std::floor(box.X * scale)), static_cast<int32_t>(std::floor(box.Y * scale)),
                           static_cast<int32_t>(std::ceil(box.Width * scale)), static_cast<int32_t>(std::ceil(box.Height * scale))});
            continue;
        }
        int count = xaml::Media::VisualTreeHelper::GetChildrenCount(queue[i]);
        for (int child = 0; child < count; child++) queue.push_back(xaml::Media::VisualTreeHelper::GetChild(queue[i], child));
    }
}

// The player's background is a caption for Windows, its controls pass through: a drag from it is the window's own, so
// Windows' snap layouts and PowerToys FancyZones see it, and it ends on the release. Ampify's moves never could: a move
// loop started from a press XAML had taken left the window stuck to the pointer until the next click (05/11). Laid out
// again as the player's layout changes.
void SetCaption(const xaml::Window& player, Placement& state) {
    using winrt::Microsoft::UI::Input::InputNonClientPointerSource;
    using winrt::Microsoft::UI::Input::NonClientRegionKind;
    try {
        auto root = player.Content().try_as<xaml::UIElement>();
        auto xamlRoot = root ? root.XamlRoot() : nullptr;
        if (!xamlRoot) return;
        double scale = xamlRoot.RasterizationScale();
        winrt::Windows::Graphics::SizeInt32 size = player.AppWindow().ClientSize();
        std::vector<winrt::Windows::Graphics::RectInt32> controls;
        ControlRects(root, scale, controls);
        auto source = InputNonClientPointerSource::GetForWindowId(player.AppWindow().Id());
        source.SetRegionRects(NonClientRegionKind::Caption, {{0, 0, size.Width, size.Height}});
        source.SetRegionRects(NonClientRegionKind::Passthrough, controls);
        if (!state.captionSet) {
            state.journal->Write("Apple player: its background is a caption for Windows, " +
                                 std::to_string(controls.size()) + " control(s) pass through");
        }
        state.captionSet = true;
    } catch (const winrt::hresult_error& error) {
        if (!state.captionRefused) state.journal->Write("Apple player: no caption regions (" + HresultText(error.code()) + ")");
        state.captionRefused = true;
    }
}

void ClearCaption(const xaml::Window& player, Placement& state) {
    if (!state.captionSet || !player) return;
    state.captionSet = false;
    try {
        winrt::Microsoft::UI::Input::InputNonClientPointerSource::GetForWindowId(player.AppWindow().Id()).ClearAllRegionRects();
    } catch (const winrt::hresult_error& error) {
        state.journal->Write("Apple player: its caption regions not cleared (" + HresultText(error.code()) + ")");
    }
}

// Ampify's handlers act only on the window Ampify made: Apple's own full screen keeps its behaviour.
bool Windowed(HWND window) {
    windowing::AppWindow app = AppWindowOf(window);
    return app && app.Presenter().Kind() == windowing::AppWindowPresenterKind::Overlapped;
}

// A press that no control takes (the background, the artwork, the titles) arms a drag. Apple's page marks presses
// handled, so the handler sees handled ones too and leaves the controls alone itself.
void OnPress(HWND window, const xaml::UIElement& root, xaml::Input::PointerRoutedEventArgs const& args) {
    Placement& state = State();
    state.dragArmed = false;
    state.dragging = false;  // a system move loop ends without a release reaching XAML
    auto point = args.GetCurrentPoint(root);
    if (!point.Properties().IsLeftButtonPressed() || !Windowed(window)) return;
    auto source = args.OriginalSource().try_as<xaml::DependencyObject>();
    bool control = OnControl(source, root);
    if (state.pressReports < kPressReports) {
        state.pressReports++;
        std::string what = source ? winrt::to_string(winrt::get_class_name(source)) : "nothing";
        state.journal->Write("Apple player: press on " + what + (control ? ", left to the control" : ", can move the window"));
    }
    state.dragArmed = !control;
    RECT outer{};
    GetWindowRect(window, &outer);
    GetCursorPos(&state.pressCursor);
    state.windowOrigin = {outer.left, outer.top};
}

void OnRelease(const xaml::UIElement& root) {
    Placement& state = State();
    state.dragArmed = false;
    if (std::exchange(state.dragging, false)) root.ReleasePointerCaptures();
}

// Past a few pixels with the button down, the window moves as from a title bar; a plain click never gets here and
// reaches Apple's own handlers. The system's move loop first: Windows' snap layouts and PowerToys FancyZones only see a
// window moved by it. A press on a caption, sent while the button is down, so the loop ends on the release: SC_MOVE
// (WM_SYSCOMMAND) moved the window as from its menu, stuck to the pointer until the next click (05/11), and posted
// rather than sent, a message ran only once the button was up. A return with the button still down means no loop
// started (a posted press did not on the bench, 31/10), and Ampify moves the window itself from then on.
void OnMove(HWND window, const xaml::UIElement& root, xaml::Input::PointerRoutedEventArgs const& args) {
    Placement& state = State();
    if (!state.dragArmed || state.systemMoving || state.captionSet) return;  // with a caption, Windows moves it
    if (!args.GetCurrentPoint(root).Properties().IsLeftButtonPressed()) {
        OnRelease(root);
        return;
    }
    POINT cursor{};
    GetCursorPos(&cursor);
    int dx = cursor.x - state.pressCursor.x, dy = cursor.y - state.pressCursor.y;
    if (!state.dragging) {
        if (dx * dx + dy * dy < kDragStart * kDragStart) return;
        state.dragging = true;
        if (!state.systemMoveFailed) {
            args.Handled(true);
            root.ReleasePointerCaptures();
            ReleaseCapture();
            state.systemMoving = true;
            SendMessageW(window, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(cursor.x, cursor.y));
            state.systemMoving = false;
            if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0) {
                if (state.dragReports++ < kPressReports) state.journal->Write("Apple player: moved by Windows' move loop");
                state.dragging = false;
                state.dragArmed = false;
                return;
            }
            state.systemMoveFailed = true;
            state.journal->Write("Apple player: the system's move loop did not start, the window follows the pointer");
        }
        root.CapturePointer(args.Pointer());  // the moves keep coming while the window slides under the pointer
        if (state.dragReports++ < kPressReports) state.journal->Write("Apple player: the window follows a drag");
    }
    SetWindowPos(window, nullptr, state.windowOrigin.x + dx, state.windowOrigin.y + dy, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    args.Handled(true);
}

xaml::FrameworkElement FindNamed(const xaml::UIElement& root, std::initializer_list<std::wstring_view> names) {
    ampify::ActivityScope activity("full-screen player tree search");
    std::vector<xaml::DependencyObject> queue{root};
    for (std::size_t i = 0; i < queue.size() && i < static_cast<std::size_t>(kSearchLimit); i++) {
        if (auto element = queue[i].try_as<xaml::FrameworkElement>()) {
            for (std::wstring_view name : names) {
                if (element.Name() == name) return element;
            }
        }
        int count = xaml::Media::VisualTreeHelper::GetChildrenCount(queue[i]);
        for (int child = 0; child < count; child++) queue.push_back(xaml::Media::VisualTreeHelper::GetChild(queue[i], child));
    }
    return nullptr;
}

// Escape closes the player with Apple's own close button, so Apple Music knows it is closed and can show it again.
// True when it did.
bool OnEscape(const xaml::UIElement& root) {
    Journal& journal = *State().journal;
    xaml::FrameworkElement button = FindNamed(root, {L"CloseFullScreenButton", L"FullScreenCloseButton"});
    auto peer = button ? xaml::Automation::Peers::FrameworkElementAutomationPeer::CreatePeerForElement(button) : nullptr;
    auto pattern = peer ? peer.GetPattern(xaml::Automation::Peers::PatternInterface::Invoke) : nullptr;
    if (auto invoke = pattern.try_as<xaml::Automation::Provider::IInvokeProvider>()) {
        invoke.Invoke();
        journal.Write("Apple player: closed with Escape, through Apple's close button");
        return true;
    }
    journal.Write("Apple player: Escape pressed, but Apple's close button was not found");
    return false;
}

void HookInput(const xaml::Window& player, HWND window) {
    auto root = player.Content().try_as<xaml::UIElement>();
    if (!root) {
        State().journal->Write("Apple player: its window has no content yet, no drag nor Escape");
        return;
    }
    if (State().hookedRoot.get() == root) return;
    auto weakRoot = winrt::make_weak(root);
    auto onPress = [window, weakRoot](auto const&, xaml::Input::PointerRoutedEventArgs const& args) noexcept {
        try {
            if (auto element = weakRoot.get()) OnPress(window, element, args);
        } catch (...) {
        }
    };
    auto onMove = [window, weakRoot](auto const&, xaml::Input::PointerRoutedEventArgs const& args) noexcept {
        try {
            if (auto element = weakRoot.get()) OnMove(window, element, args);
        } catch (...) {
        }
    };
    auto onRelease = [weakRoot](auto const&, xaml::Input::PointerRoutedEventArgs const&) noexcept {
        try {
            if (auto element = weakRoot.get()) OnRelease(element);
        } catch (...) {
        }
    };
    auto onKey = [window, weakRoot](auto const&, xaml::Input::KeyRoutedEventArgs const& args) noexcept {
        try {
            auto element = weakRoot.get();
            if (!element || args.Key() != winrt::Windows::System::VirtualKey::Escape || args.KeyStatus().WasKeyDown) return;
            if (Windowed(window) && OnEscape(element)) args.Handled(true);
        } catch (...) {
        }
    };
    root.AddHandler(xaml::UIElement::PointerPressedEvent(), winrt::box_value(xaml::Input::PointerEventHandler(onPress)), true);
    root.AddHandler(xaml::UIElement::PointerMovedEvent(), winrt::box_value(xaml::Input::PointerEventHandler(onMove)), true);
    // Not PointerCaptureLost: taking the capture from Apple's element raises it, and would end the drag at once.
    root.AddHandler(xaml::UIElement::PointerReleasedEvent(), winrt::box_value(xaml::Input::PointerEventHandler(onRelease)), true);
    root.AddHandler(xaml::UIElement::KeyDownEvent(), winrt::box_value(xaml::Input::KeyEventHandler(onKey)), true);
    State().hookedRoot = weakRoot;
}

// The rectangle of `window`'s monitor: all of it, or the part above the taskbar.
RECT MonitorRect(HWND window, bool workArea) {
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY), &monitor);
    return workArea ? monitor.rcWork : monitor.rcMonitor;
}

// Where the player goes: the main window's visible rectangle, or its monitor's work area when it is minimized.
RECT TargetRect(HWND main) {
    if (IsWindow(main) && !IsIconic(main)) return VisibleRect(main);
    return MonitorRect(main, true);
}

const char* ModeName(uirender::PlayerMode mode) {
    switch (mode) {
        case uirender::PlayerMode::Screen: return "whole screen";
        case uirender::PlayerMode::WorkArea: return "screen above the taskbar";
        case uirender::PlayerMode::Window: return "over the main window";
        case uirender::PlayerMode::Replace: return "in place of the main window";
    }
    return "?";
}

// Above the taskbar the player reads as a full screen: no frame, square corners. The overlapped presenter leaves the
// dialog frame Apple's window was made with, a 3 px light line around the screen (02/11), and puts it back on any
// style change: the client area fills `target` and a window region cuts the frame off.
void FillWithClient(HWND window, const RECT& target) {
    RECT outer{};
    RECT client{};
    POINT origin{0, 0};
    GetWindowRect(window, &outer);
    GetClientRect(window, &client);
    ClientToScreen(window, &origin);
    int left = origin.x - outer.left;
    int top = origin.y - outer.top;
    int right = outer.right - (origin.x + client.right);
    int bottom = outer.bottom - (origin.y + client.bottom);
    int width = target.right - target.left;
    int height = target.bottom - target.top;
    SetWindowPos(window, HWND_TOP, target.left - left, target.top - top, width + left + right, height + top + bottom,
                 SWP_NOOWNERZORDER);
    SetWindowRgn(window, CreateRectRgn(left, top, left + width, top + height), TRUE);  // the window owns the region
}

// A window mode gets the system's frame and corners back.
void SetScreenFrame(HWND window, bool screenLike) {
    if (!screenLike) SetWindowRgn(window, nullptr, TRUE);
    DWM_WINDOW_CORNER_PREFERENCE corner = screenLike ? DWMWCP_DONOTROUND : DWMWCP_DEFAULT;
    DwmSetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    COLORREF border = screenLike ? DWMWA_COLOR_NONE : DWMWA_COLOR_DEFAULT;
    DwmSetWindowAttribute(window, DWMWA_BORDER_COLOR, &border, sizeof(border));
}

// Apple's shrink button turns its player into the mini player. Laid out by Ampify, the player stands for the Apple
// Music window: shrinking it left only the mini player in a corner (02/11), so the button goes and its close button
// (or Escape) brings Apple Music back. Apple's own full screen keeps it.
void ShowShrinkButton(const xaml::UIElement& root, bool shown) {
    if (!root) return;
    xaml::FrameworkElement shrink = FindNamed(root, {L"ExitFullScreenButton"});
    xaml::Visibility wanted = shown ? xaml::Visibility::Visible : xaml::Visibility::Collapsed;
    if (!shrink || shrink.Visibility() == wanted) return;
    shrink.Visibility(wanted);
    if (!shown) State().journal->Write("Apple player: its shrink button hidden, its close button brings Apple Music back");
}

// Apple stretches the artwork's box over its whole area and fits the square image in its middle: in a window wider or
// taller than a screen, the box's outline framed two grey bands beside the image. Centred, the box is the image's size.
void HugArtwork(const xaml::UIElement& root) {
    if (!root) return;
    xaml::FrameworkElement artwork = FindNamed(root, {L"ActualArtwork"});
    if (!artwork || artwork.HorizontalAlignment() == xaml::HorizontalAlignment::Center) return;
    artwork.HorizontalAlignment(xaml::HorizontalAlignment::Center);
    artwork.VerticalAlignment(xaml::VerticalAlignment::Center);
}

// Apple gives the progress line's row a quarter of the window's width: in a narrow window the line shrank to a dot
// between its two times, and the Lossless badge above it was cut to its first letters. The row reaches past its column,
// evenly, to the width Apple gives it at its usual size, never closer than a margin to the window's edges.
void WidenScrubberRow(Placement& state, const xaml::UIElement& root) {
    constexpr double kRowWidth = 260;  // Apple's at 1352 pixels wide, 125 %
    constexpr double kEdgeRoom = 48;
    if (!root) return;
    xaml::FrameworkElement row = state.scrubberRow.get();
    if (!row || !row.XamlRoot()) {
        xaml::FrameworkElement scrubber = FindNamed(root, {L"Scrubber"});
        row = scrubber ? xaml::Media::VisualTreeHelper::GetParent(scrubber).try_as<xaml::FrameworkElement>() : nullptr;
        if (!row) return;
        state.scrubberRow = winrt::make_weak(row);
        state.scrubberMargin = row.Margin();
    }
    xaml::Thickness margin = row.Margin();
    const xaml::Thickness& apple = state.scrubberMargin;
    double slot = row.ActualWidth() + margin.Left + margin.Right - apple.Left - apple.Right;
    double wanted = std::min(kRowWidth, static_cast<double>(root.ActualSize().x) - 2 * kEdgeRoom);
    double reach = std::max(0.0, wanted - slot) / 2;
    if (slot <= 0 || (std::abs(margin.Left - (apple.Left - reach)) < 0.5 && std::abs(margin.Right - (apple.Right - reach)) < 0.5)) {
        return;
    }
    row.Margin({apple.Left - reach, margin.Top, apple.Right - reach, margin.Bottom});
}

// Apple shows its player with the FullScreen presenter, which puts its own style back on every change: an overlapped
// presenter is what keeps the window where Ampify puts it. Over or in place of the main window it keeps a border and
// resizes; above the taskbar it has neither, as a full screen that leaves the taskbar.
void Lay(const xaml::Window& player, const windowing::AppWindow& window, Placement& state) {
    std::string before = PresenterName(window.Presenter().Kind());
    bool screenLike = state.mode == uirender::PlayerMode::WorkArea;
    auto presenter = windowing::OverlappedPresenter::Create();
    presenter.SetBorderAndTitleBar(!screenLike, false);
    presenter.IsResizable(!screenLike);
    window.SetPresenter(presenter);
    HWND hwnd = HwndOf(player);
    SetScreenFrame(hwnd, screenLike);
    if (screenLike) {
        FillWithClient(hwnd, MonitorRect(state.main, true));
    } else {
        PlaceVisible(hwnd, TargetRect(state.main));
    }
    state.laidRect = VisibleRect(hwnd);  // taken again once it settles (Step)
    HookInput(player, hwnd);
    ShowShrinkButton(player.Content().try_as<xaml::UIElement>(), false);
    HugArtwork(player.Content().try_as<xaml::UIElement>());
    SharpenPlayerArtwork(player.Content().try_as<xaml::UIElement>(), *state.journal);
    if (screenLike) {
        ClearCaption(player, state);
    } else {
        SetCaption(player, state);
    }
    if (uirender::PlayerReplacesMain(state.mode, IsWindowVisible(state.main) != FALSE, IsIconic(state.main) != FALSE)) {
        state.mainWasZoomed = IsZoomed(state.main) != FALSE;
        ShowWindow(state.main, SW_HIDE);
        HiddenMain() = state.main;
    }
    bool leftMinimized = state.mode == uirender::PlayerMode::Replace && IsIconic(state.main);
    state.journal->Write("Apple player: laid " + std::string(ModeName(state.mode)) + " (" + before +
                         " presenter before), " + RectText(VisibleRect(hwnd)) +
                         (leftMinimized ? ", the main window left minimized (mini player)" : ""));
}

// Whether Apple Music took the window back after the change: logged once, a second later (the bench reads it).
void Report(Journal& journal) {
    xaml::Window player = AppWindow(kPlayerWindowProperty);
    windowing::AppWindow window = player ? player.AppWindow() : nullptr;
    if (!window) {
        journal.Write("Apple player: a second later, its window is gone");
        return;
    }
    HWND hwnd = HwndOf(player);
    RECT cut{};
    bool region = GetWindowRgnBox(hwnd, &cut) != ERROR;
    journal.Write("Apple player: a second later, " + std::string(window.IsVisible() ? "shown" : "hidden") + ", " +
                  PresenterName(window.Presenter().Kind()) + " presenter, " +
                  (region ? "frame cut, " : "") + RectText(VisibleRect(hwnd)));
}

// The player closed: the main window the player replaced comes back where the player was, so the two read as one.
void OnClosed(Placement& state) {
    state.timer.Stop();
    state.laid = false;
    HWND main = HiddenMain().exchange(nullptr);
    if (!main) {
        state.journal->Write("Apple player: closed");
        return;
    }
    // A player moved, snapped or resized hands its place to the main window, even one that was maximized (05/11).
    bool unmoved = EqualRect(&state.lastRect, &state.laidRect) != FALSE;
    bool placed = false;
    if (state.playerZoomed || (state.mainWasZoomed && unmoved)) {
        ShowWindow(main, SW_SHOWMAXIMIZED);
    } else {
        ShowWindow(main, SW_SHOWNOACTIVATE);
        if (state.mainWasZoomed) ShowWindow(main, SW_RESTORE);
        placed = state.lastRect.right > state.lastRect.left;
        if (placed) PlaceVisible(main, state.lastRect);
    }
    SetForegroundWindow(main);
    state.journal->Write("Apple player: closed, the main window is back at " + RectText(VisibleRect(main)) +
                         (placed ? ", the player was at " + RectText(state.lastRect) : std::string()));
    if (!placed) return;
    // Moved to a screen of another scale, the window takes the new scale after this call, and grows or shrinks by its
    // ratio (too wide on a second screen, 05/11): it is put back a few times while it settles.
    state.settleMain = main;
    state.settleTicks = kSettleTicks;
    state.timer.Start();
}

// While the main window settles after a close: back where the player was, as long as something else moves it.
void Settle(Placement& state) {
    RECT now = VisibleRect(state.settleMain);
    if (!EqualRect(&now, &state.lastRect)) {
        PlaceVisible(state.settleMain, state.lastRect);
        state.journal->Write("Apple player: the main window had become " + RectText(now) + ", put back at " +
                             RectText(VisibleRect(state.settleMain)));
    }
    if (--state.settleTicks > 0) return;
    state.settleMain = nullptr;
    state.timer.Stop();
}

// One tick: first wait for Apple's window and its FullScreen presenter, lay it out, then follow it until it closes.
void Step(Placement& state) {
    if (state.settleTicks > 0) return Settle(state);
    xaml::Window player = AppWindow(kPlayerWindowProperty);
    windowing::AppWindow window = player ? player.AppWindow() : nullptr;
    bool visible = window && window.IsVisible();
    if (state.laid) {
        if (!visible) return OnClosed(state);
        HWND hwnd = HwndOf(player);
        state.lastRect = VisibleRect(hwnd);
        state.playerZoomed = IsZoomed(hwnd) != FALSE;
        WidenScrubberRow(state, player.Content().try_as<xaml::UIElement>());
        if (++state.artworkTicks % kArtworkEvery == 0) {
            SharpenPlayerArtwork(player.Content().try_as<xaml::UIElement>(), *state.journal);  // built once a song plays
        }
        bool caption = state.mode != uirender::PlayerMode::WorkArea && !state.captionRefused;
        if (caption && ++state.captionTicks % kCaptionEvery == 0) SetCaption(player, state);  // follows the layout
        if (state.reportPending && std::chrono::steady_clock::now() >= state.reportAt) {
            state.reportPending = false;
            HookInput(player, hwnd);  // a second chance when the content came late
            AddPlayerControls(player.Content(), state.mainRoot.get(), *state.journal);
            ShowShrinkButton(player.Content().try_as<xaml::UIElement>(), false);
            HugArtwork(player.Content().try_as<xaml::UIElement>());
            // The presenter may still have resized the window while it settled.
            if (state.mode == uirender::PlayerMode::WorkArea) FillWithClient(hwnd, MonitorRect(state.main, true));
            state.laidRect = VisibleRect(hwnd);  // settled: a later change is the person's
            Report(*state.journal);
        }
        return;
    }
    // Apple gives a new window the FullScreen presenter after showing it; a window Ampify laid out before keeps
    // Ampify's presenter when it shows again, so it is ready as soon as it shows.
    bool ready = visible && (state.everLaid || window.Presenter().Kind() == windowing::AppWindowPresenterKind::FullScreen);
    if (!ready && ++state.ticks < kPollTicks) return;
    if (!ready) {
        state.timer.Stop();
        state.journal->Write(window ? "Apple player: its window did not go full screen within 3 s, left as it is"
                                    : "Apple player: shown, but Apple Music gave no FullScreenPlayerWindow, left as it is");
        return;
    }
    AddPlayerControls(player.Content(), state.mainRoot.get(), *state.journal);
    HugArtwork(player.Content().try_as<xaml::UIElement>());
    SharpenPlayerArtwork(player.Content().try_as<xaml::UIElement>(), *state.journal);
    if (state.mode == uirender::PlayerMode::Screen) {
        state.timer.Stop();
        if (window.Presenter().Kind() != windowing::AppWindowPresenterKind::FullScreen) {
            ClearCaption(player, state);
            window.SetPresenter(windowing::AppWindowPresenterKind::FullScreen);  // Ampify's from an earlier opening
            SetScreenFrame(HwndOf(player), false);
            ShowShrinkButton(player.Content().try_as<xaml::UIElement>(), true);
        }
        state.journal->Write("Apple player: left on the whole screen, as Apple shows it (setting)");
        return;
    }
    Lay(player, window, state);
    state.laid = true;
    state.everLaid = true;
    state.reportPending = true;
    state.reportAt = std::chrono::steady_clock::now() + kReportDelay;
    state.timer.Interval(kWatchStep);
}

void OnTick() noexcept {
    Placement& state = State();
    try {
        Step(state);
    } catch (const winrt::hresult_error& error) {
        state.timer.Stop();
        state.journal->Write("Apple player: its window could not be made a window (" + HresultText(error.code().value) + ")");
    } catch (...) {
        state.timer.Stop();
        state.journal->Write("Apple player: its window could not be made a window");
    }
}

// Starts waiting for the player's window on the main window's thread, unless a wait or a watch already runs.
void Watch(const xaml::Window& mainWindow, Journal& journal, uirender::PlayerMode mode) {
    Placement& state = State();
    state.mode = mode;
    if (state.timer && state.timer.IsRunning() && state.settleTicks == 0) return;
    state.settleTicks = 0;  // opened again while the main window settled: the player comes first
    state.main = HwndOf(mainWindow);
    state.mainRoot = winrt::make_weak(mainWindow.Content());
    state.journal = &journal;
    state.ticks = 0;
    state.laid = false;
    state.reportPending = false;
    if (!state.timer) {
        state.timer = mainWindow.DispatcherQueue().CreateTimer();
        state.timer.IsRepeating(true);
        state.timer.Tick([](auto const&, auto const&) noexcept { OnTick(); });
    }
    state.timer.Interval(kPollStep);
    state.timer.Start();
}

}  // namespace

void OpenApplePlayer(const xaml::Window& mainWindow, Journal& journal, uirender::PlayerMode mode) noexcept {
    try {
        if (!mainWindow) {
            journal.Write("Apple player: the main window is not available");
            return;
        }
        Placement& state = State();
        if (state.timer && state.timer.IsRunning()) {
            journal.Write("Apple player: already open or being opened, request ignored");
            return;
        }
        std::string failure = ShowFullScreenPlayer();
        if (!failure.empty()) {
            journal.Write("Apple player: not opened, " + failure);
            return;
        }
        journal.Write("Apple player: Apple's full-screen player shown");
        Watch(mainWindow, journal, mode);
    } catch (const winrt::hresult_error& error) {
        journal.Write("Apple player: not opened (" + HresultText(error.code().value) + ")");
    } catch (...) {
        journal.Write("Apple player: not opened (unknown exception)");
    }
}

void FollowApplePlayer(const xaml::Window& mainWindow, Journal& journal, uirender::PlayerMode mode) noexcept {
    try {
        State().mode = mode;
        if (!mainWindow || (State().timer && State().timer.IsRunning())) return;
        if (mode == uirender::PlayerMode::Screen && !State().everLaid) return;  // Apple's own way, nothing to undo
        xaml::Window player = AppWindow(kPlayerWindowProperty);
        windowing::AppWindow window = player ? player.AppWindow() : nullptr;
        bool fresh = window && window.Presenter().Kind() == windowing::AppWindowPresenterKind::FullScreen;
        if (!window || !window.IsVisible() || !(fresh || State().everLaid)) return;
        journal.Write("Apple player: opened by Apple Music itself");
        Watch(mainWindow, journal, mode);
    } catch (...) {
    }
}

void ShowMainWindowAgain() noexcept {
    if (HWND main = HiddenMain().exchange(nullptr)) ShowWindow(main, SW_SHOW);
}

}  // namespace ampify::win

#endif
