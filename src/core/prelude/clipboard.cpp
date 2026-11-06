// Module clipboard: Ampify.clipboard, text copied to and read from the Windows clipboard. Natives:
// src/core/natives/clipboard.cpp.

namespace ampify::prelude {

extern const char kClipboard[];
const char kClipboard[] = R"AMPIFYJS(
  Ampify.clipboard = Object.freeze({
    // Permission clipboard.write. True once the text is on the clipboard.
    copy: (value) => native.clipboardCopy(text(value)),
    // Permission clipboard.read. The text the user copied last, from any program, or null when it is not text.
    paste: () => native.clipboardPaste(),
  });
)AMPIFYJS";

}  // namespace ampify::prelude
