#pragma once

// Runs work on the interface thread of this process. Pure Win32, so it compiles with MinGW and is tested under Wine.
//
// A WH_GETMESSAGE hook is installed on the thread that owns the main window. Windows calls the hook each time that
// thread takes a message from its queue, and the hook runs the work queued for it. Work is queued from any thread.

#include <windows.h>

#include <functional>

namespace amk::win {

// Finds the thread that owns a visible top-level window of `processId` and hooks it. `module` is the DLL that holds
// the hook procedure (the kernel). False when the window does not exist yet: the caller retries later.
bool AttachUiThread(DWORD processId, HINSTANCE module);

// Removes the hook and drops the work that was not run yet.
void DetachUiThread();

// Queues work for the interface thread and wakes it up. False when not attached. Work that throws is dropped.
bool PostToUiThread(std::function<void()> work);

// True when the hook is installed.
bool UiThreadAttached();

}  // namespace amk::win
