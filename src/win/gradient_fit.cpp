#include "gradient_fit.hpp"

#if defined(AMPIFY_WITH_XAML)

#include <winrt/Windows.Foundation.h>

namespace ampify::win {

namespace xaml = winrt::Microsoft::UI::Xaml;
namespace media = winrt::Microsoft::UI::Xaml::Media;
using winrt::Windows::Foundation::Point;

GradientFit::~GradientFit() {
    for (auto& [key, entry] : m_entries) entry.sized.revoke();
}

void GradientFit::Fit(const Entry& entry, double width, double height) {
    const theme::GradientLine line = theme::FitGradient(entry.square, width, height);
    entry.brush.StartPoint(Point{static_cast<float>(line.x1), static_cast<float>(line.y1)});
    entry.brush.EndPoint(Point{static_cast<float>(line.x2), static_cast<float>(line.y2)});
}

void GradientFit::Follow(theme::NodeId id, const std::string& property, const xaml::DependencyObject& object,
                         const media::Brush& brush, bool made) noexcept {
    try {
        Key key{id, property};
        if (auto known = m_entries.find(key); known != m_entries.end()) {
            known->second.sized.revoke();
            m_entries.erase(known);
        }
        auto gradient = made ? brush.try_as<media::LinearGradientBrush>() : nullptr;
        auto element = object.try_as<xaml::FrameworkElement>();
        if (!gradient || !element || gradient.MappingMode() != media::BrushMappingMode::RelativeToBoundingBox) return;

        Entry entry;
        entry.element = winrt::make_weak(element);
        entry.brush = gradient;
        const Point start = gradient.StartPoint(), end = gradient.EndPoint();
        entry.square = {start.X, start.Y, end.X, end.Y};
        std::weak_ptr<bool> alive = m_alive;
        entry.sized = element.SizeChanged(winrt::auto_revoke, [this, alive, key](auto const&, xaml::SizeChangedEventArgs const& args) noexcept {
            try {
                if (alive.expired()) return;
                auto it = m_entries.find(key);
                if (it != m_entries.end()) Fit(it->second, args.NewSize().Width, args.NewSize().Height);
            } catch (...) {
                // A handler runs inside Apple Music's XAML frame: nothing may escape it.
            }
        });
        Fit(entry, element.ActualWidth(), element.ActualHeight());
        m_entries.emplace(std::move(key), std::move(entry));
    } catch (...) {
        // Nothing may escape into Apple Music's XAML frame.
    }
}

void GradientFit::Sweep() noexcept {
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        if (it->second.element.get()) {
            ++it;
            continue;
        }
        it->second.sized.revoke();
        it = m_entries.erase(it);
    }
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
