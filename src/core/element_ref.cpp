#include "element_ref.hpp"

#include <algorithm>

#include "kit_layout.hpp"
#include "settings.hpp"
#include "text_util.hpp"

namespace ampify {

const ElementKindTraits& TraitsOf(ElementKind kind) {
    for (const ElementKindTraits& traits : kElementKinds) {
        if (traits.kind == kind) return traits;
    }
    return kElementKinds.front();
}

std::string_view ToString(ElementKind kind) { return TraitsOf(kind).name; }

std::optional<ElementKind> KindNamed(std::string_view name) {
    for (const ElementKindTraits& traits : kElementKinds) {
        if (traits.name == name) return traits.kind;
    }
    return std::nullopt;
}

std::optional<ElementKind> KindOfFolder(std::string_view folder) {
    for (const ElementKindTraits& traits : kElementKinds) {
        if (traits.folder == folder) return traits.kind;
    }
    return std::nullopt;
}

bool IsElementId(std::string_view id) {
    constexpr size_t kMinId = 2;
    constexpr size_t kMaxId = 40;
    const auto lowerOrDigit = [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); };
    if (id.size() < kMinId || id.size() > kMaxId || !lowerOrDigit(id.front())) return false;
    return std::all_of(id.begin(), id.end(), [&](char c) { return lowerOrDigit(c) || c == '-'; });
}

bool IsDeclarableSettingKey(std::string_view key) { return key != kEnabledKey && IsSettingKey(key); }

std::string ElementRef::HostId() const {
    return kind == ElementKind::App ? std::string(kAppHostPrefix) + id : id;
}

std::string ElementRef::SettingsPrefix() const { return std::string(Folder()) + "." + id + "."; }

std::string ElementRef::EnabledKey() const { return SettingsPrefix() + std::string(kEnabledKey); }

bool IsElementOn(const Settings& settings, const ElementRef& element) {
    if (element.kind == ElementKind::Theme) return settings.GetString("theme.active", "") == element.id;
    return settings.GetBool(element.EnabledKey(), IsOnByDefault(element.Folder(), element.id));
}

}  // namespace ampify
