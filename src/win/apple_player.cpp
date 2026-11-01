// Written against the C++/WinRT projection of Windows App SDK 1.8. Apple's interfaces are called by vtable slot, with
// the identifiers and slots of src/core/amp_abi.hpp (checked against knowledge/winmd/AMP.UI.Views.txt by a test).

#if defined(AMPIFY_WITH_XAML)

#include "apple_player.hpp"

// The WinRT headers come before windows.h: its GetClassName macro would rename IAutomationPeer::GetClassName in some
// of them and not in others (the error the CI's build without UNICODE gave).
#include <winrt/Windows.Foundation.h>
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

#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../core/amp_abi.hpp"
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
constexpr int kPressReports = 3;     // presses described in the log, to see what takes them
constexpr int kSearchLimit = 4000;   // elements walked to find Apple's close button
constexpr int kDragStart = 4;        // pixels the pointer moves, button down, before the window follows: a click stays one

// Interface thread only. Never freed: releasing WinRT objects while the process exits can crash it.
struct Placement {
    dispatching::DispatcherQueueTimer timer{nullptr};
    int ticks = 0;
    bool reporting = false;  // laid over: the next tick logs where the window is
    HWND main = nullptr;
    Journal* journal = nullptr;
    winrt::weak_ref<xaml::UIElement> hookedRoot;  // the content that already has the press and Escape handlers
    int pressReports = 0;
    int dragReports = 0;
    bool dragArmed = false;  // the left button went down outside the controls
    bool dragging = false;   // past kDragStart: the window follows the pointer
    POINT pressCursor{};     // screen coordinates of the press
    POINT windowOrigin{};    // the window's top left corner at the press
};

Placement& State() {
    static Placement* state = new Placement();
    return *state;
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

// Past a few pixels with the button down, the window follows the pointer, as from a title bar; a plain click never
// gets here and reaches Apple's own handlers. Ampify moves it itself: the system's move loop (WM_NCLBUTTONDOWN with
// HTCAPTION) did not start on this WinUI window without a title bar (bench, 31/10).
void OnMove(HWND window, const xaml::UIElement& root, xaml::Input::PointerRoutedEventArgs const& args) {
    Placement& state = State();
    if (!state.dragArmed) return;
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
        root.CapturePointer(args.Pointer());  // the moves keep coming while the window slides under the pointer
        if (state.dragReports++ < kPressReports) state.journal->Write("Apple player: the window follows a drag");
    }
    SetWindowPos(window, nullptr, state.windowOrigin.x + dx, state.windowOrigin.y + dy, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    args.Handled(true);
}

xaml::FrameworkElement FindNamed(const xaml::UIElement& root, std::initializer_list<std::wstring_view> names) {
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

// Where the player goes: the main window's visible rectangle, or its monitor's work area when it is minimized.
RECT TargetRect(HWND main) {
    if (IsWindow(main) && !IsIconic(main)) return VisibleRect(main);
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(main, MONITOR_DEFAULTTOPRIMARY), &monitor);
    return monitor.rcWork;
}

// Apple shows its player with the FullScreen presenter, which puts its own style back on every change: an overlapped
// presenter with a border and no title bar is what keeps the window resizable and bare.
void MakeWindowed(const xaml::Window& player, const windowing::AppWindow& window, Journal& journal) {
    std::string before = PresenterName(window.Presenter().Kind());
    auto presenter = windowing::OverlappedPresenter::Create();
    presenter.SetBorderAndTitleBar(true, false);
    presenter.IsResizable(true);
    window.SetPresenter(presenter);
    HWND hwnd = HwndOf(player);
    PlaceVisible(hwnd, TargetRect(State().main));
    HookInput(player, hwnd);
    journal.Write("Apple player: laid over the main window (" + before + " presenter before), " +
                  RectText(VisibleRect(hwnd)));
}

// Whether Apple Music took the window back after the change: logged once, a second later (the bench reads it).
void Report(Journal& journal) {
    xaml::Window player = AppWindow(kPlayerWindowProperty);
    windowing::AppWindow window = player ? player.AppWindow() : nullptr;
    if (!window) {
        journal.Write("Apple player: a second later, its window is gone");
        return;
    }
    journal.Write("Apple player: a second later, " + std::string(window.IsVisible() ? "shown" : "hidden") + ", " +
                  PresenterName(window.Presenter().Kind()) + " presenter, " + RectText(VisibleRect(HwndOf(player))));
}

// Waits for Apple's window and its FullScreen presenter, lays it over the main window, then reports.
void Step(Placement& state) {
    xaml::Window player = AppWindow(kPlayerWindowProperty);
    windowing::AppWindow window = player ? player.AppWindow() : nullptr;
    if (state.reporting) {
        state.timer.Stop();
        if (player) HookInput(player, HwndOf(player));  // a second chance when the content came late
        Report(*state.journal);
        return;
    }
    bool ready = window && window.IsVisible() && window.Presenter().Kind() == windowing::AppWindowPresenterKind::FullScreen;
    if (!ready && ++state.ticks < kPollTicks) return;
    state.timer.Stop();
    if (!ready) {
        state.journal->Write(window ? "Apple player: its window did not go full screen within 3 s, left as it is"
                                    : "Apple player: shown, but Apple Music gave no FullScreenPlayerWindow, left as it is");
        return;
    }
    MakeWindowed(player, window, *state.journal);
    state.reporting = true;
    state.timer.Interval(kReportDelay);
    state.timer.Start();
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

}  // namespace

void OpenApplePlayer(const xaml::Window& mainWindow, Journal& journal) noexcept {
    try {
        if (!mainWindow) {
            journal.Write("Apple player: the main window is not available");
            return;
        }
        Placement& state = State();
        if (state.timer && state.timer.IsRunning()) {
            journal.Write("Apple player: already being opened, request ignored");
            return;
        }
        std::string failure = ShowFullScreenPlayer();
        if (!failure.empty()) {
            journal.Write("Apple player: not opened, " + failure);
            return;
        }
        journal.Write("Apple player: Apple's full-screen player shown");
        state.main = HwndOf(mainWindow);
        state.journal = &journal;
        state.ticks = 0;
        state.reporting = false;
        if (!state.timer) {
            state.timer = mainWindow.DispatcherQueue().CreateTimer();
            state.timer.IsRepeating(true);
            state.timer.Tick([](auto const&, auto const&) noexcept { OnTick(); });
        }
        state.timer.Interval(kPollStep);
        state.timer.Start();
    } catch (const winrt::hresult_error& error) {
        journal.Write("Apple player: not opened (" + HresultText(error.code().value) + ")");
    } catch (...) {
        journal.Write("Apple player: not opened (unknown exception)");
    }
}

}  // namespace ampify::win

#endif
