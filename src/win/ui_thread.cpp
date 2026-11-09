#include "ui_thread.hpp"

#include <atomic>
#include <memory>
#include <utility>

#include "../core/ui_watchdog.hpp"

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
    // Signalled however the work ends: one that throws (and is dropped) no longer leaves the caller waiting it out.
    struct Signal {
        std::shared_ptr<Done> done;
        ~Signal() { SetEvent(done->event); }
    };
    if (!PostToUiThread([work = std::move(work), done] {
            Signal signal{done};
            work();
        })) {
        return false;
    }
    return WaitForSingleObject(done->event, timeoutMs) == WAIT_OBJECT_0;
}

bool RunNamedOnUiThread(const char* activity, std::function<void()> work) {
    return RunOnUiThreadAndWait(
        [activity, work = std::move(work)] {
            ampify::ActivityScope scope(activity);
            work();
        },
        kInterfaceWaitMs);
}

}  // namespace ampify::win
