#include "ui_thread.hpp"

#include <atomic>
#include <memory>
#include <utility>

namespace ampify::win {

namespace {

std::atomic<const UiThreadBackend*> g_backend{nullptr};

const UiThreadBackend* Backend() {
    return g_backend.load(std::memory_order_acquire);
}

}  // namespace

void SetUiThreadBackend(const UiThreadBackend* backend) {
    g_backend.store(backend, std::memory_order_release);
}

bool AttachUiThread() {
    const UiThreadBackend* backend = Backend();
    return backend && backend->attach();
}

void DetachUiThread() {
    if (const UiThreadBackend* backend = Backend()) backend->detach();
}

bool PostToUiThread(std::function<void()> work) {
    const UiThreadBackend* backend = Backend();
    return backend && backend->post(std::move(work));
}

DWORD UiThreadId() {
    const UiThreadBackend* backend = Backend();
    return backend ? backend->threadId() : 0;
}

bool UiThreadAttached() {
    const UiThreadBackend* backend = Backend();
    return backend && backend->attached();
}

bool RunOnUiThreadAndWait(std::function<void()> work, DWORD timeoutMs) {
    if (UiThreadId() != 0 && UiThreadId() == GetCurrentThreadId()) {
        work();
        return true;
    }
    struct Done {
        HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        ~Done() {
            if (event) CloseHandle(event);
        }
    };
    auto done = std::make_shared<Done>();
    if (!done->event) return false;
    if (!PostToUiThread([work = std::move(work), done] {
            work();
            SetEvent(done->event);
        })) {
        return false;
    }
    return WaitForSingleObject(done->event, timeoutMs) == WAIT_OBJECT_0;
}

}  // namespace ampify::win
