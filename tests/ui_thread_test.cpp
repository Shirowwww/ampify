// Windows only. Checks the interface-thread facade (src/win/ui_thread.hpp) the way the kernel uses it:
//
// 1. The main thread creates a Windows.System.DispatcherQueue (CreateDispatcherQueueController of CoreMessaging.dll,
//    DQTYPE_THREAD_CURRENT, DQTAT_COM_NONE), adopts it, and shows a window. A worker thread attaches, queues three
//    tasks with TryEnqueue, and each task must run on the main thread. It is the mechanism of the XAML build, where
//    the queue is the one of Apple Music's main window (Microsoft.UI.Dispatching).
// 2. An installed backend (the XAML build installs one, src/win/xaml_host.cpp) receives every call of the facade, and
//    removing it brings the fallback back.
//
// Built with the unit tests on Windows; CI runs it beside ampify_tests (Unit tests, Windows).

#include <windows.h>
#include <unknwn.h>

#include <atomic>
#include <cstdio>
#include <functional>
#include <thread>

#include "../src/win/ui_thread.hpp"

namespace {

std::atomic<int> g_ran{0};
std::atomic<bool> g_onInterfaceThread{true};
DWORD g_interfaceThread = 0;

// CoreMessaging's API and the controller's ABI, declared by hand: the test needs no WinRT header.
struct QueueOptions {
    DWORD size;
    int threadType;     // DQTYPE_THREAD_CURRENT = 2
    int apartmentType;  // DQTAT_COM_NONE = 0
};
using CreateController = HRESULT(WINAPI*)(QueueOptions, IUnknown**);

struct InspectableAbi : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetIids(ULONG* count, IID** iids) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetRuntimeClassName(void** name) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTrustLevel(int* level) = 0;
};
struct ControllerAbi : InspectableAbi {
    virtual HRESULT STDMETHODCALLTYPE get_DispatcherQueue(IUnknown** queue) = 0;
    virtual HRESULT STDMETHODCALLTYPE ShutdownQueueAsync(IUnknown** operation) = 0;
};
// {22F34E66-50DB-4E36-A98D-61C01B384D20}
const IID kIidController = {0x22f34e66, 0x50db, 0x4e36, {0xa9, 0x8d, 0x61, 0xc0, 0x1b, 0x38, 0x4d, 0x20}};

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcW(window, message, wParam, lParam);
}

// A fake backend: it runs the work at once, on the calling thread, and counts the calls.
struct FakeBackend {
    static inline int attaches = 0;
    static inline int posts = 0;
    static inline int detaches = 0;
    static inline bool attachedNow = false;
};

bool FakeAttach(DWORD, HINSTANCE) {
    FakeBackend::attaches++;
    FakeBackend::attachedNow = true;
    return true;
}
void FakeDetach() {
    FakeBackend::detaches++;
    FakeBackend::attachedNow = false;
}
bool FakePost(std::function<void()> work) {
    FakeBackend::posts++;
    work();
    return true;
}
bool FakeAttached() { return FakeBackend::attachedNow; }
DWORD FakeThreadId() { return 4242; }

const ampify::win::UiThreadBackend kFakeBackend{FakeAttach, FakeDetach, FakePost, FakeAttached, FakeThreadId};

// Part 2: with a backend installed, every call goes to it.
bool CheckBackend() {
    ampify::win::SetUiThreadBackend(&kFakeBackend);
    int ran = 0;
    bool attached = ampify::win::AttachUiThread(GetCurrentProcessId(), GetModuleHandleW(nullptr));
    bool posted = ampify::win::PostToUiThread([&ran] { ran++; });
    bool reported = ampify::win::UiThreadAttached() && ampify::win::UiThreadId() == 4242;
    ampify::win::DetachUiThread();
    bool detached = !ampify::win::UiThreadAttached();
    ampify::win::SetUiThreadBackend(nullptr);
    bool fallbackBack = !ampify::win::UiThreadAttached() && ampify::win::UiThreadId() == 0;
    bool passed = attached && posted && ran == 1 && reported && detached && fallbackBack && FakeBackend::attaches == 1 &&
                  FakeBackend::posts == 1 && FakeBackend::detaches == 1;
    std::printf("backend: attach=%d post=%d ran=%d reported=%d detached=%d fallback_back=%d\n", attached ? 1 : 0,
                posted ? 1 : 0, ran, reported ? 1 : 0, detached ? 1 : 0, fallbackBack ? 1 : 0);
    return passed;
}

// Part 1: a real DispatcherQueue on the main thread, fed from a worker thread.
bool CheckQueue() {
    HMODULE messaging = LoadLibraryW(L"CoreMessaging.dll");
    auto create = messaging ? reinterpret_cast<CreateController>(reinterpret_cast<void*>(GetProcAddress(messaging, "CreateDispatcherQueueController")))
                            : nullptr;
    if (!create) {
        std::printf("queue: FAIL CoreMessaging.dll!CreateDispatcherQueueController absent\n");
        return false;
    }
    IUnknown* controllerUnknown = nullptr;
    HRESULT created = create(QueueOptions{sizeof(QueueOptions), 2, 0}, &controllerUnknown);
    ControllerAbi* controller = nullptr;
    if (SUCCEEDED(created) && controllerUnknown) {
        controllerUnknown->QueryInterface(kIidController, reinterpret_cast<void**>(&controller));
        controllerUnknown->Release();
    }
    IUnknown* queue = nullptr;
    if (controller) controller->get_DispatcherQueue(&queue);
    if (!queue) {
        std::printf("queue: FAIL CreateDispatcherQueueController hr=0x%08lX, no queue\n",
                    static_cast<unsigned long>(created));
        if (controller) controller->Release();
        return false;
    }
    bool adopted = ampify::win::AdoptCurrentThreadQueue(queue);
    queue->Release();

    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"AmpifyUiThreadTest";
    RegisterClassW(&windowClass);
    HWND window = CreateWindowExW(0, windowClass.lpszClassName, L"ampify", WS_OVERLAPPEDWINDOW, 0, 0, 200, 100, nullptr,
                                  nullptr, windowClass.hInstance, nullptr);
    if (!window) {
        std::printf("queue: FAIL window\n");
        controller->Release();
        return false;
    }
    ShowWindow(window, SW_SHOWNOACTIVATE);
    g_interfaceThread = GetCurrentThreadId();

    std::atomic<bool> attachedOk{false};
    std::atomic<bool> postedOk{true};
    std::atomic<DWORD> attachedThread{0};
    std::thread worker([&] {
        // The window may take a moment to be visible: retry for a few seconds, as the kernel does.
        for (int attempt = 0; attempt < 50 && !attachedOk; attempt++) {
            attachedOk = ampify::win::AttachUiThread(GetCurrentProcessId(), GetModuleHandleW(nullptr));
            if (!attachedOk) Sleep(100);
        }
        attachedThread = ampify::win::UiThreadId();
        for (int i = 0; i < 3; i++) {
            bool posted = ampify::win::PostToUiThread([] {
                if (GetCurrentThreadId() != g_interfaceThread) g_onInterfaceThread = false;
                g_ran++;
            });
            if (!posted) postedOk = false;
        }
    });

    // The queue of DQTYPE_THREAD_CURRENT runs from this thread's message loop.
    MSG message{};
    ULONGLONG deadline = GetTickCount64() + 10000;
    while (g_ran < 3 && GetTickCount64() < deadline) {
        if (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        } else {
            Sleep(10);
        }
    }
    worker.join();
    ampify::win::DetachUiThread();
    DestroyWindow(window);
    controller->Release();

    bool passed = adopted && attachedOk && postedOk && g_ran == 3 && g_onInterfaceThread;
    std::printf("queue: adopted=%d attach=%d posted=%d ran=%d on_interface_thread=%d attached_thread=%lu "
                "interface_thread=%lu\n",
                adopted ? 1 : 0, attachedOk ? 1 : 0, postedOk ? 1 : 0, g_ran.load(), g_onInterfaceThread ? 1 : 0,
                static_cast<unsigned long>(attachedThread.load()), static_cast<unsigned long>(g_interfaceThread));
    return passed;
}

}  // namespace

int main() {
    bool queueOk = CheckQueue();
    bool backendOk = CheckBackend();

    bool passed = queueOk && backendOk;
    std::printf("%s\n", passed ? "PASS" : "FAIL");
    return passed ? 0 : 1;
}
