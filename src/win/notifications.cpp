#if defined(AMPIFY_WITH_XAML)

#include "notifications.hpp"

// The WinRT headers (through xaml_chrome.hpp) come before windows.h, as in apple_player.cpp.
#include "xaml_chrome.hpp"
#include "win_common.hpp"

#include <windows.h>

#include <atomic>
#include <mutex>
#include <string>

#include "hook.hpp"
#include "winrt_abi.hpp"

namespace ampify::win {

namespace {

using uirender::NotificationRule;

constexpr wchar_t kManagerClass[] = L"Microsoft.Windows.AppNotifications.AppNotificationManager";
constexpr char kIidManagerStatics[] = "6CFC0D8D-84A3-5592-B4C6-E3E7E7C680E4";
constexpr char kIidManager[] = "55129688-B4BD-550B-AE6B-C24061954D91";
// IAppNotificationManagerStatics: get_Default is its first method, slot 6. IAppNotificationManager: Register 6,
// Unregister 7, UnregisterAll 8, add_NotificationInvoked 9, remove_NotificationInvoked 10, Show 11
// (Microsoft.Windows.AppNotifications.winmd of the Windows App SDK; the identifiers come from the same file).
constexpr int kStaticsDefaultSlot = 6;
constexpr int kManagerShowSlot = 11;
constexpr int kHeldReports = 3;  // held notifications described in the log

using ShowMethod = HRESULT(STDMETHODCALLTYPE*)(void*, void*);

std::atomic<int> g_rule{static_cast<int>(NotificationRule::Apple)};
std::atomic<ShowMethod> g_originalShow{nullptr};
std::atomic<Journal*> g_journal{nullptr};
std::atomic<int> g_heldReports{0};
std::mutex g_installLock;
bool g_tried = false;  // under g_installLock

// True when the window in front belongs to Apple Music and is not minimized.
bool AppleMusicInFront() {
    HWND front = GetForegroundWindow();
    if (!front) return false;
    DWORD process = 0;
    GetWindowThreadProcessId(front, &process);
    return process == GetCurrentProcessId() && !IsIconic(front);
}

HRESULT STDMETHODCALLTYPE HeldShow(void* manager, void* notification) {
    auto rule = static_cast<NotificationRule>(g_rule.load());
    bool hold = rule == NotificationRule::Never || (rule == NotificationRule::Background && AppleMusicInFront());
    ShowMethod original = g_originalShow.load();
    if (!hold && original) return original(manager, notification);
    if (Journal* journal = g_journal.load(); journal && g_heldReports++ < kHeldReports) {
        journal->Write(std::string("Notifications: one held back (") +
                       (rule == NotificationRule::Never ? "never" : "Apple Music in front") + ")");
    }
    return S_OK;  // Apple Music reads it as shown
}

// The factory of AppNotificationManager: the system's activation, else the Windows App SDK runtime Apple Music has
// already loaded (a self-contained app registers its classes in its manifest only).
bool ManagerStatics(rt::Ref& statics, std::string& why) {
    rt::Activation activation = rt::GetActivationFactory(kManagerClass, rt::GuidOf(kIidManagerStatics), statics);
    if (SUCCEEDED(activation.result) && statics) return true;
    HMODULE runtime = GetModuleHandleW(L"Microsoft.WindowsAppRuntime.dll");
    using GetFactory = HRESULT(WINAPI*)(HSTRING, void**);
    auto get = runtime ? reinterpret_cast<GetFactory>(GetProcAddress(runtime, "DllGetActivationFactory")) : nullptr;
    if (!get) {
        why = "AppNotificationManager not activated (" + HresultText(activation.result) + ") and no Windows App SDK runtime";
        return false;
    }
    rt::HString name(kManagerClass);
    rt::Ref factory;
    HRESULT result = get(name.Get(), factory.Put());
    if (FAILED(result) || !rt::Query(factory, rt::GuidOf(kIidManagerStatics), statics)) {
        why = "AppNotificationManager factory refused (" + HresultText(result) + ")";
        return false;
    }
    return true;
}

bool Install(std::string& why) {
    rt::Ref statics;
    if (!ManagerStatics(statics, why)) return false;
    rt::Ref manager;
    HRESULT result = rt::CallSlot(statics, kStaticsDefaultSlot, manager.Put());
    rt::Ref typed;
    if (FAILED(result) || !rt::Query(manager, rt::GuidOf(kIidManager), typed)) {
        why = "AppNotificationManager.Default refused (" + HresultText(result) + ")";
        return false;
    }
    void** entry = VtableEntry(typed.Get(), kManagerShowSlot);
    // Known before the entry changes, so a notification shown meanwhile still reaches Apple's method.
    g_originalShow = reinterpret_cast<ShowMethod>(*entry);
    void* replaced = nullptr;
    return HookVtableEntry(entry, reinterpret_cast<void*>(&HeldShow), &replaced, why);
}

}  // namespace

void SetNotificationRule(NotificationRule rule, Journal& journal) {
    g_journal = &journal;
    int previous = g_rule.exchange(static_cast<int>(rule));
    if (previous != static_cast<int>(rule)) {
        g_heldReports = 0;
        journal.Write(std::string("Notifications: ") +
                      (rule == NotificationRule::Never        ? "never shown"
                       : rule == NotificationRule::Background ? "shown only when Apple Music is not in front"
                                                              : "as Apple Music shows them"));
    }
    if (rule == NotificationRule::Apple) return;  // nothing to hook: Apple's own behaviour
    std::lock_guard<std::mutex> lock(g_installLock);
    if (g_tried) return;
    g_tried = true;
    std::string why;
    if (Install(why)) {
        journal.Write("Notifications: AppNotificationManager.Show hooked");
    } else {
        journal.Write("Notifications: cannot hold them back, " + why);
    }
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
