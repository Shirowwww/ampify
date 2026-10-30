#pragma once

#include <string_view>

namespace amk {

// True for the modules that no hook may touch: CoreFP and CoreADI64 (FairPlay and device identity),
// and anything named after FairPlay. Every hook the kernel installs must be refused when this returns true.
bool IsProtectedModule(std::string_view modulePath);

}  // namespace amk
