// Windows only. Checks the interface-thread hook with a real window and message loop: the worker thread attaches
// to the main thread's window, queues work, and the work must run on the main thread. Run it under Wine with a
// display (xvfb-run) or on the Windows bench.

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <thread>

#include "../win/ui_thread.hpp"

namespace {

std::atomic<int> g_ran{0};
std::atomic<bool> g_onInterfaceThread{false};
DWORD g_interfaceThread = 0;

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcW(window, message, wParam, lParam);
}

}  // namespace

int main() {
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"AmkUiThreadTest";
    RegisterClassW(&windowClass);

    HWND window = CreateWindowExW(0, windowClass.lpszClassName, L"amk", WS_OVERLAPPEDWINDOW, 0, 0, 200, 100, nullptr,
                                  nullptr, windowClass.hInstance, nullptr);
    if (!window) {
        std::printf("FAIL: window\n");
        return 1;
    }
    ShowWindow(window, SW_SHOWNOACTIVATE);
    g_interfaceThread = GetCurrentThreadId();

    bool attachedOk = false;
    bool postedOk = true;
    std::thread worker([&] {
        // The window may take a moment to be visible: retry for a few seconds, as the kernel does.
        for (int attempt = 0; attempt < 50 && !attachedOk; attempt++) {
            attachedOk = amk::win::AttachUiThread(GetCurrentProcessId(), GetModuleHandleW(nullptr));
            if (!attachedOk) Sleep(100);
        }
        for (int i = 0; i < 3; i++) {
            postedOk = amk::win::PostToUiThread([] {
                g_onInterfaceThread = GetCurrentThreadId() == g_interfaceThread;
                g_ran++;
            }) && postedOk;
        }
    });

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

    bool passed = attachedOk && postedOk && g_ran == 3 && g_onInterfaceThread;
    std::printf("attach=%d posted=%d ran=%d on_interface_thread=%d\n", attachedOk ? 1 : 0, postedOk ? 1 : 0,
                g_ran.load(), g_onInterfaceThread ? 1 : 0);
    std::printf("%s\n", passed ? "PASS" : "FAIL");
    return passed ? 0 : 1;
}
