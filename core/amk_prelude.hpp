#pragma once

namespace amk {

// JavaScript that builds the AMK object and the timers on top of the native bridge. Evaluated once per
// extension. It is a function expression: called with the bridge, it returns the entry points the kernel uses
// (stop, timer, key, player). See core/amk_prelude.cpp.
extern const char* const kAmkPrelude;

}  // namespace amk
