#include "ui_thread.hpp"

#include <mutex>
#include <vector>

namespace amk::win {

namespace {

struct HookState {
    std::mutex mutex;
    std::vector<std::function<void()>> queue;
    HHOOK hook = nullptr;
    DWORD threadId = 0;
};

HookState& State() {
    static HookState state;
    return state;
}

// Runs on the interface thread. Takes the whole queue under the lock, then runs it outside the lock, so work can
// queue more work without deadlocking.
void Drain() {
    std::vector<std::function<void()>> work;
    {
        std::lock_guard<std::mutex> lock(State().mutex);
        work.swap(State().queue);
    }
    for (auto& item : work) {
        try {
            item();
        } catch (...) {
            // A failing task must not take the interface down with it. The caller logs what it needs itself.
        }
    }
}

LRESULT CALLBACK UiHookProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code >= 0) Drain();
    return CallNextHookEx(nullptr, code, wParam, lParam);  // the hook handle is ignored by CallNextHookEx
}

struct WindowSearch {
    DWORD processId;
    DWORD threadId;
};

BOOL CALLBACK FindMainWindow(HWND window, LPARAM parameter) {
    auto* search = reinterpret_cast<WindowSearch*>(parameter);

    DWORD owner = 0;
    DWORD thread = GetWindowThreadProcessId(window, &owner);
    if (owner != search->processId || !IsWindowVisible(window)) return TRUE;
    if (GetWindow(window, GW_OWNER) != nullptr) return TRUE;  // a dialog or a tool window, not the main one

    search->threadId = thread;
    return FALSE;
}

}  // namespace

bool AttachUiThread(DWORD processId, HINSTANCE module) {
    HookState& state = State();
    if (state.hook) return true;

    WindowSearch search{processId, 0};
    EnumWindows(FindMainWindow, reinterpret_cast<LPARAM>(&search));
    if (search.threadId == 0) return false;

    HHOOK hook = SetWindowsHookExW(WH_GETMESSAGE, UiHookProc, module, search.threadId);
    if (!hook) return false;

    std::lock_guard<std::mutex> lock(state.mutex);
    state.hook = hook;
    state.threadId = search.threadId;
    return true;
}

void DetachUiThread() {
    HookState& state = State();
    if (!state.hook) return;

    UnhookWindowsHookEx(state.hook);
    std::lock_guard<std::mutex> lock(state.mutex);
    state.hook = nullptr;
    state.threadId = 0;
    state.queue.clear();
}

bool PostToUiThread(std::function<void()> work) {
    HookState& state = State();
    DWORD threadId = 0;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (!state.hook) return false;
        state.queue.push_back(std::move(work));
        threadId = state.threadId;
    }

    // WM_NULL does nothing by itself. Its only use is to make the thread take a message, which runs the hook.
    PostThreadMessageW(threadId, WM_NULL, 0, 0);
    return true;
}

bool UiThreadAttached() {
    std::lock_guard<std::mutex> lock(State().mutex);
    return State().hook != nullptr;
}

}  // namespace amk::win
