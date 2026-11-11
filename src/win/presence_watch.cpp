#include "presence_watch.hpp"

#if defined(AMPIFY_WITH_XAML)

#include <dwmapi.h>

#include <chrono>
#include <cwchar>

#include "../core/journal.hpp"
#include "xaml_chrome.hpp"

#pragma comment(lib, "dwmapi.lib")

namespace ampify::win {

namespace {

constexpr wchar_t kWinUiClass[] = L"WinUIDesktopWin32WindowClass";  // Apple's main, mini and full-screen windows
constexpr LONG kLeastSide = 64;  // smaller WinUI windows of the process are helpers, not something one looks at
constexpr auto kSettle = std::chrono::milliseconds(300);  // a minimize or restore has ended by then

bool g_watching = false;       // interface thread
Journal* g_journal = nullptr;  // interface thread

bool Cloaked(HWND window) noexcept {
    DWORD cloaked = 0;  // on another virtual desktop, or hidden by the shell
    return SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof cloaked)) && cloaked != 0;
}

ScreenBox BoxOf(const RECT& rect) noexcept { return {rect.left, rect.top, rect.right, rect.bottom}; }

// The window in front, of another app, hides all of `box`: a game or a full-screen video while the music plays behind
// it, a maximized window. Not the desktop, nor a see-through overlay (layered), which let what is behind show.
bool HiddenByForeground(const RECT& box) noexcept {
    HWND front = GetForegroundWindow();
    if (!front || !WindowOnScreen(front) || Cloaked(front)) return false;
    DWORD process = 0;
    GetWindowThreadProcessId(front, &process);
    if (process == GetCurrentProcessId()) return false;
    if (GetWindowLongPtrW(front, GWL_EXSTYLE) & (WS_EX_LAYERED | WS_EX_TRANSPARENT)) return false;
    wchar_t name[32] = {};
    GetClassNameW(front, name, 32);
    if (std::wcscmp(name, L"Progman") == 0 || std::wcscmp(name, L"WorkerW") == 0) return false;
    RECT over{};
    return GetWindowRect(front, &over) && Covers(BoxOf(over), BoxOf(box));
}

// Seen: one of Apple Music's windows on screen and not hidden behind another app. The main one, or the full-screen
// player or the mini player, which Apple shows with the main window minimized or hidden: the theme still dresses them
// from the main window's passes.
BOOL CALLBACK FindSeen(HWND window, LPARAM found) noexcept {
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (process != GetCurrentProcessId() || !WindowOnScreen(window) || Cloaked(window)) return TRUE;
    wchar_t name[64] = {};
    GetClassNameW(window, name, 64);
    RECT box{};
    if (std::wcscmp(name, kWinUiClass) != 0 || !GetWindowRect(window, &box)) return TRUE;
    if (box.right - box.left < kLeastSide || box.bottom - box.top < kLeastSide) return TRUE;
    // A window kept on top (the mini player can be) stays in sight over a maximized app.
    const bool onTop = (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    if (!onTop && HiddenByForeground(box)) return TRUE;
    *reinterpret_cast<bool*>(found) = true;
    return FALSE;
}

void Evaluate() noexcept {
    bool seen = false;
    EnumWindows(FindSeen, reinterpret_cast<LPARAM>(&seen));
    if (InterfacePresence().Set(seen) && g_journal) {
        g_journal->Write(seen ? "Presence: a window shows, the drawing work goes on"
                              : "Presence: no window shows (minimized, hidden or covered), the drawing work pauses");
    }
}

// "Minimize start" may come before the window is iconic, and "end" before it is restored: the state is read again a
// moment later, so the work does not wait for the theme's next tick.
void EvaluateSoon() noexcept {
    static winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer later{nullptr};  // interface thread
    try {
        if (!later) {
            later = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
            later.IsRepeating(false);
            later.Interval(kSettle);
            later.Tick([](auto&&, auto&&) noexcept { Evaluate(); });
        }
        later.Start();  // again from now, if a check was already waiting
    } catch (...) {
    }
}

void CALLBACK WindowEvent(HWINEVENTHOOK, DWORD event, HWND window, LONG object, LONG child, DWORD, DWORD) noexcept {
    if (event == EVENT_SYSTEM_FOREGROUND) return Evaluate();  // another app came in front, or went
    if (object != OBJID_WINDOW || child != CHILDID_SELF || !window || GetAncestor(window, GA_ROOT) != window) return;
    Evaluate();
    if (event == EVENT_SYSTEM_MINIMIZESTART || event == EVENT_SYSTEM_MINIMIZEEND) EvaluateSoon();
}

}  // namespace

void WatchPresence(Journal& journal) noexcept {
    if (g_watching) return;
    g_watching = true;
    g_journal = &journal;
    // Out of context, the events come to this thread through its message loop: shown, hidden, minimized, restored,
    // cloaked (another virtual desktop), for the windows of this process; the window in front, for any app. A window
    // in front that grows to full screen without a change of front is seen at the theme's next tick (RefreshPresence).
    const DWORD process = GetCurrentProcessId();
    constexpr DWORD kFlags = WINEVENT_OUTOFCONTEXT;
    SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND, nullptr, WindowEvent, process, 0, kFlags);
    SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_HIDE, nullptr, WindowEvent, process, 0, kFlags);
    SetWinEventHook(EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED, nullptr, WindowEvent, process, 0, kFlags);
    SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, WindowEvent, 0, 0, kFlags);
    Evaluate();  // without the hooks, RefreshPresence from the theme's tick keeps the state within seconds
}

void RefreshPresence() noexcept { Evaluate(); }

namespace {

Presence::Listener OnThreadOf(std::weak_ptr<std::function<void(bool)>> weak) {
    winrt::Microsoft::UI::Dispatching::DispatcherQueue queue{nullptr};
    try {
        queue = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
    } catch (...) {
    }
    return [weak, queue](bool seen) {
        try {
            if (!queue || queue.HasThreadAccess()) {
                if (auto listener = weak.lock()) (*listener)(seen);
                return;
            }
            queue.TryEnqueue([weak, seen] {
                try {
                    if (auto listener = weak.lock()) (*listener)(seen);
                } catch (...) {
                }
            });
        } catch (...) {
        }
    };
}

}  // namespace

SeenListener::SeenListener(std::function<void(bool seen)> listener)
    : m_listener(std::make_shared<std::function<void(bool)>>(std::move(listener))),
      m_presence(InterfacePresence(), OnThreadOf(m_listener)) {}

SeenTimer::SeenTimer() : m_presence([this](bool seen) { Follow(seen); }) {}

SeenTimer::~SeenTimer() {
    try {
        if (m_timer) m_timer.Stop();
    } catch (...) {  // the queue may already have stopped with the process
    }
}

void SeenTimer::Run(winrt::Windows::Foundation::TimeSpan interval, std::function<void()> tick) {
    m_tick = std::move(tick);
    m_wanted = true;
    if (!m_timer) {
        auto queue = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        if (!queue) return;
        m_timer = queue.CreateTimer();
        m_timer.IsRepeating(true);
        // An exception out of a timer's handler is a failed HRESULT for the dispatcher, which ends the whole process.
        m_timer.Tick([this](auto&&, auto&&) noexcept { Tick(); });
    }
    Interval(interval);
    if (InterfacePresence().Seen() && !m_timer.IsRunning()) m_timer.Start();
}

void SeenTimer::Stop() noexcept {
    m_wanted = false;
    try {
        if (m_timer) m_timer.Stop();
    } catch (...) {
    }
}

void SeenTimer::Interval(winrt::Windows::Foundation::TimeSpan interval) {
    if (interval == m_interval || !m_timer) return;
    m_interval = interval;
    m_timer.Interval(interval);
}

void SeenTimer::Follow(bool seen) noexcept {
    if (!m_wanted || !m_timer) return;
    try {
        if (!seen) {
            m_timer.Stop();
            return;
        }
        Tick();  // catches up with what changed while hidden, without waiting a whole interval
        if (m_wanted && !m_timer.IsRunning()) m_timer.Start();
    } catch (...) {
    }
}

void SeenTimer::Tick() noexcept {
    try {
        if (m_tick) m_tick();
    } catch (...) {
        // A handler runs inside Apple Music's XAML frame: nothing may escape it.
    }
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
