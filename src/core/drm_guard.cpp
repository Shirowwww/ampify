#include "drm_guard.hpp"

#include <string>

#include "apple_facts.hpp"
#include "text_util.hpp"

namespace ampify {

bool IsProtectedModule(std::string_view modulePath) {
    std::string name = ToLowerAscii(FileName(modulePath));
    size_t dot = name.rfind('.');
    if (dot != std::string::npos) name.erase(dot);  // "CoreFP.dll" gives "corefp"

    for (std::string_view protectedName : kProtectedModuleNames) {
        if (name == ToLowerAscii(protectedName)) return true;
    }
    for (std::string_view part : kProtectedModuleParts) {
        if (name.find(ToLowerAscii(part)) != std::string::npos) return true;
    }
    return false;
}

}  // namespace ampify
