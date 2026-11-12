// Module automix: Ampify.automix, the mix of the current song into the next one. Natives: src/core/natives/automix.cpp.
// The kernel plays a plan with Apple's player and a second player of its own; the script decides the plan.

namespace ampify::prelude {

extern const char kAutoMix[];
const char kAutoMix[] = R"AMPIFYJS(
  Ampify.automix = {
    state() {
      return JSON.parse(native.automixState());
    },
    schedule(plan) {
      if (plan === null || typeof plan !== "object") throw new TypeError("automix.schedule expects a plan object");
      const reason = native.automixSchedule(JSON.stringify(plan));
      return reason === "" ? { ok: true } : { ok: false, reason };
    },
    cancel(reason) {
      native.automixCancel(reason === undefined ? "" : String(reason));
    },
  };
)AMPIFYJS";

}  // namespace ampify::prelude
