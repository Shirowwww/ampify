// ::before and ::after: the images and bars a theme puts into a panel.

#if defined(AMPIFY_WITH_XAML)

#include "xaml_backend.hpp"

#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <set>

#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
#include <winrt/Microsoft.UI.Xaml.Navigation.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.Foundation.Numerics.h>

#include "../core/color_extract.hpp"
#include "../core/theme/animation.hpp"
#include "../core/theme/colour_keys.hpp"
#include "../core/theme/properties.hpp"
#include "../core/theme/selector.hpp"
#include "../core/text_util.hpp"
#include "../core/ui_render/render_tree.hpp"
#include "sidebar_hover.hpp"
#include "xaml_host.hpp"
#include "xaml_backend_detail.hpp"

namespace ampify::win {

using namespace xaml_detail;

namespace {

// A decor whose animation turns it: a rotation track in its `animation`.
bool DecorTurns(const theme::Decor& decor) {
    for (const auto& [property, value] : decor.properties) {
        if (property != "Animation") continue;
        std::optional<std::vector<theme::Animation>> animations = theme::DecodeAnimations(value.text);
        if (!animations) continue;
        for (const theme::Animation& animation : *animations) {
            for (const theme::AnimationTrack& track : animation.tracks) {
                if (track.channel == theme::AnimationTrack::Channel::Rotation) return true;
            }
        }
    }
    return false;
}

// A turning artwork is a square on its panel's diagonal, centred on it: drawn the panel's size, its corners showed as
// it turned (09/11), whatever its scale. Negative margins give it that room without the panel clipping it, and follow
// the panel's size: the margin depends on the panel only, so setting it settles at once.
void KeepSquareOnDiagonal(const xaml::FrameworkElement& decor) {
    decor.SizeChanged([](IInspectable const& sender, xaml::SizeChangedEventArgs const&) noexcept {
        try {
            auto element = sender.as<xaml::FrameworkElement>();
            auto panel = media::VisualTreeHelper::GetParent(element).try_as<xaml::FrameworkElement>();
            if (!panel || panel.ActualWidth() <= 0 || panel.ActualHeight() <= 0) return;
            const double side = std::ceil(std::hypot(panel.ActualWidth(), panel.ActualHeight()));
            const double x = -(side - panel.ActualWidth()) / 2;
            const double y = -(side - panel.ActualHeight()) / 2;
            const xaml::Thickness margin{x, y, x, y};
            if (element.Margin() != margin) element.Margin(margin);
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });
}

// The same picture and the same declarations, only their values changed (not the animation): updated in place, the
// element keeps its transition. Bars count as the same picture across a width (thin, thick) alone.
bool SameDecorPicture(const theme::Decor& before, const theme::Decor& after) {
    std::optional<theme::BarsContent> barsBefore = theme::ReadBarsContent(before.image);
    std::optional<theme::BarsContent> barsAfter = theme::ReadBarsContent(after.image);
    if (barsBefore && barsAfter) {
        barsAfter->thick = barsBefore->thick;
        if (*barsBefore != *barsAfter) return false;
    } else if (before.image != after.image) {
        return false;
    }
    if (before.properties.size() != after.properties.size()) return false;
    for (size_t i = 0; i < before.properties.size(); i++) {
        const auto& [name, value] = before.properties[i];
        if (name != after.properties[i].first) return false;
        if (name == "Animation" && value != after.properties[i].second) return false;
    }
    return true;
}

}  // namespace

// A panel that refuses a child (a sealed collection, a decode the app refuses) costs only its own image.
theme::DecorChange XamlTreeBackend::SetDecor(theme::NodeId id, theme::PseudoElement slot, const theme::Decor* decor) {
    if (*m_closed) return theme::DecorChange::Refused;
    try {
        return decor ? PlaceDecor(id, slot, *decor) : RemoveDecor(id, slot);
    } catch (...) {
        return theme::DecorChange::Refused;
    }
}

// A changed decor gets a new Image where the old one stood (Children.SetAt): no value of the old one (a width the
// rule no longer gives, a running animation) is left behind, and the panel never holds two.
theme::DecorChange XamlTreeBackend::PlaceDecor(theme::NodeId id, theme::PseudoElement slot, const theme::Decor& decor) {
    controls::Panel panel{nullptr};
    if (xaml::DependencyObject object = Resolve(id)) panel = object.try_as<controls::Panel>();
    if (!TakesDecor(panel)) return theme::DecorChange::Refused;
    controls::UIElementCollection children = panel.Children();
    auto key = std::make_pair(id, slot);
    auto known = m_decors.find(key);
    const bool same = known != m_decors.end() && known->second.decor == decor;
    if (same && KeepDecorInPlace(children, known->second.image, slot)) return theme::DecorChange::Unchanged;
    // The same picture with other values (a setting switched: the mascot's size, the bars' width): the element stays,
    // so its transition animates the change, and the bars ease to their new width.
    if (known != m_decors.end() && !same && SameDecorPicture(known->second.decor, decor) &&
        KeepDecorInPlace(children, known->second.image, slot)) {
        std::optional<theme::BarsContent> bars = theme::ReadBarsContent(decor.image);
        if (bars && *bars != theme::ReadBarsContent(known->second.decor.image)) {
            m_bars.SetThick(known->second.image, bars->thick);
        }
        known->second.decor = decor;
        WriteDecor(known->second.imageId, panel, decor);
        return theme::DecorChange::Applied;
    }
    xaml::FrameworkElement old{nullptr};  // held until it leaves the panel
    if (known != m_decors.end()) {
        old = known->second.image;
        m_animations.Forget(known->second.imageId);
        ForgetDecorMotion(known->second.imageId);
    }
    xaml::FrameworkElement image{nullptr};
    const wchar_t* name = slot == theme::PseudoElement::Before ? L"AmpifyDecorBefore" : L"AmpifyDecorAfter";
    if (std::optional<theme::BarsContent> bars = theme::ReadBarsContent(decor.image)) {
        image = m_bars.Make(*bars, name);
    } else if (decor.image.starts_with(theme::kArtworkImage)) {
        // The song's artwork, painted by a brush FollowArtwork keeps up to date: a Border, so it moves and turns.
        image = ArtworkDecor(decor.image, name);
        if (DecorTurns(decor)) KeepSquareOnDiagonal(image);
    } else if (decor.image.starts_with(theme::kImmersionContent)) {
        image = m_immersion.Make(name);
        if (!image) return theme::DecorChange::Refused;  // no compositor to draw it with
    } else {
        image = DecorImageFrom(decor.image, slot);
    }
    // Decorative only: a click goes through to what lies under it. Spinning artwork's record over the play bar's
    // artwork took the click that opens Apple's mini player.
    image.IsHitTestVisible(false);
    auto imageId = reinterpret_cast<std::uint64_t>(winrt::get_abi(image.as<xaml::DependencyObject>()));
    m_decors[key] = DecorImage{winrt::make_weak(panel), image, imageId, decor};
    WriteDecor(imageId, panel, decor);
    std::uint32_t index = 0;
    if (old && children.IndexOf(old, index)) {
        children.SetAt(index, image);
    } else if (slot == theme::PseudoElement::Before) {
        children.InsertAt(0, image);
    } else {
        children.Append(image);
    }
    m_bars.Drop(old);
    // In the tree first: the compositor animates an element that has its visual.
    for (const auto& [property, value] : decor.properties) {
        if (property == "Animation") WriteRaw(imageId, property, value);
    }
    return theme::DecorChange::Applied;
}

// Size, margin, alignment, cell, opacity, transform: the values AppliesToDecor lets through, written as on any element.
// Over the whole Grid unless the theme names a row or a column, so the image lies over the panel.
void XamlTreeBackend::WriteDecor(theme::NodeId imageId, const controls::Panel& panel, const theme::Decor& decor) {
    bool row = false;
    bool column = false;
    // The transition first: the transform written after it eases (WriteTransform reads it).
    for (const auto& [property, value] : decor.properties) {
        if (property == "Transition") WriteRaw(imageId, property, value);
    }
    for (const auto& [property, value] : decor.properties) {
        row = row || property == "Grid.Row" || property == "Grid.RowSpan";
        column = column || property == "Grid.Column" || property == "Grid.ColumnSpan";
        if (property != "Animation" && property != "Foreground" && property != "Transition") {
            WriteRaw(imageId, property, value);
        }
    }
    xaml::FrameworkElement element{nullptr};
    if (xaml::DependencyObject object = Resolve(imageId)) element = object.try_as<xaml::FrameworkElement>();
    // `color` paints the bars of bars(n); an image has no colour of its own.
    auto bars = decor.image.starts_with(theme::kBarsContent) && element ? element.try_as<controls::Grid>() : nullptr;
    for (const auto& [property, value] : decor.properties) {
        if (property != "Foreground" || !bars) continue;
        media::Brush brush{nullptr};
        if (value.kind == theme::ValueKind::Color) {
            if (std::optional<ColorValue> color = ColorFromText(value.text)) brush = media::SolidColorBrush(*color);
        } else if (value.kind == theme::ValueKind::Gradient) {
            brush = GradientFrom(value.text);
        }
        if (brush) m_bars.SetBrush(bars, brush);
    }
    auto grid = panel.try_as<controls::Grid>();
    if (!grid || !element) return;
    auto span = [](std::uint32_t count) { return std::max<std::int32_t>(1, static_cast<std::int32_t>(count)); };
    if (!row) controls::Grid::SetRowSpan(element, span(grid.RowDefinitions().Size()));
    if (!column) controls::Grid::SetColumnSpan(element, span(grid.ColumnDefinitions().Size()));
}

// A decor's transition goes with it: a new element at the same address must not ease a transform it never asked to.
void XamlTreeBackend::ForgetDecorMotion(theme::NodeId imageId) {
    m_transitions.erase(imageId);
    m_transitionOriginals.erase(imageId);
    m_storyDurations.erase(imageId);
}

theme::DecorChange XamlTreeBackend::RemoveDecor(theme::NodeId id, theme::PseudoElement slot) {
    auto known = m_decors.find(std::make_pair(id, slot));
    if (known == m_decors.end()) return theme::DecorChange::Unchanged;
    DecorImage held = std::move(known->second);  // released at the end of this call, on the interface thread
    m_decors.erase(known);
    m_bars.Drop(held.image);
    m_animations.Forget(held.imageId);
    ForgetDecorMotion(held.imageId);
    controls::Panel panel = held.panel.get();
    if (!panel) return theme::DecorChange::Refused;
    controls::UIElementCollection children = panel.Children();
    std::uint32_t index = 0;
    if (children.IndexOf(held.image, index)) children.RemoveAt(index);
    return theme::DecorChange::Applied;
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
