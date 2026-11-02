#include "tray.hpp"

#include <shellapi.h>

#include <future>

#include "win_common.hpp"

namespace ampify::win {

namespace {

constexpr wchar_t kWindowClass[] = L"AmpifyTrayIcon";
constexpr UINT kIconId = 1;

void FillIcon(NOTIFYICONDATAW& icon, HWND window) {
    icon = NOTIFYICONDATAW{};
    icon.cbSize = sizeof icon;
    icon.hWnd = window;
    icon.uID = kIconId;
}

}  // namespace

TrayIcon::~TrayIcon() { Stop(); }

bool TrayIcon::Start(const std::wstring& tooltip, OnCommand onCommand, std::string* error) {
    if (m_thread.joinable()) return running();
    m_onCommand = std::move(onCommand);
    m_tooltip = tooltip;

    std::promise<std::string> started;  // empty text on success, the reason otherwise
    std::future<std::string> result = started.get_future();
    m_thread = std::thread([this, &started] {
        HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof windowClass;
        windowClass.lpfnWndProc = WindowProc;
        windowClass.hInstance = instance;
        windowClass.lpszClassName = kWindowClass;
        RegisterClassExW(&windowClass);  // fails harmlessly when already registered by an earlier icon

        HWND window = CreateWindowExW(0, kWindowClass, L"AmpifyTray", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance,
                                      nullptr);
        if (!window) {
            started.set_value("could not create the window (code " + std::to_string(GetLastError()) + ")");
            return;
        }
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

        NOTIFYICONDATAW icon;
        FillIcon(icon, window);
        icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        icon.uCallbackMessage = kCallbackMessage;
        icon.hIcon = AmpifyIcon(false);
        wcsncpy_s(icon.szTip, m_tooltip.c_str(), _TRUNCATE);
        if (!Shell_NotifyIconW(NIM_ADD, &icon)) {
            DestroyWindow(window);
            started.set_value("Shell_NotifyIcon refused the icon");
            return;
        }
        m_window = window;
        started.set_value({});

        MSG message;
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    });

    std::string reason = result.get();
    if (!reason.empty()) {
        m_thread.join();
        if (error) *error = reason;
        return false;
    }
    return true;
}

void TrayIcon::SetMenu(std::vector<TrayEntry> entries) {
    std::lock_guard<std::mutex> guard(m_mutex);
    m_entries = std::move(entries);
}

void TrayIcon::Stop() {
    if (m_window) PostMessageW(m_window, WM_CLOSE, 0, 0);
    if (m_thread.joinable()) m_thread.join();
    m_window = nullptr;
}

void TrayIcon::ShowMenu() {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        for (const TrayEntry& entry : m_entries) {
            if (entry.separator) {
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                continue;
            }
            UINT flags = MF_STRING | (entry.checked ? MF_CHECKED : 0) | (entry.enabled ? 0 : MF_GRAYED);
            AppendMenuW(menu, flags, entry.command, entry.label.c_str());
        }
    }
    POINT cursor{};
    GetCursorPos(&cursor);
    // Without the foreground, the menu would not close when the user clicks elsewhere.
    SetForegroundWindow(m_window);
    UINT command = static_cast<UINT>(TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, cursor.x,
                                                    cursor.y, 0, m_window, nullptr));
    PostMessageW(m_window, WM_NULL, 0, 0);
    DestroyMenu(menu);
    if (command != 0 && m_onCommand) m_onCommand(command);
}

LRESULT CALLBACK TrayIcon::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<TrayIcon*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
        case kCallbackMessage:
            // Classic icon messages: lParam is the mouse message.
            if (self && (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU)) self->ShowMenu();
            if (self && lParam == WM_LBUTTONUP && self->m_onCommand) self->m_onCommand(0);
            return 0;
        case WM_COMMAND:
            if (self && self->m_onCommand && LOWORD(wParam) != 0) self->m_onCommand(LOWORD(wParam));
            return 0;
        case WM_CLOSE: {
            NOTIFYICONDATAW icon;
            FillIcon(icon, window);
            Shell_NotifyIconW(NIM_DELETE, &icon);
            DestroyWindow(window);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(window, message, wParam, lParam);
    }
}

}  // namespace ampify::win
