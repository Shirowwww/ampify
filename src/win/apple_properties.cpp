// Written against the C++/WinRT projection of Windows App SDK 1.8. Apple's interfaces are called by vtable slot, with
// the identifiers and slots of src/core/amp_abi.hpp (checked against knowledge/winmd/AMP.UI.Controls.txt by a test).

#if defined(AMPIFY_WITH_XAML)

#include "apple_properties.hpp"

#include <winrt/Windows.Foundation.h>

#include <unknwn.h>

#include <cmath>
#include <iterator>
#include <string>

#include "../core/amp_abi.hpp"
#include "../core/theme/properties.hpp"
#include "winrt_abi.hpp"

namespace ampify::win {

namespace {

namespace xaml = winrt::Microsoft::UI::Xaml;
namespace amp = ampify::amp;

enum class AppleKind { Length, Visibility };

struct AppleSpec {
    std::string_view property;  // the XAML name the theme engine uses (properties.cpp)
    const char* className;
    const char* instanceIid;     // tells an element of that class
    const char* staticsIid;
    int slot;                    // of the getter of the dependency property, in the statics
    AppleKind kind;
};

constexpr AppleSpec kSpecs[] = {
    {"TrackHeight", amp::kClassScrubberControl, amp::kIidScrubberControl, amp::kIidScrubberControlStatics,
     amp::slot::kScrubberTrackHeightProperty, AppleKind::Length},
    {"TrackRectRadius", amp::kClassScrubberControl, amp::kIidScrubberControl, amp::kIidScrubberControlStatics,
     amp::slot::kScrubberTrackRectRadiusProperty, AppleKind::Length},
    {"ThumbVisibility", amp::kClassScrubberControl, amp::kIidScrubberControl, amp::kIidScrubberControlStatics,
     amp::slot::kScrubberThumbVisibilityProperty, AppleKind::Visibility},
};

const AppleSpec* FindSpec(std::string_view property) {
    for (const AppleSpec& spec : kSpecs) {
        if (spec.property == property) return &spec;
    }
    return nullptr;
}

bool IsOfClass(const xaml::DependencyObject& object, const AppleSpec& spec) {
    winrt::com_ptr<::IUnknown> instance;
    auto* unknown = static_cast<::IUnknown*>(winrt::get_abi(object));
    return unknown && SUCCEEDED(unknown->QueryInterface(rt::GuidOf(spec.instanceIid), instance.put_void()));
}

// Asked once per property: a class Apple Music lacks (another version) is not asked again. Interface thread only;
// never destroyed (xaml-lessons.md: no XAML object in a static destructor).
xaml::DependencyProperty Fetch(const AppleSpec& spec) {
    winrt::com_ptr<::IUnknown> statics;
    auto factory = winrt::get_activation_factory<winrt::Windows::Foundation::IActivationFactory>(
        winrt::to_hstring(spec.className));
    auto* unknown = static_cast<::IUnknown*>(winrt::get_abi(factory));
    if (FAILED(unknown->QueryInterface(rt::GuidOf(spec.staticsIid), statics.put_void()))) return nullptr;
    void* property = nullptr;
    if (FAILED(rt::CallSlot(statics.get(), spec.slot, &property)) || !property) return nullptr;
    xaml::DependencyProperty result{nullptr};
    winrt::attach_abi(result, property);
    return result;
}

xaml::DependencyProperty Cached(const AppleSpec& spec) {
    struct Entry {
        bool asked = false;
        xaml::DependencyProperty property{nullptr};
    };
    static auto* entries = new Entry[std::size(kSpecs)];
    Entry& entry = entries[&spec - kSpecs];
    if (!entry.asked) {
        entry.asked = true;
        try {
            entry.property = Fetch(spec);
        } catch (...) {
            // A class Apple Music lacks (another version): the property stays empty and is not asked again.
        }
    }
    return entry.property;
}

}  // namespace

bool IsAppleProperty(std::string_view property) noexcept { return FindSpec(property) != nullptr; }

xaml::DependencyProperty AppleProperty(const xaml::DependencyObject& object, std::string_view property) noexcept {
    const AppleSpec* spec = FindSpec(property);
    if (!spec || !object || !IsOfClass(object, *spec)) return nullptr;
    return Cached(*spec);
}

std::optional<theme::Value> ReadAppleProperty(const xaml::DependencyObject& object, std::string_view property) noexcept {
    try {
        xaml::DependencyProperty dp = AppleProperty(object, property);
        if (!dp) return std::nullopt;
        auto value = object.GetValue(dp);
        if (FindSpec(property)->kind == AppleKind::Visibility) {
            bool shown = winrt::unbox_value_or<xaml::Visibility>(value, xaml::Visibility::Visible) == xaml::Visibility::Visible;
            return theme::Value{theme::ValueKind::Visibility, shown ? "Visible" : "Collapsed"};
        }
        return theme::Value{theme::ValueKind::Length, theme::FormatNumber(winrt::unbox_value_or<double>(value, NAN))};
    } catch (...) {
        return std::nullopt;
    }
}

bool WriteAppleProperty(const xaml::DependencyObject& object, std::string_view property, const theme::Value& value) noexcept {
    try {
        xaml::DependencyProperty dp = AppleProperty(object, property);
        if (!dp) return false;
        if (FindSpec(property)->kind == AppleKind::Visibility) {
            if (value.kind != theme::ValueKind::Visibility) return false;
            bool shown = value.text == "Visible";
            object.SetValue(dp, winrt::box_value(shown ? xaml::Visibility::Visible : xaml::Visibility::Collapsed));
            return true;
        }
        if (value.kind != theme::ValueKind::Length) return false;
        double length = std::stod(value.text);
        // Apple's template sizes a Rectangle with it: an automatic or negative size has no meaning there.
        if (!std::isfinite(length) || length < 0) return false;
        object.SetValue(dp, winrt::box_value(length));
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
