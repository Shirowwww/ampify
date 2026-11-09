// Windows only: the interface-thread facade, driven through a fake backend (the kernel installs the XAML one).
#include <windows.h>

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

#include "../../src/win/ui_thread.hpp"
#include "../test.hpp"

namespace {

using ampify::win::UiThreadBackend;

constexpr DWORD kFakeThread = 4242;
constexpr DWORD kShortWaitMs = 100;
constexpr DWORD kLongWaitMs = 5000;

bool g_dropWork = false;
bool g_refuse = false;
bool g_attachedNow = false;

bool FakeAttach() { return g_attachedNow = true; }
void FakeDetach() { g_attachedNow = false; }
bool FakeAttached() { return g_attachedNow; }
DWORD FakeThreadId() { return g_attachedNow ? kFakeThread : 0; }

// Runs the work on another thread, as a real queue does (dropping work that throws), or keeps it forever.
bool FakePost(std::function<void()> work) {
    if (g_refuse) return false;
    if (!g_dropWork) {
        std::thread([work = std::move(work)] {
            try {
                work();
            } catch (...) {
            }
        }).detach();
    }
    return true;
}

const UiThreadBackend kFake{FakeAttach, FakeDetach, FakePost, FakeAttached, FakeThreadId};

struct BackendScope {
    BackendScope() {
        g_dropWork = g_refuse = g_attachedNow = false;
        ampify::win::SetUiThreadBackend(&kFake);
    }
    ~BackendScope() { ampify::win::SetUiThreadBackend(nullptr); }
};

}  // namespace

TEST_CASE(UiThread_WithoutBackendNothingIsAttachedAndEverythingFails) {
    ampify::win::SetUiThreadBackend(nullptr);
    CHECK(!ampify::win::AttachUiThread());
    CHECK(!ampify::win::UiThreadAttached());
    CHECK(ampify::win::UiThreadId() == 0);
    CHECK(!ampify::win::PostToUiThread([] {}));
    CHECK(!ampify::win::RunOnUiThreadAndWait([] {}, kShortWaitMs));
}

TEST_CASE(UiThread_AttachingGoesThroughTheBackend) {
    BackendScope scope;
    CHECK(ampify::win::AttachUiThread());
    CHECK(ampify::win::UiThreadAttached());
    CHECK(ampify::win::UiThreadId() == kFakeThread);
    ampify::win::DetachUiThread();
    CHECK(!ampify::win::UiThreadAttached());
}

TEST_CASE(UiThread_RunAndWaitReturnsOnceTheTaskRan) {
    BackendScope scope;
    std::atomic<int> ran{0};
    CHECK(ampify::win::RunOnUiThreadAndWait([&ran] { ran++; }, kLongWaitMs));
    CHECK(ran == 1);
}

TEST_CASE(UiThread_RunAndWaitTimesOutWhenTheTaskNeverRuns) {
    BackendScope scope;
    g_dropWork = true;
    CHECK(!ampify::win::RunOnUiThreadAndWait([] {}, kShortWaitMs));
}

TEST_CASE(UiThread_RunAndWaitFailsAtOnceWhenTheQueueRefuses) {
    BackendScope scope;
    g_refuse = true;
    CHECK(!ampify::win::RunOnUiThreadAndWait([] {}, kLongWaitMs));
}

TEST_CASE(UiThread_RunAndWaitEndsAsSoonAsATaskThrows) {
    BackendScope scope;
    auto start = std::chrono::steady_clock::now();
    ampify::win::RunOnUiThreadAndWait([] { throw std::runtime_error("Apple refused"); }, kLongWaitMs);
    auto waited = std::chrono::steady_clock::now() - start;
    CHECK(waited < std::chrono::milliseconds(kLongWaitMs / 2));
}

TEST_CASE(UiThread_NamedRunWaitsForTheTask) {
    BackendScope scope;
    std::atomic<int> ran{0};
    CHECK(ampify::win::RunNamedOnUiThread("test task", [&ran] { ran++; }));
    CHECK(ran == 1);
}
