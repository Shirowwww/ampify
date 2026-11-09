#include "pane_corners.hpp"

#if defined(AMPIFY_WITH_XAML)

#include <array>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

#include "../core/theme/pane.hpp"

namespace ampify::win {

namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = winrt::Microsoft::UI::Xaml::Controls;
namespace media = winrt::Microsoft::UI::Xaml::Media;
namespace comp = winrt::Microsoft::UI::Composition;
using winrt::Windows::Foundation::Numerics::float2;

namespace {

constexpr wchar_t kPaneName[] = L"PaneRoot";

// The SplitView whose template holds `pane` as its pane: the pane is a child of the template's root grid.
controls::SplitView OwnerOf(const controls::Grid& pane) {
    if (pane.Name() != kPaneName) return nullptr;
    xaml::DependencyObject root = media::VisualTreeHelper::GetParent(pane);
    return root ? media::VisualTreeHelper::GetParent(root).try_as<controls::SplitView>() : nullptr;
}

// The template's `Border Child="{TemplateBinding Pane}"`, the pane's first Border.
controls::Border HostOf(const controls::Grid& pane) {
    const int count = media::VisualTreeHelper::GetChildrenCount(pane);
    for (int i = 0; i < count; i++) {
        if (auto border = media::VisualTreeHelper::GetChild(pane, i).try_as<controls::Border>()) return border;
    }
    return nullptr;
}

bool Compact(const controls::SplitView& split) {
    const controls::SplitViewDisplayMode mode = split.DisplayMode();
    return mode == controls::SplitViewDisplayMode::CompactInline || mode == controls::SplitViewDisplayMode::CompactOverlay;
}

float2 Ellipse(const theme::CornerEllipse& corner) {
    return {static_cast<float>(corner.x), static_cast<float>(corner.y)};
}

}  // namespace

PaneCorners::~PaneCorners() {
    for (auto& [id, entry] : m_entries) Drop(entry);
}

void PaneCorners::Follow(theme::NodeId id, const xaml::DependencyObject& object) noexcept {
    try {
        auto pane = object.try_as<controls::Grid>();
        if (!pane || !OwnerOf(pane)) return;
        auto known = m_entries.find(id);
        if (known != m_entries.end() && known->second.pane.get() != pane) {  // a new pane at a gone one's address
            Drop(known->second);
            m_entries.erase(known);
            known = m_entries.end();
        }
        if (known == m_entries.end() && !Start(id, pane)) return;
        Update(m_entries.at(id));
    } catch (...) {
        // Nothing may escape into Apple Music's XAML frame.
    }
}

// The host's hand-out visual takes the clip: Apple never animates the template's Border, and the pane itself keeps
// the template's rectangle clip, which slides it open.
bool PaneCorners::Start(theme::NodeId id, const controls::Grid& pane) {
    controls::SplitView split = OwnerOf(pane);
    controls::Border host = HostOf(pane);
    if (!split || !host) return false;
    Entry entry;
    entry.visual = xaml::Hosting::ElementCompositionPreview::GetElementVisual(host);
    entry.before = entry.visual.Clip();
    entry.clip = entry.visual.Compositor().CreateRectangleClip();
    entry.pane = winrt::make_weak(pane);
    entry.split = winrt::make_weak(split);
    entry.host = winrt::make_weak(host);
    std::weak_ptr<bool> alive = m_alive;
    entry.sized = host.SizeChanged(winrt::auto_revoke, [this, alive, id](auto const&, auto const&) noexcept {
        if (!alive.expired()) OnChange(id);
    });
    entry.modeToken = split.RegisterPropertyChangedCallback(
        controls::SplitView::DisplayModeProperty(), [this, alive, id](auto const&, auto const&) noexcept {
            if (!alive.expired()) OnChange(id);
        });
    m_entries.emplace(id, std::move(entry));
    return true;
}

void PaneCorners::Update(Entry& entry) {
    controls::Grid pane = entry.pane.get();
    controls::SplitView split = entry.split.get();
    controls::Border host = entry.host.get();
    if (!pane || !split || !host) return;
    const xaml::CornerRadius radius = pane.CornerRadius();
    const xaml::Thickness border = pane.BorderThickness();
    const std::array<theme::CornerEllipse, 4> corners = theme::InnerCorners(
        {radius.TopLeft, radius.TopRight, radius.BottomRight, radius.BottomLeft},
        {border.Left, border.Top, border.Right, border.Bottom});
    if (Compact(split) || !theme::Rounded(corners)) {
        if (entry.visual.Clip() == entry.clip) entry.visual.Clip(entry.before);
        return;
    }
    entry.clip.Left(0);
    entry.clip.Top(0);
    entry.clip.Right(static_cast<float>(host.ActualWidth()));
    entry.clip.Bottom(static_cast<float>(host.ActualHeight()));
    entry.clip.TopLeftRadius(Ellipse(corners[0]));
    entry.clip.TopRightRadius(Ellipse(corners[1]));
    entry.clip.BottomRightRadius(Ellipse(corners[2]));
    entry.clip.BottomLeftRadius(Ellipse(corners[3]));
    if (entry.visual.Clip() != entry.clip) entry.visual.Clip(entry.clip);
}

void PaneCorners::OnChange(theme::NodeId id) noexcept {
    try {
        if (auto it = m_entries.find(id); it != m_entries.end()) Update(it->second);
    } catch (...) {
        // Nothing may escape into Apple Music's XAML frame.
    }
}

void PaneCorners::Drop(Entry& entry) noexcept {
    try {
        entry.sized.revoke();
        if (controls::SplitView split = entry.split.get()) {
            split.UnregisterPropertyChangedCallback(controls::SplitView::DisplayModeProperty(), entry.modeToken);
        }
        if (entry.visual && entry.visual.Clip() == entry.clip) entry.visual.Clip(entry.before);
    } catch (...) {
        // The element may be torn down already: nothing is left to put back.
    }
}

void PaneCorners::Sweep() noexcept {
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        if (it->second.host.get()) {
            ++it;
            continue;
        }
        Drop(it->second);
        it = m_entries.erase(it);
    }
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
