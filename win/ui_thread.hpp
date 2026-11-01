#pragma once

// Runs work on the interface thread of this process, through a DispatcherQueue. A thin facade with two backends:
//
// - The XAML build (AMPIFY_WITH_XAML) installs its backend at start (win/xaml_host.cpp): the work goes to
//   window.DispatcherQueue().TryEnqueue (Microsoft.UI.Dispatching) of Apple Music's main window, obtained from
//   AMP.Utils.dll. That is the path used in Apple Music.
// - Without a backend, a Windows.System.DispatcherQueue that the interface thread adopted (AdoptCurrentThreadQueue),
//   reached through its ABI only, so it builds with MinGW and is tested under Wine and on the bench
//   (tests/ui_thread_test.cpp). Same mechanism as the XAML backend, without the projection.
//
// The WH_GETMESSAGE hook used before is gone: on Windows Server 2025 (bench run 41097054052) it was installed on the
// right thread and PostThreadMessage succeeded, but the hook was never called, so the posted work never ran.
//
// Work is queued from any thread.

#include <windows.h>

#include <functional>

namespace ampify::win {

// Interface thread only: adopts the Windows.System.DispatcherQueue of the calling thread for the fallback backend.
// `dispatcherQueue` is an ABI::Windows::System::IDispatcherQueue* (for example from CreateDispatcherQueueController);
// null asks DispatcherQueue.GetForCurrentThread. False when the thread has no queue or WinRT is unavailable.
bool AdoptCurrentThreadQueue(void* dispatcherQueue = nullptr);

// Attaches to the interface thread of `processId`: the thread that owns a visible top-level window of the process
// (fallback backend: that thread must have adopted its queue). `module` is kept for the callers; the DispatcherQueue
// backends do not need it. False when the window or the queue does not exist yet: the caller retries later.
bool AttachUiThread(DWORD processId, HINSTANCE module);

// Detaches and drops the queue. Work already queued may still run.
void DetachUiThread();

// Queues work for the interface thread. False when not attached or when the queue refuses it (shutting down). Work
// that throws is dropped.
bool PostToUiThread(std::function<void()> work);

// Runs `work` on the interface thread and waits for it, at most `timeoutMs`; on that thread already, runs it now. False
// when it could not be queued or did not end in time: it may still run later, so it must own what it touches.
bool RunOnUiThreadAndWait(std::function<void()> work, DWORD timeoutMs);

// True when attached.
bool UiThreadAttached();

// The interface thread, 0 when not attached or unknown. For diagnostics.
DWORD UiThreadId();

// Another way to reach the interface thread. Every function must be set. The functions are called from any thread.
struct UiThreadBackend {
    bool (*attach)(DWORD processId, HINSTANCE module);
    void (*detach)();
    bool (*post)(std::function<void()> work);
    bool (*attached)();
    DWORD (*threadId)();
};

// Installs `backend` in place of the fallback; null puts the fallback back. Call it before AttachUiThread, and not
// while another thread uses the facade. The backend must outlive its use (a static object).
void SetUiThreadBackend(const UiThreadBackend* backend);

}  // namespace ampify::win
