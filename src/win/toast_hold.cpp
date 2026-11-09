#include "toast_hold.hpp"

#include <windows.h>
#include <appmodel.h>

#include <string>

#include "apple_facts.hpp"
#include "held_show.hpp"
#include "win_common.hpp"
#include "winrt_abi.hpp"

namespace ampify::win {

namespace {

constexpr wchar_t kManagerClass[] = L"Windows.UI.Notifications.ToastNotificationManager";
// Slots and identifiers: Windows.UI.Notifications in Windows.winmd.
constexpr char kIidManagerStatics[] = "50AC103F-D235-4598-BBEF-98FE4D1A3AD4";
constexpr char kIidNotifier[] = "75927B93-03F3-41EC-91D3-6E5BAC1B38E7";
constexpr int kCreateNotifierSlot = 6;
constexpr int kCreateNotifierWithIdSlot = 7;
constexpr int kNotifierShowSlot = 6;

// True when the window in front is Apple Music's, which lives in another process than this one.
bool AppleMusicInFront() {
    HWND front = GetForegroundWindow();
    if (!front || IsIconic(front)) return false;
    DWORD process = 0;
    GetWindowThreadProcessId(front, &process);
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process);
    if (!handle) return false;
    wchar_t path[MAX_PATH] = {};
    DWORD length = MAX_PATH;
    bool apple = false;
    if (QueryFullProcessImageNameW(handle, 0, path, &length)) {
        std::wstring image(path, length);
        size_t slash = image.find_last_of(L"\\/");
        apple = _wcsicmp(image.c_str() + (slash == std::wstring::npos ? 0 : slash + 1), kAppleMusicImageW) == 0;
    }
    CloseHandle(handle);
    return apple;
}

bool Notifier(rt::Ref& notifier, std::string& why) {
    rt::Ref statics;
    rt::Activation activation = rt::GetActivationFactory(kManagerClass, rt::GuidOf(kIidManagerStatics), statics);
    if (FAILED(activation.result) || !statics) {
        why = "ToastNotificationManager not activated (" + HresultText(activation.result) + ")";
        return false;
    }
    rt::Ref created;
    HRESULT result = rt::CallSlot(statics, kCreateNotifierSlot, created.Put());
    if (FAILED(result)) {
        // Outside a package's own identity the parameterless form fails: name the application instead.
        wchar_t id[APPLICATION_USER_MODEL_ID_MAX_LENGTH] = {};
        UINT32 length = APPLICATION_USER_MODEL_ID_MAX_LENGTH;
        if (GetCurrentApplicationUserModelId(&length, id) != ERROR_SUCCESS) {
            why = "CreateToastNotifier refused (" + HresultText(result) + ") and no application id";
            return false;
        }
        rt::HString name(id);
        result = rt::CallSlot(statics, kCreateNotifierWithIdSlot, name.Get(), created.Put());
    }
    if (FAILED(result) || !rt::Query(created, rt::GuidOf(kIidNotifier), notifier)) {
        why = "CreateToastNotifier refused (" + HresultText(result) + ")";
        return false;
    }
    return true;
}

// The vtable entry of a notifier's Show.
bool ShowEntry(void**& entry, std::string& why) {
    rt::Ref notifier;
    if (!Notifier(notifier, why)) return false;
    entry = VtableEntry(notifier.Get(), kNotifierShowSlot);
    return true;
}

constexpr HeldShowTarget kToasts{
    "Notifications: ToastNotifier.Show hooked in the library process",
    "Notifications: cannot hold back the library process's ones, ",
    "Notifications: one held back in the library process ",
    false,
    AppleMusicInFront,
    ShowEntry,
};

}  // namespace

void SetToastRule(uirender::NotificationRule rule, Journal& journal) {
    HeldShowHook<kToasts>::SetRule(rule, journal);
}

}  // namespace ampify::win
