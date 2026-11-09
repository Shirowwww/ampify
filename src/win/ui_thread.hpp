#pragma once

// Runs work on the interface thread of this process, through a DispatcherQueue. A thin facade over one backend: the
// XAML build installs it at start (src/win/xaml_host.cpp), and the work goes to window.DispatcherQueue().TryEnqueue
// (Microsoft.UI.Dispatching) of Apple Music's main window, obtained from AMP.Utils.dll. Without a backend nothing is
// attached and every call fails cleanly, which is the state of the unit tests.
//
// No WH_GETMESSAGE hook: on Windows Server 2025 it was installed on the right thread and PostThreadMessage
// succeeded, but the hook was never called, so the posted work never ran.
//
// Work is queued from any thread.

#include <windows.h>

#include <functional>

namespace ampify::win {

// Attaches to the interface thread of this process: the thread of Apple Music's main window. False when there is no
// backend, or the window or its queue does not exist yet: the caller retries later, because Apple Music gives no
// signal when its main window appears.
bool AttachUiThread();

// Detaches and drops the queue. Work already queued may still run.
void DetachUiThread();

// Queues work for the interface thread. False when not attached or when the queue refuses it (shutting down). Work
// that throws is dropped.
bool PostToUiThread(std::function<void()> work);

// Runs `work` on the interface thread and waits for it, at most `timeoutMs`; on that thread already, runs it now. False
// when it could not be queued or did not end in time: it may still run later, so it must own what it touches.
bool RunOnUiThreadAndWait(std::function<void()> work, DWORD timeoutMs);

// How long a caller waits for Apple's objects on the interface thread: its slowest answer seen took close to 2 s.
inline constexpr DWORD kInterfaceWaitMs = 2000;

// RunOnUiThreadAndWait with kInterfaceWaitMs, the work named `activity` for the freeze log (core/ui_watchdog.hpp).
bool RunNamedOnUiThread(const char* activity, std::function<void()> work);

bool UiThreadAttached();

// The interface thread, 0 when not attached or unknown. For diagnostics.
DWORD UiThreadId();

// The way to reach the interface thread. Every function must be set. The functions are called from any thread.
struct UiThreadBackend {
    bool (*attach)();
    void (*detach)();
    bool (*post)(std::function<void()> work);
    bool (*attached)();
    DWORD (*threadId)();
};

// Installs `backend`; null removes it. Call it before AttachUiThread, and not while another thread uses the facade.
// The backend must outlive its use (a static object).
void SetUiThreadBackend(const UiThreadBackend* backend);

}  // namespace ampify::win
