#if defined(AMPIFY_WITH_XAML)

#include "notifications.hpp"

// The WinRT headers (through xaml_chrome.hpp) come before windows.h, as in apple_player.cpp.
#include "xaml_chrome.hpp"
#include "win_common.hpp"

#include <windows.h>

#include <string>

#include "held_show.hpp"
#include "winrt_abi.hpp"

namespace ampify::win {

namespace {

constexpr wchar_t kManagerClass[] = L"Microsoft.Windows.AppNotifications.AppNotificationManager";
constexpr char kIidManagerStatics[] = "6CFC0D8D-84A3-5592-B4C6-E3E7E7C680E4";
constexpr char kIidManager[] = "55129688-B4BD-550B-AE6B-C24061954D91";
// Slots and identifiers: Microsoft.Windows.AppNotifications.winmd of the Windows App SDK.
constexpr int kStaticsDefaultSlot = 6;
constexpr int kManagerShowSlot = 11;

// True when the window in front belongs to Apple Music and is not minimized.
bool AppleMusicInFront() {
    HWND front = GetForegroundWindow();
    if (!front) return false;
    DWORD process = 0;
    GetWindowThreadProcessId(front, &process);
    return process == GetCurrentProcessId() && !IsIconic(front);
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

// The vtable entry of AppNotificationManager.Default's Show.
bool ShowEntry(void**& entry, std::string& why) {
    rt::Ref statics;
    if (!ManagerStatics(statics, why)) return false;
    rt::Ref manager;
    HRESULT result = rt::CallSlot(statics, kStaticsDefaultSlot, manager.Put());
    rt::Ref typed;
    if (FAILED(result) || !rt::Query(manager, rt::GuidOf(kIidManager), typed)) {
        why = "AppNotificationManager.Default refused (" + HresultText(result) + ")";
        return false;
    }
    entry = VtableEntry(typed.Get(), kManagerShowSlot);
    return true;
}

constexpr HeldShowTarget kAppNotifications{
    "Notifications: AppNotificationManager.Show hooked",
    "Notifications: cannot hold them back, ",
    "Notifications: one held back ",
    true,
    AppleMusicInFront,
    ShowEntry,
};

}  // namespace

void SetNotificationRule(uirender::NotificationRule rule, Journal& journal) {
    HeldShowHook<kAppNotifications>::SetRule(rule, journal);
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
