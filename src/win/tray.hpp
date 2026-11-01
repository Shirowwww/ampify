#pragma once

// Ampify's notification area icon, with its menu. It lives on its own thread, with a message-only window that
// receives the icon's messages, so the menu stays responsive whatever the JavaScript thread does. A choice in the
// menu, or a left click on the icon (command 0), is handed to the callback on that thread: the callback only queues.
//
// Known limit: a message-only window does not receive TaskbarCreated, so the icon does not come back by itself after
// Explorer restarts. Apple Music has to be restarted then.

#include <windows.h>

#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ampify::win {

struct TrayEntry {
    UINT command = 0;  // from 1; 0 is the left click on the icon
    std::wstring label;
    bool separator = false;
    bool checked = false;
    bool enabled = true;
};

class TrayIcon {
public:
    using OnCommand = std::function<void(UINT command)>;

    TrayIcon() = default;
    ~TrayIcon();

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    // Starts the thread and adds the icon. Waits until Windows accepted or refused the icon. False with `error`.
    bool Start(const std::wstring& tooltip, OnCommand onCommand, std::string* error);

    // The menu shown on a right click, from top to bottom.
    void SetMenu(std::vector<TrayEntry> entries);

    // Removes the icon and ends the thread.
    void Stop();

    bool running() const { return m_window != nullptr; }

    // The window of the icon, for the tests: posting WM_COMMAND with a command id acts as a menu choice.
    HWND window() const { return m_window; }

    static constexpr UINT kCallbackMessage = WM_APP + 1;

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    void ShowMenu();

    OnCommand m_onCommand;
    std::wstring m_tooltip;
    std::mutex m_mutex;
    std::vector<TrayEntry> m_entries;
    HWND m_window = nullptr;
    std::thread m_thread;
};

}  // namespace ampify::win
