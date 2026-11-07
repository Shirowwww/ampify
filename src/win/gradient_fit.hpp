#pragma once
// A theme's linear-gradient keeps its CSS angle on the element it paints, whatever the element's shape. Its points are
// relative to the element and written for a square; stretched over a tall narrow column, 135deg put the column's top
// right and bottom left corners at the middle of the line, where Glass's rim is dark, and its outline looked
// unfinished there. The points follow the element's size (theme::FitGradient).

#if defined(AMPIFY_WITH_XAML)

#include <map>
#include <memory>
#include <string>
#include <utility>

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

#include "../core/theme/properties.hpp"
#include "../core/theme/tree.hpp"

namespace ampify::win {

class GradientFit {
public:
    GradientFit() = default;
    GradientFit(const GradientFit&) = delete;
    GradientFit& operator=(const GradientFit&) = delete;
    ~GradientFit();

    // After `brush` went into `property` of `object`: a LinearGradientBrush of the theme (`made`) is fitted to the
    // element now and at each of its size changes; anything else ends what the property followed. Interface thread.
    void Follow(theme::NodeId id, const std::string& property, const winrt::Microsoft::UI::Xaml::DependencyObject& object,
                const winrt::Microsoft::UI::Xaml::Media::Brush& brush, bool made) noexcept;

    // Forgets the elements that are gone. Interface thread only.
    void Sweep() noexcept;

private:
    struct Entry {
        winrt::weak_ref<winrt::Microsoft::UI::Xaml::FrameworkElement> element;  // Apple's: a weak reference resolves
        winrt::Microsoft::UI::Xaml::Media::LinearGradientBrush brush{nullptr};  // Ampify's: held, released here
        theme::GradientLine square;
        winrt::Microsoft::UI::Xaml::FrameworkElement::SizeChanged_revoker sized;
    };
    using Key = std::pair<theme::NodeId, std::string>;

    static void Fit(const Entry& entry, double width, double height);

    std::map<Key, Entry> m_entries;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);  // the handlers do nothing once this is gone
};

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
