// Module console: the global console, written to the journal through native.log. JavaScript only, no natives.

namespace amk::prelude {

extern const char kConsole[];
const char kConsole[] = R"AMKJS(
  function logLine(prefix, args) {
    native.log(prefix + args.map(describe).join(" "));
  }

  const console = {
    log: (...args) => logLine("", args),
    info: (...args) => logLine("", args),
    warn: (...args) => logLine("[avertissement] ", args),
    error: (...args) => logLine("[erreur] ", args),
  };

  globalThis.console = console;
)AMKJS";

}  // namespace amk::prelude
