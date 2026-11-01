// Module kit: Ampify.kit, the kit's own command line for the Marketplace page. Permission kit.manage, reserved to the
// Marketplace that ships with the kit. Natives: src/core/natives/kit.cpp.

namespace ampify::prelude {

extern const char kKit[];
const char kKit[] = R"AMPIFYJS(
  const kitRuns = new Map();
  let kitNextRun = 1;

  Ampify.kit = Object.freeze({
    // Runs ampify with these arguments ("install", "demo", "--yes") and resolves with { code, out, err }.
    run(args) {
      if (!Array.isArray(args) || args.length === 0) throw new TypeError("kit.run expects a list of arguments");
      const id = kitNextRun++;
      return new Promise((resolve) => {
        kitRuns.set(id, resolve);
        try {
          native.kitRun(id, JSON.stringify(args.map((arg) => text(arg))));
        } catch (error) {
          kitRuns.delete(id);
          throw error;
        }
      });
    },
  });

  dispatch.kitDone = (payload) => {
    const result = JSON.parse(payload);
    const resolve = kitRuns.get(result.id);
    if (!resolve) return;
    kitRuns.delete(result.id);
    resolve({ code: result.code, out: result.out, err: result.err });
  };
)AMPIFYJS";

}  // namespace ampify::prelude
