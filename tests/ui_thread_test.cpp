// Windows only. Checks the interface-thread facade (win/ui_thread.hpp) the way the kernel uses it:
//
// 1. The main thread creates a Windows.System.DispatcherQueue (CreateDispatcherQueueController of CoreMessaging.dll,
//    DQTYPE_THREAD_CURRENT, DQTAT_COM_NONE), adopts it, and shows a window. A worker thread attaches, queues three
//    tasks with TryEnqueue, and each task must run on the main thread. It is the mechanism of the XAML build, where
//    the queue is the one of Apple Music's main window (Microsoft.UI.Dispatching).
// 2. An installed backend (the XAML build installs one, win/xaml_host.cpp) receives every call of the facade, and
//    removing it brings the fallback back.
//
// Run it on the Windows bench, or under Wine with a display (xvfb-run). When Wine lacks CreateDispatcherQueueController
// or does not run the queue, part 1 prints SKIP with the reason and the program still exits with 0; on Windows a
// failure there is a FAIL.

#include <windows.h>
#include <unknwn.h>

#include <atomic>
#include <cstdio>
#include <functional>
#include <thread>

#include "../win/ui_thread.hpp"

namespace {

std::atomic<int> g_ran{0};
std::atomic<bool> g_onInterfaceThread{true};
DWORD g_interfaceThread = 0;

// CoreMessaging's API and the controller's ABI, declared by hand (MinGW has no header for them).
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

bool UnderWine() {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    return ntdll && GetProcAddress(ntdll, "wine_get_version") != nullptr;
}

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

const amk::win::UiThreadBackend kFakeBackend{FakeAttach, FakeDetach, FakePost, FakeAttached, FakeThreadId};

// Part 2: with a backend installed, every call goes to it.
bool CheckBackend() {
    amk::win::SetUiThreadBackend(&kFakeBackend);
    int ran = 0;
    bool attached = amk::win::AttachUiThread(GetCurrentProcessId(), GetModuleHandleW(nullptr));
    bool posted = amk::win::PostToUiThread([&ran] { ran++; });
    bool reported = amk::win::UiThreadAttached() && amk::win::UiThreadId() == 4242;
    amk::win::DetachUiThread();
    bool detached = !amk::win::UiThreadAttached();
    amk::win::SetUiThreadBackend(nullptr);
    bool fallbackBack = !amk::win::UiThreadAttached() && amk::win::UiThreadId() == 0;
    bool passed = attached && posted && ran == 1 && reported && detached && fallbackBack && FakeBackend::attaches == 1 &&
                  FakeBackend::posts == 1 && FakeBackend::detaches == 1;
    std::printf("backend: attach=%d post=%d ran=%d reported=%d detached=%d fallback_back=%d\n", attached ? 1 : 0,
                posted ? 1 : 0, ran, reported ? 1 : 0, detached ? 1 : 0, fallbackBack ? 1 : 0);
    return passed;
}

enum class Outcome { Pass, Fail, Skip };

// Part 1: a real DispatcherQueue on the main thread, fed from a worker thread.
Outcome CheckQueue(bool wine) {
    HMODULE messaging = LoadLibraryW(L"CoreMessaging.dll");
    auto create = messaging ? reinterpret_cast<CreateController>(reinterpret_cast<void*>(GetProcAddress(messaging, "CreateDispatcherQueueController")))
                            : nullptr;
    if (!create) {
        std::printf("queue: SKIP CoreMessaging.dll!CreateDispatcherQueueController absent%s\n", wine ? " (Wine)" : "");
        return wine ? Outcome::Skip : Outcome::Fail;
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
        std::printf("queue: %s CreateDispatcherQueueController hr=0x%08lX, no queue%s\n", wine ? "SKIP" : "FAIL",
                    static_cast<unsigned long>(created), wine ? " (Wine)" : "");
        if (controller) controller->Release();
        return wine ? Outcome::Skip : Outcome::Fail;
    }
    bool adopted = amk::win::AdoptCurrentThreadQueue(queue);
    queue->Release();

    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"AmkUiThreadTest";
    RegisterClassW(&windowClass);
    HWND window = CreateWindowExW(0, windowClass.lpszClassName, L"amk", WS_OVERLAPPEDWINDOW, 0, 0, 200, 100, nullptr,
                                  nullptr, windowClass.hInstance, nullptr);
    if (!window) {
        std::printf("queue: FAIL window\n");
        controller->Release();
        return Outcome::Fail;
    }
    ShowWindow(window, SW_SHOWNOACTIVATE);
    g_interfaceThread = GetCurrentThreadId();

    std::atomic<bool> attachedOk{false};
    std::atomic<bool> postedOk{true};
    std::atomic<DWORD> attachedThread{0};
    std::thread worker([&] {
        // The window may take a moment to be visible: retry for a few seconds, as the kernel does.
        for (int attempt = 0; attempt < 50 && !attachedOk; attempt++) {
            attachedOk = amk::win::AttachUiThread(GetCurrentProcessId(), GetModuleHandleW(nullptr));
            if (!attachedOk) Sleep(100);
        }
        attachedThread = amk::win::UiThreadId();
        for (int i = 0; i < 3; i++) {
            bool posted = amk::win::PostToUiThread([] {
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
    amk::win::DetachUiThread();
    DestroyWindow(window);
    controller->Release();

    bool passed = adopted && attachedOk && postedOk && g_ran == 3 && g_onInterfaceThread;
    std::printf("queue: adopted=%d attach=%d posted=%d ran=%d on_interface_thread=%d attached_thread=%lu "
                "interface_thread=%lu\n",
                adopted ? 1 : 0, attachedOk ? 1 : 0, postedOk ? 1 : 0, g_ran.load(), g_onInterfaceThread ? 1 : 0,
                static_cast<unsigned long>(attachedThread.load()), static_cast<unsigned long>(g_interfaceThread));
    if (passed) return Outcome::Pass;
    if (wine) {
        std::printf("queue: SKIP Wine does not run this DispatcherQueue the way Windows does\n");
        return Outcome::Skip;
    }
    return Outcome::Fail;
}

}  // namespace

int main() {
    bool wine = UnderWine();
    Outcome queue = CheckQueue(wine);
    bool backendOk = CheckBackend();

    bool passed = queue != Outcome::Fail && backendOk;
    std::printf("%s%s\n", passed ? "PASS" : "FAIL", queue == Outcome::Skip ? " (file DispatcherQueue : SKIP)" : "");
    return passed ? 0 : 1;
}
