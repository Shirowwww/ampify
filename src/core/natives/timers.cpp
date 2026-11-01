// Natives of the module timers (src/core/prelude/timers.cpp). The due times live in ScriptExtension, whose Pump calls
// dispatch.timer for each timer that is due.

#include <algorithm>
#include <chrono>

#include "../api/bridge.hpp"
#include "../api/module.hpp"

namespace ampify::natives {

namespace {

struct TimerNatives : ScriptBridge {
    AMPIFY_NATIVE(Schedule) {
        ScriptExtension* ext = Owner(ctx);
        uint32_t id = static_cast<uint32_t>(Number(ctx, argc, argv, 0));
        auto delay = std::chrono::milliseconds(std::max<int64_t>(1, static_cast<int64_t>(Number(ctx, argc, argv, 1))));
        // A timer id is scheduled once: rescheduling replaces the previous due time.
        auto& timers = TimersOf(ext);
        for (auto it = timers.begin(); it != timers.end();) {
            it = it->second == id ? timers.erase(it) : std::next(it);
        }
        timers.emplace(Now(ext) + delay, id);
        return JS_UNDEFINED;
    }

    AMPIFY_NATIVE(Cancel) {
        ScriptExtension* ext = Owner(ctx);
        uint32_t id = static_cast<uint32_t>(Number(ctx, argc, argv, 0));
        auto& timers = TimersOf(ext);
        for (auto it = timers.begin(); it != timers.end();) {
            it = it->second == id ? timers.erase(it) : std::next(it);
        }
        return JS_UNDEFINED;
    }
};

}  // namespace

void DefineTimers(api::NativeScope& scope) {
    scope.Function("schedule", TimerNatives::Schedule, 2);
    scope.Function("cancel", TimerNatives::Cancel, 1);
}

}  // namespace ampify::natives
