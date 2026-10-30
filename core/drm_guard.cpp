#include "drm_guard.hpp"

#include <string>

#include "text_util.hpp"

namespace amk {

bool IsProtectedModule(std::string_view modulePath) {
    std::string name = ToLowerAscii(FileName(modulePath));
    size_t dot = name.rfind('.');
    if (dot != std::string::npos) name.erase(dot);  // "CoreFP.dll" gives "corefp"

    if (name == "corefp" || name == "coreadi64") return true;
    return name.find("fairplay") != std::string::npos;
}

}  // namespace amk
