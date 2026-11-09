#pragma once
// Holds back the notifications of one of Windows' notification classes by Ampify's rule (never, or while Apple Music is
// the window in front): the first rule other than Apple's own replaces the vtable entry of the class's Show method, and
// the replacement then follows every later rule. Each target (notifications.cpp, toast_hold.cpp) says how to find its
// Show method and how to tell that Apple Music is in front; this header holds the rule, the hook and its log lines.

#include <windows.h>

#include <atomic>
#include <mutex>
#include <string>

#include "../core/journal.hpp"
#include "../core/ui_render/apple_tweaks.hpp"
#include "hook.hpp"

namespace ampify::win {

struct HeldShowTarget {
    const char* hookedLine;   // logged once the entry is replaced
    const char* refusedLine;  // logged, followed by the reason, when it cannot be
    const char* heldLine;     // logged, followed by "(never)" or "(Apple Music in front)", for the first few held
    bool announceRule;        // the rule change itself is logged here (the interface process's hook)
    bool (*appleMusicInFront)();
    // The vtable entry of the Show method, an HRESULT (this, notification). False, with the reason, when not found.
    bool (*findShowEntry)(void**& entry, std::string& why);
};

// One per target: `kTarget` makes each its own state and its own replacement function.
template <const HeldShowTarget& kTarget>
class HeldShowHook {
public:
    // Any thread. A failed hook is logged once and not tried again.
    static void SetRule(uirender::NotificationRule rule, Journal& journal) {
        s_journal = &journal;
        int previous = s_rule.exchange(static_cast<int>(rule));
        if (previous != static_cast<int>(rule)) {
            s_heldReports = 0;
            if (kTarget.announceRule) journal.Write(RuleLine(rule));
        }
        if (rule == uirender::NotificationRule::Apple) return;  // nothing to hook: Apple's own behaviour
        std::lock_guard<std::mutex> lock(s_installLock);
        if (s_tried) return;
        s_tried = true;
        std::string why;
        journal.Write(Install(why) ? std::string(kTarget.hookedLine) : kTarget.refusedLine + why);
    }

private:
    using ShowMethod = HRESULT(STDMETHODCALLTYPE*)(void*, void*);
    static constexpr int kHeldReports = 3;  // held notifications described in the log

    static std::string RuleLine(uirender::NotificationRule rule) {
        using uirender::NotificationRule;
        return std::string("Notifications: ") +
               (rule == NotificationRule::Never        ? "never shown"
                : rule == NotificationRule::Background ? "shown only when Apple Music is not in front"
                                                       : "as Apple Music shows them");
    }

    static HRESULT STDMETHODCALLTYPE Held(void* self, void* notification) {
        auto rule = static_cast<uirender::NotificationRule>(s_rule.load());
        bool hold = rule == uirender::NotificationRule::Never ||
                    (rule == uirender::NotificationRule::Background && kTarget.appleMusicInFront());
        ShowMethod original = s_originalShow.load();
        if (!hold && original) return original(self, notification);
        if (Journal* journal = s_journal.load(); journal && s_heldReports++ < kHeldReports) {
            journal->Write(std::string(kTarget.heldLine) +
                           (rule == uirender::NotificationRule::Never ? "(never)" : "(Apple Music in front)"));
        }
        return S_OK;  // the caller has no "dropped" outcome: it reads the notification as shown
    }

    static bool Install(std::string& why) {
        void** entry = nullptr;
        if (!kTarget.findShowEntry(entry, why)) return false;
        // Known before the entry changes, so a notification shown meanwhile still reaches the original method.
        s_originalShow = reinterpret_cast<ShowMethod>(*entry);
        void* replaced = nullptr;
        return HookVtableEntry(entry, reinterpret_cast<void*>(&Held), &replaced, why);
    }

    static inline std::atomic<int> s_rule{static_cast<int>(uirender::NotificationRule::Apple)};
    static inline std::atomic<ShowMethod> s_originalShow{nullptr};
    static inline std::atomic<Journal*> s_journal{nullptr};
    static inline std::atomic<int> s_heldReports{0};
    static inline std::mutex s_installLock;
    static inline bool s_tried = false;  // under s_installLock
};

}  // namespace ampify::win
