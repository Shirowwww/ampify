#include "toast_hold.hpp"

#include <windows.h>
#include <appmodel.h>

#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>

#include "apple_facts.hpp"
#include "hook.hpp"
#include "winrt_abi.hpp"

namespace ampify::win {

namespace {

using uirender::NotificationRule;

constexpr wchar_t kManagerClass[] = L"Windows.UI.Notifications.ToastNotificationManager";
// IToastNotificationManagerStatics: CreateToastNotifier 6, CreateToastNotifierWithId 7. IToastNotifier: Show 6
// (Windows.UI.Notifications in Windows.winmd).
constexpr char kIidManagerStatics[] = "50AC103F-D235-4598-BBEF-98FE4D1A3AD4";
constexpr char kIidNotifier[] = "75927B93-03F3-41EC-91D3-6E5BAC1B38E7";
constexpr int kCreateNotifierSlot = 6;
constexpr int kCreateNotifierWithIdSlot = 7;
constexpr int kNotifierShowSlot = 6;
constexpr int kHeldReports = 3;  // held notifications described in the log

using ShowMethod = HRESULT(STDMETHODCALLTYPE*)(void*, void*);

std::atomic<int> g_rule{static_cast<int>(NotificationRule::Apple)};
std::atomic<ShowMethod> g_originalShow{nullptr};
std::atomic<Journal*> g_journal{nullptr};
std::atomic<int> g_heldReports{0};
std::mutex g_installLock;
bool g_tried = false;  // under g_installLock

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

HRESULT STDMETHODCALLTYPE HeldShow(void* notifier, void* notification) {
    auto rule = static_cast<NotificationRule>(g_rule.load());
    bool hold = rule == NotificationRule::Never || (rule == NotificationRule::Background && AppleMusicInFront());
    ShowMethod original = g_originalShow.load();
    if (!hold && original) return original(notifier, notification);
    if (Journal* journal = g_journal.load(); journal && g_heldReports++ < kHeldReports) {
        journal->Write(std::string("Notifications: one held back in the library process (") +
                       (rule == NotificationRule::Never ? "never" : "Apple Music in front") + ")");
    }
    return S_OK;  // the agent reads it as shown
}

std::string Hex(HRESULT code) {
    char text[16];
    std::snprintf(text, sizeof(text), "0x%08lX", static_cast<unsigned long>(code));
    return text;
}

bool Notifier(rt::Ref& notifier, std::string& why) {
    rt::Ref statics;
    rt::Activation activation = rt::GetActivationFactory(kManagerClass, rt::GuidOf(kIidManagerStatics), statics);
    if (FAILED(activation.result) || !statics) {
        why = "ToastNotificationManager not activated (" + Hex(activation.result) + ")";
        return false;
    }
    rt::Ref created;
    HRESULT result = rt::CallSlot(statics, kCreateNotifierSlot, created.Put());
    if (FAILED(result)) {
        // Outside a package's own identity the parameterless form fails: name the application instead.
        wchar_t id[APPLICATION_USER_MODEL_ID_MAX_LENGTH] = {};
        UINT32 length = APPLICATION_USER_MODEL_ID_MAX_LENGTH;
        if (GetCurrentApplicationUserModelId(&length, id) != ERROR_SUCCESS) {
            why = "CreateToastNotifier refused (" + Hex(result) + ") and no application id";
            return false;
        }
        rt::HString name(id);
        result = rt::CallSlot(statics, kCreateNotifierWithIdSlot, name.Get(), created.Put());
    }
    if (FAILED(result) || !rt::Query(created, rt::GuidOf(kIidNotifier), notifier)) {
        why = "CreateToastNotifier refused (" + Hex(result) + ")";
        return false;
    }
    return true;
}

bool Install(std::string& why) {
    rt::Ref notifier;
    if (!Notifier(notifier, why)) return false;
    void** entry = VtableEntry(notifier.Get(), kNotifierShowSlot);
    // Known before the entry changes, so a notification shown meanwhile still reaches the original method.
    g_originalShow = reinterpret_cast<ShowMethod>(*entry);
    void* replaced = nullptr;
    return HookVtableEntry(entry, reinterpret_cast<void*>(&HeldShow), &replaced, why);
}

}  // namespace

void SetToastRule(NotificationRule rule, Journal& journal) {
    g_journal = &journal;
    int previous = g_rule.exchange(static_cast<int>(rule));
    if (previous != static_cast<int>(rule)) g_heldReports = 0;
    if (rule == NotificationRule::Apple) return;  // nothing to hook: Apple's own behaviour
    std::lock_guard<std::mutex> lock(g_installLock);
    if (g_tried) return;
    g_tried = true;
    std::string why;
    if (Install(why)) {
        journal.Write("Notifications: ToastNotifier.Show hooked in the library process");
    } else {
        journal.Write("Notifications: cannot hold back the library process's ones, " + why);
    }
}

}  // namespace ampify::win
