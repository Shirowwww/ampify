#if defined(AMPIFY_WITH_XAML)

#include "close_to_tray.hpp"

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <windowsx.h>

#include <atomic>
#include <string>

#include <winrt/Microsoft.UI.Windowing.h>

#include "../core/i18n.hpp"
#include "xaml_chrome.hpp"

namespace ampify::win {

namespace {

namespace xaml = winrt::Microsoft::UI::Xaml;

constexpr UINT kTrayMessage = WM_APP + 0x41;
constexpr UINT kWakeMessage = WM_APP + 0x42;  // from a later AppleMusic.exe (WakeHiddenWindow)
constexpr UINT_PTR kSubclassId = 0x414D5059;  // "AMPY"
constexpr UINT kIconId = 1;
constexpr UINT kShowCommand = 1;
constexpr UINT kQuitCommand = 2;
constexpr const wchar_t* kTrayClass = kCloseToTrayWindowClass;

std::atomic<bool> g_toTray{false};
std::atomic<HWND> g_main{nullptr};
std::atomic<HWND> g_tray{nullptr};  // hidden window that receives the icon's messages, on the interface thread
std::atomic<bool> g_iconShown{false};
bool g_quitting = false;  // interface thread: the menu's Quit lets the next WM_CLOSE through
bool g_hookFailed = false;  // interface thread
UINT g_taskbarCreated = 0;
Journal* g_journal = nullptr;

void Log(const std::string& line) {
    if (g_journal) g_journal->Write("Close to tray: " + line);
}

std::wstring WideText(const std::string& key) {
    return std::wstring(winrt::to_hstring(i18n::Text(key)));
}

// Apple Music's own small icon, as its window shows it; the executable's otherwise.
HICON AppleIcon(HWND window) {
    if (auto icon = reinterpret_cast<HICON>(SendMessageW(window, WM_GETICON, ICON_SMALL2, 0))) return icon;
    if (auto icon = reinterpret_cast<HICON>(GetClassLongPtrW(window, GCLP_HICONSM))) return icon;
    wchar_t path[MAX_PATH] = {};
    HICON icon = nullptr;
    if (GetModuleFileNameW(nullptr, path, MAX_PATH) && ExtractIconExW(path, 0, nullptr, &icon, 1) == 1 && icon) {
        return icon;
    }
    return LoadIconW(nullptr, MAKEINTRESOURCEW(32512));  // IDI_APPLICATION, which is narrow without UNICODE
}

NOTIFYICONDATAW IconData() {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = g_tray;
    data.uID = kIconId;
    return data;
}

void AddIcon() {
    if (g_iconShown) return;
    NOTIFYICONDATAW data = IconData();
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data.uCallbackMessage = kTrayMessage;
    data.hIcon = AppleIcon(g_main);
    wchar_t title[128] = {};
    if (!GetWindowTextW(g_main, title, 128) || !title[0]) wcscpy_s(title, L"Apple Music");
    wcsncpy_s(data.szTip, title, _TRUNCATE);
    if (!Shell_NotifyIconW(NIM_ADD, &data)) {
        Log("the icon was refused by the notification area");
        return;
    }
    data.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &data);
    g_iconShown = true;
}

void RemoveIcon() {
    if (!g_iconShown.exchange(false)) return;
    NOTIFYICONDATAW data = IconData();
    Shell_NotifyIconW(NIM_DELETE, &data);
}

void ShowAgain() {
    HWND main = g_main;
    RemoveIcon();
    if (!main) return;
    ShowWindow(main, IsIconic(main) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(main);
}

void HideToTray() {
    AddIcon();
    if (!g_iconShown) return;  // without an icon, a hidden window could not be found again: it stays
    ShowWindow(g_main, SW_HIDE);
}

void Quit() {
    g_quitting = true;
    RemoveIcon();
    PostMessageW(g_main, WM_CLOSE, 0, 0);
}

void ShowMenu(HWND owner, int x, int y) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, kShowCommand, WideText("tray.show").c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kQuitCommand, WideText("tray.quit").c_str());
    SetMenuDefaultItem(menu, kShowCommand, FALSE);
    SetForegroundWindow(owner);  // or the menu would not close when the user clicks elsewhere
    UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, x, y, 0, owner, nullptr);
    PostMessageW(owner, WM_NULL, 0, 0);
    DestroyMenu(menu);
    if (command == kShowCommand) ShowAgain();
    if (command == kQuitCommand) Quit();
}

LRESULT CALLBACK TrayProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == kTrayMessage) {
        switch (LOWORD(lParam)) {
            case NIN_SELECT:
            case NIN_KEYSELECT:
                ShowAgain();
                break;
            case WM_CONTEXTMENU:
                ShowMenu(window, GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam));
                break;
        }
        return 0;
    }
    if (message == kWakeMessage) {
        if (g_iconShown) ShowAgain();
        return 0;
    }
    if (message == kCloseToTrayQuitMessage) {
        Quit();
        return 0;
    }
    if (g_taskbarCreated && message == g_taskbarCreated && g_iconShown) {
        g_iconShown = false;  // Explorer started again without the icon
        AddIcon();
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CALLBACK MainProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
    if (message == WM_CLOSE && g_toTray && !g_quitting) {
        HideToTray();
        if (g_iconShown) return 0;
    }
    // Shown again some other way (Apple Music started again from the Start menu): the icon has nothing left to do.
    if (message == WM_SHOWWINDOW && wParam && g_iconShown) RemoveIcon();
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, MainProc, kSubclassId);
    return DefSubclassProc(window, message, wParam, lParam);
}

HWND MakeTrayWindow() {
    HINSTANCE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&TrayProc), &module);
    WNDCLASSEXW type{};
    type.cbSize = sizeof(type);
    type.lpfnWndProc = TrayProc;
    type.hInstance = module;
    type.lpszClassName = kTrayClass;
    RegisterClassExW(&type);  // fails harmlessly when already registered
    // A hidden top-level window rather than a message-only one: only those hear TaskbarCreated.
    return CreateWindowExW(WS_EX_TOOLWINDOW, kTrayClass, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, module, nullptr);
}

// The main window's subclass and the icon's window, made once on the interface thread.
bool Hook(HWND main) {
    HWND tray = MakeTrayWindow();
    if (!tray) {
        Log("no window for the icon (" + std::to_string(GetLastError()) + ")");
        return false;
    }
    if (!SetWindowSubclass(main, MainProc, kSubclassId, 0)) {
        DestroyWindow(tray);
        Log("the main window refused the subclass");
        return false;
    }
    g_tray = tray;
    g_main = main;
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    return true;
}

}  // namespace

void FollowCloseRule(const xaml::Window& mainWindow, bool toTray, Journal& journal) noexcept {
    try {
        g_journal = &journal;
        if (!toTray && !g_main) return;  // never on: Apple Music's window stays untouched
        HWND main = WindowHandle(mainWindow);
        if (!main || g_hookFailed) return;
        if (main != g_main && !Hook(main)) {
            g_hookFailed = true;  // tried once: the log says why
            return;
        }
        bool was = g_toTray.exchange(toTray);
        if (was == toTray) return;
        if (!toTray && g_iconShown) ShowAgain();
        Log(toTray ? "on, closing the main window hides it in the notification area" : "off, closing quits again");
    } catch (...) {
        Log("error while following the rule");
    }
}

bool WakeHiddenWindow() noexcept {
    HWND tray = FindWindowW(kTrayClass, nullptr);
    if (!tray) return false;
    DWORD owner = 0;
    GetWindowThreadProcessId(tray, &owner);
    if (owner == GetCurrentProcessId()) return false;
    // The first process may show its window: it is not in front, and SetForegroundWindow would be refused.
    AllowSetForegroundWindow(owner);
    return PostMessageW(tray, kWakeMessage, 0, 0) != FALSE;
}

void ReleaseCloseToTray() noexcept {
    g_toTray = false;
    HWND main = g_main;
    bool hidden = g_iconShown;
    RemoveIcon();
    if (main && hidden) ShowWindow(main, SW_SHOW);
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
