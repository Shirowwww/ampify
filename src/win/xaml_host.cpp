// Written against the C++/WinRT projection of Windows App SDK 1.8.

#if defined(AMPIFY_WITH_XAML)

#include "xaml_host.hpp"

#include <atomic>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>

#include <objbase.h>

#include "player_controls.hpp"
#include "ui_thread.hpp"

namespace ampify::win {

namespace {

namespace xaml = winrt::Microsoft::UI::Xaml;
namespace dispatching = winrt::Microsoft::UI::Dispatching;
using winrt::Windows::Foundation::IInspectable;

using GetAppPropertyFn = IInspectable(__cdecl*)(winrt::hstring const&);

GetAppPropertyFn GetAppPropertyFunction() noexcept {
    static std::atomic<GetAppPropertyFn> cached{nullptr};
    if (GetAppPropertyFn function = cached.load(std::memory_order_acquire)) return function;
    HMODULE utils = GetModuleHandleW(L"AMP.Utils.dll");
    if (!utils) return nullptr;
    auto function = reinterpret_cast<GetAppPropertyFn>(GetProcAddress(utils, kGetAppPropertyExport));
    if (function) cached.store(function, std::memory_order_release);
    return function;
}

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
    EnsureMta();
    xaml::Window window = AppWindow(L"MainWindow");
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
                Log("Interface: a task on the interface thread failed (" + HresultText(error.code().value) + ")");
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

xaml::Window AppWindow(std::wstring_view property) noexcept {
    GetAppPropertyFn function = GetAppPropertyFunction();
    if (!function || property.empty()) return nullptr;
    try {
        IInspectable value = function(winrt::hstring(property));
        return value ? value.try_as<xaml::Window>() : nullptr;
    } catch (const winrt::hresult_error& error) {
        // The mini player's and the full-screen player's windows are absent while closed: E_BOUNDS, not a problem.
        bool playerWindow = property == L"MiniPlayerWindow" || property == L"FullScreenPlayerWindow";
        if (!(playerWindow && error.code() == E_BOUNDS)) {
            static std::atomic<int> logged{0};
            if (logged++ < 3) {
                Log("Interface: GetAppProperty failed (" + HresultText(error.code().value) + ")");
            }
        }
        return nullptr;
    } catch (...) {
        return nullptr;
    }
}

bool AppPropertyAvailable() noexcept {
    return GetAppPropertyFunction() != nullptr;
}

void EnsureMta() noexcept {
    // S_FALSE: already in the MTA. RPC_E_CHANGED_MODE: already single-threaded; WinRT calls still work there.
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    (void)result;
}

void InstallXamlUiThread(Journal& journal) {
    State().journal = &journal;
    SetUiThreadBackend(&kXamlBackend);
    InstallPlayerButtons(&ReadMainPlayerModes, &PressMainPlayerButton);
    InstallPlayerVolume(&SetMainPlayerVolume, &TakeMainPlayerVolumeProblem);  // through Apple's own volume model
    InstallLyricsLineStart(&ReadLyricsLineStart);
}

std::uint64_t NowMs() noexcept {
    return GetTickCount64();
}

std::string HresultText(std::int32_t code) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "0x%08X", static_cast<unsigned>(code));
    return buffer;
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
