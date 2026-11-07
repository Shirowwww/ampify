#pragma once
// A SplitView's pane that a theme rounds (the sidebar, the lyrics and queue panel of floating-panels) keeps its corners
// round whatever it holds. WinUI rounds a Grid's background and border, not its children: the lyrics view painted a
// square patch over the pane's rounded border at its corners. The pane's content host, the template's Border, is
// clipped to the corners inside the pane's border (theme::InnerCorners). A compact pane shows only its column of icons,
// which themes round themselves: it is left alone.

#if defined(AMPIFY_WITH_XAML)

#include <cstdint>
#include <map>
#include <memory>

#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>

#include "../core/theme/tree.hpp"

namespace ampify::win {

class PaneCorners {
public:
    PaneCorners() = default;
    PaneCorners(const PaneCorners&) = delete;
    PaneCorners& operator=(const PaneCorners&) = delete;
    ~PaneCorners();

    // After a theme writes or gives back the corners or the border of `object`: follows it when it is a SplitView's
    // Grid#PaneRoot, and clips its content again (or no more, once its corners are square). Interface thread only.
    void Follow(theme::NodeId id, const winrt::Microsoft::UI::Xaml::DependencyObject& object) noexcept;

    // Forgets the panes that are gone. Interface thread only.
    void Sweep() noexcept;

private:
    struct Entry {
        winrt::weak_ref<winrt::Microsoft::UI::Xaml::Controls::Grid> pane;
        winrt::weak_ref<winrt::Microsoft::UI::Xaml::Controls::SplitView> split;
        winrt::weak_ref<winrt::Microsoft::UI::Xaml::Controls::Border> host;
        winrt::Microsoft::UI::Composition::Visual visual{nullptr};
        winrt::Microsoft::UI::Composition::CompositionClip before{nullptr};  // the host's clip before Ampify's
        winrt::Microsoft::UI::Composition::RectangleClip clip{nullptr};
        winrt::Microsoft::UI::Xaml::FrameworkElement::SizeChanged_revoker sized;
        std::int64_t modeToken = 0;
    };

    bool Start(theme::NodeId id, const winrt::Microsoft::UI::Xaml::Controls::Grid& pane);
    void Update(Entry& entry);
    void Drop(Entry& entry) noexcept;
    void OnChange(theme::NodeId id) noexcept;

    std::map<theme::NodeId, Entry> m_entries;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);  // the handlers do nothing once this is gone
};

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
