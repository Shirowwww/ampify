#include "off_notice.hpp"

#include <fstream>
#include <system_error>

namespace ampify {

std::string OffNoticeKey(const PolicyVerdict& verdict, std::string_view appVersion) {
    if (verdict.allowed) return {};
    if (verdict.cause == OffCause::SafeMode) return "safe-mode";
    bool buildCause = verdict.cause == OffCause::UntestedVersion || verdict.cause == OffCause::WrongExports;
    if (buildCause && !appVersion.empty()) return "version:" + std::string(appVersion);
    return {};
}

bool TakeOffNotice(const std::filesystem::path& file, const PolicyVerdict& verdict, std::string_view appVersion) {
    std::error_code ignored;
    if (verdict.allowed) {
        std::filesystem::remove(file, ignored);
        return false;
    }
    std::string key = OffNoticeKey(verdict, appVersion);
    if (key.empty()) return false;

    std::string recorded;
    if (std::ifstream in{file}) std::getline(in, recorded);
    while (!recorded.empty() && (recorded.back() == '\r' || recorded.back() == ' ')) recorded.pop_back();
    if (recorded == key) return false;

    std::filesystem::create_directories(file.parent_path(), ignored);
    std::ofstream out(file, std::ios::trunc);
    out << key << "\n";
    return true;
}

}  // namespace ampify
