// Module console: the global console, written to the journal through native.log. JavaScript only, no natives.

namespace ampify::prelude {

extern const char kConsole[];
const char kConsole[] = R"AMPIFYJS(
  function logLine(prefix, args) {
    native.log(prefix + args.map(describe).join(" "));
  }

  const console = {
    log: (...args) => logLine("", args),
    info: (...args) => logLine("", args),
    warn: (...args) => logLine("[warning] ", args),
    error: (...args) => logLine("[error] ", args),
  };

  globalThis.console = console;
)AMPIFYJS";

}  // namespace ampify::prelude
