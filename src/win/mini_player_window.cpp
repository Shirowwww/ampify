// Built with AMPIFY_WITH_XAML, like the interface service that calls it.

#if defined(AMPIFY_WITH_XAML)

#include "mini_player_window.hpp"
#include "xaml_ending.hpp"

#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.h>

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "../core/amp_abi.hpp"
#include "winrt_abi.hpp"
#include "win_common.hpp"
#include "xaml_chrome.hpp"

namespace ampify::win {

namespace {

namespace xaml = winrt::Microsoft::UI::Xaml;

constexpr BYTE kOpaque = 255;  // a layered window's alpha

// Shows Apple's mini player. Empty on success, otherwise what stopped it.
std::string ShowMiniPlayer() {
    rt::Ref statics;
    rt::Activation activation = rt::GetActivationFactory(winrt::to_hstring(amp::kMiniPlayerClass).c_str(),
                                                         rt::GuidOf(amp::kIidMiniPlayerStatics), statics);
    if (FAILED(activation.result)) return "IMiniPlayerStatics refused (another Apple Music version?)";
    rt::Ref instance;
    HRESULT result = rt::CallSlot(statics.Get(), amp::slot::kMiniPlayerStaticsInstance, instance.Put());
    if (FAILED(result) || !instance) return "MiniPlayer.Instance failed (" + HresultText(result) + ")";
    rt::Ref player;
    if (!rt::Query(instance, rt::GuidOf(amp::kIidMiniPlayer), player)) return "IMiniPlayer refused (another Apple Music version?)";
    // Apple gives the mini player its view model at start. Without one, Show is not called: what it would do is unknown.
    rt::Ref viewModel;
    rt::CallSlot(player.Get(), amp::slot::kMiniPlayerViewModel, viewModel.Put());
    if (!viewModel) return "the mini player has no view model yet";
    result = rt::CallSlot(player.Get(), amp::slot::kMiniPlayerShow);
    if (FAILED(result)) return "MiniPlayer.Show failed (" + HresultText(result) + ")";
    return "";
}

// As Alt+F4 does: Apple closes the mini player and brings its main window back. Invoking Apple's own cross through its
// automation peer did nothing at times (24/11/2026, lyrics pane open), while the window's close message always works.
void CloseMiniPlayer(const xaml::Window& mini, Journal& journal) {
    HWND hwnd = WindowHandle(mini);
    if (hwnd && PostMessageW(hwnd, WM_CLOSE, 0, 0)) {
        journal.Write("Mini player: closed");
        return;
    }
    journal.Write("Mini player: not closed (error " + std::to_string(GetLastError()) + ")");
}

// A mini player open but minimized or behind other windows comes back to the front, on its own thread.
void RaiseMiniPlayer(const xaml::Window& mini, Journal& journal) {
    auto queue = mini.DispatcherQueue();
    if (!queue) return;
    winrt::weak_ref<xaml::Window> weak = winrt::make_weak(mini);
    Journal* log = &journal;
    TryEnqueueUi(queue, [weak, log]() noexcept {
        try {
            auto live = weak.get();
            if (!live) return;
            if (HWND hwnd = WindowHandle(live); hwnd && IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
            live.Activate();
            log->Write("Mini player: already open, brought to the front");
        } catch (...) {
        }
    });
}

}  // namespace

void RunMiniPlayerAction(MiniPlayerAction action, Journal& journal) noexcept {
    try {
        xaml::Window mini = AppWindowOf(WindowRole::Mini);
        const bool onScreen = mini && WindowOnScreen(WindowHandle(mini));
        if (action == MiniPlayerAction::Toggle) action = onScreen ? MiniPlayerAction::Close : MiniPlayerAction::Open;
        if (action == MiniPlayerAction::Close) {
            if (mini) CloseMiniPlayer(mini, journal);
            return;
        }
        if (mini) {
            RaiseMiniPlayer(mini, journal);
            return;
        }
        std::string failure = ShowMiniPlayer();
        journal.Write(failure.empty() ? "Mini player: opened through Apple's MiniPlayer" : "Mini player: not opened, " + failure);
    } catch (const winrt::hresult_error& error) {
        journal.Write("Mini player: not opened (" + HresultText(error.code().value) + ")");
    } catch (...) {
        journal.Write("Mini player: not opened (unknown exception)");
    }
}

MiniWindowState FollowMiniOpacity(HWND__* window, double opacity, MiniWindowState state, Journal* journal) noexcept {
    if (!window || opacity == state.opacity) return state;
    LONG_PTR style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    if (opacity < 1.0) {
        if (!(style & WS_EX_LAYERED)) {
            SetWindowLongPtrW(window, GWL_EXSTYLE, style | WS_EX_LAYERED);
            state.madeLayered = true;
        }
        BYTE alpha = static_cast<BYTE>(std::lround(std::clamp(opacity, 0.0, 1.0) * kOpaque));
        if (!SetLayeredWindowAttributes(window, 0, alpha, LWA_ALPHA)) {
            if (journal) journal->Write("Mini player: Windows refused its opacity (error " + std::to_string(GetLastError()) + ")");
            return state;
        }
        if (journal) journal->Write("Mini player: window at " + std::to_string(std::lround(opacity * 100)) + " % opacity");
    } else {
        SetLayeredWindowAttributes(window, 0, kOpaque, LWA_ALPHA);
        if (state.madeLayered) SetWindowLongPtrW(window, GWL_EXSTYLE, style & ~static_cast<LONG_PTR>(WS_EX_LAYERED));
        state.madeLayered = false;
        if (journal) journal->Write("Mini player: window opaque again");
    }
    state.opacity = opacity;
    return state;
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
