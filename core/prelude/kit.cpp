// Module kit: AMK.kit, the kit's own command line for the Marketplace page. Permission kit.manage, reserved to the
// Marketplace that ships with the kit. Natives: core/natives/kit.cpp.

namespace amk::prelude {

extern const char kKit[];
const char kKit[] = R"AMKJS(
  const kitRuns = new Map();
  let kitNextRun = 1;

  AMK.kit = Object.freeze({
    // Runs amk with these arguments ("install", "demo", "--yes") and resolves with { code, out, err }.
    run(args) {
      if (!Array.isArray(args) || args.length === 0) throw new TypeError("kit.run attend une liste d'arguments");
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
)AMKJS";

}  // namespace amk::prelude
