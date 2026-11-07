// Written against the C++/WinRT projection of Windows App SDK 1.8.

#if defined(AMPIFY_WITH_XAML)

#include "xaml_host.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>

#include "player_controls.hpp"
#include "ui_thread.hpp"
#include "winrt_abi.hpp"

namespace ampify::win {

namespace {

namespace xaml = winrt::Microsoft::UI::Xaml;
namespace dispatching = winrt::Microsoft::UI::Dispatching;

// The state of the DispatcherQueue backend. The queue is agile: any thread may enqueue.
struct DispatchState {
    std::mutex mutex;
    winrt::weak_ref<xaml::Window> window;
    dispatching::DispatcherQueue queue{nullptr};
    Journal* journal = nullptr;
    std::atomic<DWORD> threadId{0};
    std::atomic<bool> firstRun{false};
    std::atomic<bool> unavailableLogged{false};
};

// Never destroyed: a static destructor would release the DispatcherQueue while Apple Music exits.
DispatchState& State() {
    static DispatchState* state = new DispatchState;
    return *state;
}

void Log(const std::string& line) {
    if (Journal* journal = State().journal) journal->Write(line);
}

bool XamlAttach() {
    DispatchState& state = State();
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (state.queue && state.window.get()) return true;
    }
    rt::EnsureMultithreaded();
    xaml::Window window = AppWindowOf(WindowRole::Main);
    if (!window) {
        if (!AppPropertyAvailable() && !state.unavailableLogged.exchange(true)) {
            Log("Interface: AMP.Utils.dll or its GetAppProperty export is not available yet: the XAML window "
                "cannot be reached (retrying every second)");
        }
        return false;
    }
    dispatching::DispatcherQueue queue{nullptr};
    try {
        queue = window.DispatcherQueue();  // documented as callable from any thread
    } catch (...) {
        queue = nullptr;
    }
    if (!queue) return false;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.window = winrt::make_weak(window);
        state.queue = queue;
    }
    Log("Interface: main window obtained through AMP.Utils (GetAppProperty \"MainWindow\"), work goes "
        "through its DispatcherQueue");
    return true;
}

void XamlDetach() {
    DispatchState& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.queue = nullptr;
    state.window = {};
    state.threadId = 0;
}

bool XamlPost(std::function<void()> work) {
    DispatchState& state = State();
    dispatching::DispatcherQueue queue{nullptr};
    bool windowGone = false;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        queue = state.queue;
        windowGone = queue && !state.window.get();
    }
    if (windowGone) {
        // The main window was closed and maybe replaced: take the current one.
        XamlDetach();
        if (!XamlAttach()) return false;
        std::lock_guard<std::mutex> lock(state.mutex);
        queue = state.queue;
    }
    if (!queue) return false;
    try {
        return queue.TryEnqueue(dispatching::DispatcherQueuePriority::Low, [work = std::move(work)]() noexcept {
            DispatchState& current = State();
            try {
                current.threadId = GetCurrentThreadId();
                if (!current.firstRun.exchange(true)) {
                    Log("Interface: first work run by the main window's DispatcherQueue (thread " +
                        std::to_string(GetCurrentThreadId()) + ")");
                }
                work();
            } catch (const winrt::hresult_error& error) {
                Log("Interface: a task on the interface thread failed (" + HresultText(error.code()) + ")");
            } catch (...) {
                Log("Interface: a task on the interface thread failed (unknown exception)");
            }
        });
    } catch (...) {
        return false;  // the queue may be shutting down while the window closes
    }
}

bool XamlAttached() {
    DispatchState& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.queue != nullptr;
}

DWORD XamlThreadId() {
    return State().threadId.load();
}

const UiThreadBackend kXamlBackend{XamlAttach, XamlDetach, XamlPost, XamlAttached, XamlThreadId};

}  // namespace

void InstallXamlUiThread(Journal& journal) {
    State().journal = &journal;
    SetChromeJournal(&journal);
    SetUiThreadBackend(&kXamlBackend);
    InstallPlayerButtons(&ReadMainPlayerModes, &PressMainPlayerButton);
    InstallPlayerVolume(&SetMainPlayerVolume, &TakeMainPlayerVolumeProblem);  // through Apple's own volume model
    InstallLyricsLineStart(&ReadLyricsLineStart);
}

std::uint64_t NowMs() noexcept {
    return GetTickCount64();
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
