// XamlTreeBackend: writing one property of one element (WriteRaw and its writers). Reading is in xaml_read.cpp, the
// transitions and animated transforms in xaml_motion.cpp, the rest of the class in xaml_backend.cpp.

#if defined(AMPIFY_WITH_XAML)

#include "xaml_backend.hpp"

#include "artwork_passthrough.hpp"

#include "xaml_properties_detail.hpp"

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

#include <winrt/Microsoft.UI.Xaml.Documents.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
#include <winrt/Microsoft.UI.Xaml.Navigation.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.Foundation.Numerics.h>

#include "../core/theme/colour_keys.hpp"
#include "../core/theme/properties.hpp"
#include "../core/text_util.hpp"
#include "../core/ui_render/render_tree.hpp"
#include "apple_properties.hpp"
#include "artwork_cap.hpp"
#include "xaml_host.hpp"
#include "xaml_backend_detail.hpp"

namespace ampify::win {

using namespace xaml_detail;

namespace {

// A link inside a TextBlock (the album artist) and its runs keep their own colour over the TextBlock's, so the text's
// colour goes to every inline too, and the colour read back is the last run's: Apple fills the link after the
// theme's first pass, and a mismatch is what makes the next pass write again. shortcut: a cleared colour leaves the
// inlines on their default, not on the colour Apple set in code; Apple sets it again when the page is built.
void CollectInlines(const winrt::Windows::Foundation::Collections::IVector<xaml::Documents::Inline>& inlines,
                    std::vector<xaml::Documents::Inline>& all) {
    for (const auto& inline_ : inlines) {
        all.push_back(inline_);
        if (auto span = inline_.try_as<xaml::Documents::Span>()) CollectInlines(span.Inlines(), all);
    }
}

// Only a text holding a link: plain runs follow the TextBlock's colour already.
void PaintLinks(const controls::TextBlock& text, const media::Brush* brush) {
    std::vector<xaml::Documents::Inline> inlines = InlinesOf(text);
    if (!HasLink(inlines)) return;
    for (const auto& inline_ : inlines) {
        if (brush) inline_.Foreground(*brush);
        else inline_.ClearValue(xaml::Documents::TextElement::ForegroundProperty());
    }
}

}  // namespace

namespace xaml_detail {

theme::Value NumberValue(double value) {
    return theme::Value{theme::ValueKind::Number, th::FormatNumber(value)};
}

std::vector<xaml::Documents::Inline> InlinesOf(const controls::TextBlock& text) {
    std::vector<xaml::Documents::Inline> all;
    CollectInlines(text.Inlines(), all);
    return all;
}

bool HasLink(const std::vector<xaml::Documents::Inline>& inlines) {
    return std::any_of(inlines.begin(), inlines.end(),
                       [](const auto& inline_) { return inline_.try_as<xaml::Documents::Hyperlink>() != nullptr; });
}

winrt::Windows::Foundation::TimeSpan Milliseconds(double ms) {
    return std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(std::chrono::duration<double, std::milli>(ms));
}

// A storyboard easing the transform's scale, rotation and translation from `from` to `to`, empty when nothing moves.
animation::Storyboard EaseTransform(const media::CompositeTransform& transform, const double (&from)[5],
                                    const std::vector<double>& to, double ms) {
    animation::Storyboard story;
    animation::CubicEase ease;
    ease.EasingMode(animation::EasingMode::EaseOut);
    auto duration = xaml::DurationHelper::FromTimeSpan(Milliseconds(ms));
    constexpr const wchar_t* kParts[5] = {L"ScaleX", L"ScaleY", L"Rotation", L"TranslateX", L"TranslateY"};
    for (int i = 0; i < 5; i++) {
        if (from[i] == to[i]) continue;
        animation::DoubleAnimation part;
        part.From(from[i]);
        part.To(to[i]);
        part.Duration(duration);
        part.EasingFunction(ease);
        part.FillBehavior(animation::FillBehavior::Stop);  // the local value, already the target, shows after
        animation::Storyboard::SetTarget(part, transform);
        animation::Storyboard::SetTargetProperty(part, kParts[i]);
        story.Children().Append(part);
    }
    return story;
}

}  // namespace xaml_detail

// The entry of this very control, or none: one left by a gone control under the same id is dropped (its originals
// belonged to that control's resources).
XamlTreeBackend::CheckedKeys* XamlTreeBackend::LiveChecked(theme::NodeId id, const controls::Control& control) {
    auto known = m_checked.find(id);
    if (known == m_checked.end()) return nullptr;
    if (known->second.control.get() == control) return &known->second;
    m_checked.erase(known);
    return nullptr;
}

// Apple Music gives its shuffle button its own ToggleButtonForegroundChecked keys (TransportControl.xbf), so the icon
// stayed Apple's blue while on, whatever the theme's accent. The control's own keys take the theme's colour; the theme
// gone, they get back what they held.
bool XamlTreeBackend::WriteCheckedForeground(theme::NodeId id, const xaml::DependencyObject& object,
                                             const theme::Value& value) {
    static const std::array<std::wstring, 3> kKeys = {L"ToggleButtonForegroundChecked",
                                                      L"ToggleButtonForegroundCheckedPointerOver",
                                                      L"ToggleButtonForegroundCheckedPressed"};
    auto control = object.try_as<controls::Control>();
    if (!control) return false;
    xaml::ResourceDictionary resources = control.Resources();
    LiveChecked(id, control);
    auto known = m_checked.find(id);
    if (value == theme::kNoOwnValue) {
        if (known == m_checked.end()) return true;
        for (const auto& [key, original] : known->second.originals) {
            IInspectable name = winrt::box_value(winrt::hstring(key));
            if (original) resources.Insert(name, original);
            else if (resources.HasKey(name)) resources.Remove(name);
        }
        m_checked.erase(known);
        ReadThemeAgain(control);
        return true;
    }
    if (value.kind != theme::ValueKind::Color) return false;
    std::optional<ColorValue> color = ColorFromText(value.text);
    if (!color) return false;
    if (known != m_checked.end() && known->second.colour == value.text) return true;
    if (known == m_checked.end()) {
        CheckedKeys saved;
        saved.control = control;
        for (const std::wstring& key : kKeys) {
            IInspectable name = winrt::box_value(winrt::hstring(key));
            saved.originals.emplace_back(key, resources.HasKey(name) ? resources.Lookup(name) : nullptr);
        }
        known = m_checked.emplace(id, std::move(saved)).first;
    }
    for (const std::wstring& key : kKeys) {
        resources.Insert(winrt::box_value(winrt::hstring(key)), media::SolidColorBrush(*color));
    }
    known->second.colour = value.text;
    ReadThemeAgain(control);
    return true;
}

// Background, Foreground, BorderBrush and the selection brushes: a colour, a gradient, an image or acrylic.
std::optional<bool> XamlTreeBackend::WriteBrush(theme::NodeId id, const xaml::DependencyObject& object, std::string_view property, const theme::Value& value) {
    if (property == "Background" || property == "Foreground" || property == "BorderBrush" || IsSelectionBrush(property)) {
        std::optional<media::Brush> target;
        bool made = false;  // an object made from the value, remembered so that it reads back as the value
        if (value.kind == theme::ValueKind::Color) {
            std::optional<ColorValue> color = ColorFromText(value.text);
            if (!color) return false;
            target = media::SolidColorBrush(*color);
            made = true;
        } else if (value.kind == theme::ValueKind::Image) {
            target = value.text.starts_with(theme::kArtworkImage) ? ArtworkBrush(value.text) : ImageFrom(value.text);
            made = true;
        } else if (value.kind == theme::ValueKind::Gradient) {
            media::Brush gradient = GradientFrom(value.text);
            if (!gradient) return false;
            target = gradient;
            made = true;
        } else if (value.kind == theme::ValueKind::Acrylic) {
            media::Brush acrylic = AcrylicFrom(value.text);
            if (!acrylic) return false;
            target = acrylic;
            made = true;
        } else if (value.kind == theme::ValueKind::Opaque) {
            target = Restore(value).try_as<media::Brush>();  // null too: an element that had no brush gets none again
        }
        if (!target) return false;

        bool written = false;
        if (IsSelectionBrush(property)) {
            auto row = object.try_as<primitives::ListViewItemPresenter>();
            if (!row) return false;
            SetSelectionBrush(row, property, *target);
            if (made) {
                Remember(id, property, value, *target);
            } else {
                Forget(id, property);
            }
            return true;  // the row draws these itself: no state holds them
        }
        if (property == "Background") {
            if (auto control = object.try_as<controls::Control>()) { control.Background(*target); written = true; }
            else if (auto panel = object.try_as<controls::Panel>()) { panel.Background(*target); written = true; }
            else if (auto border = object.try_as<controls::Border>()) { border.Background(*target); written = true; }
        } else if (property == "Foreground") {
            if (auto control = object.try_as<controls::Control>()) {
                control.Foreground(*target);
                FollowForegroundInSprites(control);
                written = true;
            } else if (auto text = object.try_as<controls::TextBlock>()) {
                text.Foreground(*target);
                PaintLinks(text, &*target);
                written = true;
            }
            else if (auto rich = object.try_as<controls::RichTextBlock>()) { rich.Foreground(*target); written = true; }
            else if (auto icon = object.try_as<controls::IconElement>()) { icon.Foreground(*target); written = true; }
            else if (auto presenter = object.try_as<controls::ContentPresenter>()) {
                presenter.Foreground(*target);
                written = true;
            }
        } else {
            if (auto control = object.try_as<controls::Control>()) { control.BorderBrush(*target); written = true; }
            else if (auto border = object.try_as<controls::Border>()) { border.BorderBrush(*target); written = true; }
            else if (auto grid = object.try_as<controls::Grid>()) { grid.BorderBrush(*target); written = true; }
            else if (auto stack = object.try_as<controls::StackPanel>()) { stack.BorderBrush(*target); written = true; }
        }
        if (!written) return false;
        m_gradientFit.Follow(id, std::string(property), object, *target, made);
        if (made) {
            Remember(id, property, value, *target);
        } else {
            Forget(id, property);
        }
        UpdateStates(id, object, property, made ? IInspectable(*target) : nullptr);
        return true;
    }
    return std::nullopt;
}

// The size, family and weight of the text, and the text properties of a TextBlock.
std::optional<bool> XamlTreeBackend::WriteFont(theme::NodeId id, const xaml::DependencyObject& object, std::string_view property, const theme::Value& value) {
    if (property == "FontSize" && value.kind == theme::ValueKind::Length) {
        double size = ParseLength(value.text);
        if (auto control = object.try_as<controls::Control>()) { control.FontSize(size); return true; }
        if (auto text = object.try_as<controls::TextBlock>()) { text.FontSize(size); return true; }
        if (auto rich = object.try_as<controls::RichTextBlock>()) { rich.FontSize(size); return true; }
        return false;
    }

    if (property == "FontFamily" && value.kind == theme::ValueKind::Text) {
        // A font of the theme is "file:///C:/.../fonts/x.ttf#Family" (properties.cpp), as images are given by path.
        media::FontFamily family(winrt::to_hstring(value.text));
        if (auto control = object.try_as<controls::Control>()) { control.FontFamily(family); return true; }
        if (auto text = object.try_as<controls::TextBlock>()) { text.FontFamily(family); return true; }
        if (auto rich = object.try_as<controls::RichTextBlock>()) { rich.FontFamily(family); return true; }
        return false;
    }

    if (property == "FontWeight" && value.kind == theme::ValueKind::Number) {
        auto weight = static_cast<std::uint16_t>(std::stod(value.text));
        if (auto control = object.try_as<controls::Control>()) { control.FontWeight(winrt::Windows::UI::Text::FontWeight{weight}); return true; }
        if (auto text = object.try_as<controls::TextBlock>()) { text.FontWeight(winrt::Windows::UI::Text::FontWeight{weight}); return true; }
        if (auto rich = object.try_as<controls::RichTextBlock>()) { rich.FontWeight(winrt::Windows::UI::Text::FontWeight{weight}); return true; }
        return false;
    }

    if (IsTextProperty(property)) {
        bool written = WriteTextProperty(object, property, value);
        if (written) FollowAppleRewrites(id, object, property);
        return written;
    }
    return std::nullopt;
}

// Margin, Padding and BorderThickness.
std::optional<bool> XamlTreeBackend::WriteThickness(theme::NodeId id, const xaml::DependencyObject& object, std::string_view property, const theme::Value& value) {
    if ((property == "Margin" || property == "Padding" || property == "BorderThickness") && value.kind == theme::ValueKind::Thickness) {
        std::vector<double> sides = Numbers(value.text, ',');
        if (sides.size() != 4) return false;
        xaml::Thickness thickness{sides[0], sides[1], sides[2], sides[3]};  // left, top, right, bottom
        if (property == "Margin") {
            if (auto element = object.try_as<xaml::FrameworkElement>()) { element.Margin(thickness); return true; }
        } else if (property == "Padding") {
            if (auto control = object.try_as<controls::Control>()) {
                if (m_templates.count(id) && !NoNegativeSide(thickness)) return false;
                control.Padding(thickness);
                return true;
            }
            if (auto border = object.try_as<controls::Border>()) { border.Padding(thickness); return true; }
            if (auto grid = object.try_as<controls::Grid>()) { grid.Padding(thickness); return true; }
            if (auto stack = object.try_as<controls::StackPanel>()) { stack.Padding(thickness); return true; }
        } else {
            if (auto control = object.try_as<controls::Control>()) {
                if (m_templates.count(id) && !NoNegativeSide(thickness)) return false;
                control.BorderThickness(thickness);
                return true;
            }
            if (auto border = object.try_as<controls::Border>()) { border.BorderThickness(thickness); return true; }
            if (auto grid = object.try_as<controls::Grid>()) {
                grid.BorderThickness(thickness);
                m_paneCorners.Follow(id, grid);
                return true;
            }
            if (auto stack = object.try_as<controls::StackPanel>()) { stack.BorderThickness(thickness); return true; }
        }
        return false;
    }
    return std::nullopt;
}

// CornerRadius.
std::optional<bool> XamlTreeBackend::WriteCorner(theme::NodeId id, const xaml::DependencyObject& object, std::string_view property, const theme::Value& value) {
    if (property == "CornerRadius" && value.kind == theme::ValueKind::Thickness) {
        std::optional<th::Corners> corners = th::DecodeCorners(value.text);
        if (!corners) return false;
        xaml::CornerRadius radius{corners->topLeft, corners->topRight, corners->bottomRight, corners->bottomLeft};
        if (auto control = object.try_as<controls::Control>()) { control.CornerRadius(radius); return true; }
        if (auto border = object.try_as<controls::Border>()) { border.CornerRadius(radius); return true; }
        if (auto grid = object.try_as<controls::Grid>()) {
            grid.CornerRadius(radius);
            m_paneCorners.Follow(id, grid);  // WinUI rounds the pane, not what it holds
            return true;
        }
        if (auto stack = object.try_as<controls::StackPanel>()) { stack.CornerRadius(radius); return true; }
        if (auto rectangle = object.try_as<xaml::Shapes::Rectangle>()) {  // one radius for all four: the top-left one
            rectangle.RadiusX(radius.TopLeft);
            rectangle.RadiusY(radius.TopLeft);
            return true;
        }
        return false;
    }
    return std::nullopt;
}

// The size an element asks for, and the folded sidebar's width.
std::optional<bool> XamlTreeBackend::WriteSize(theme::NodeId id, const xaml::DependencyObject& object, std::string_view property, const theme::Value& value) {
    if ((property == "Width" || property == "Height" || property == "MinWidth" || property == "MaxWidth" ||
         property == "MinHeight" || property == "MaxHeight") &&
        value.kind == theme::ValueKind::Length) {
        double length = ParseLength(value.text);
        auto element = object.try_as<xaml::FrameworkElement>();
        if (!element) return false;
        if (property == "Width") element.Width(length);
        else if (property == "Height") element.Height(length);
        else if (property == "MinWidth") element.MinWidth(length);
        else if (property == "MaxWidth") element.MaxWidth(length);
        else if (property == "MinHeight") element.MinHeight(length);
        else element.MaxHeight(length);
        if (property == "Width" || property == "Height") FollowArtworkCap(element);
        return true;
    }

    if (property == "CompactPaneLength" && value.kind == theme::ValueKind::Length) {
        auto nav = object.try_as<controls::NavigationView>();
        if (!nav) return false;
        nav.CompactPaneLength(ParseLength(value.text));
        return true;
    }
    return std::nullopt;
}

// Opacity and Visibility.
std::optional<bool> XamlTreeBackend::WriteShown(theme::NodeId id, const xaml::DependencyObject& object, std::string_view property, const theme::Value& value) {
    if (property == "Opacity" && value.kind == theme::ValueKind::Number) {
        if (auto element = object.try_as<xaml::UIElement>()) { element.Opacity(std::stod(value.text)); return true; }
        return false;
    }

    if (property == "Visibility" && value.kind == theme::ValueKind::Visibility) {
        if (auto element = object.try_as<xaml::UIElement>()) {
            // Shown by an animation of Ampify's (HoldVisible), never a local value: that would replace Apple's binding of
            // the slider's thumb for good. Ending the hold gives back Apple's value as it is now, which a write would not.
            std::optional<bool> visible = th::DecodeVisible(value.text);
            if (!visible) return false;
            if (*visible) {
                if (element.Visibility() != xaml::Visibility::Visible) HoldVisible(id, element);
            } else if (!ReleaseVisible(id, element)) {
                element.Visibility(xaml::Visibility::Collapsed);
            }
            return true;
        }
        return false;
    }
    return std::nullopt;
}

// The keyword properties: the direction, the light or dark theme, the alignments.
std::optional<bool> XamlTreeBackend::WriteKeyword(theme::NodeId id, const xaml::DependencyObject& object, std::string_view property, const theme::Value& value) {
    if (property == "FlowDirection" && value.kind == theme::ValueKind::Keyword) {
        if (auto element = object.try_as<xaml::FrameworkElement>()) {
            element.FlowDirection(value.text == "RightToLeft" ? xaml::FlowDirection::RightToLeft
                                                               : xaml::FlowDirection::LeftToRight);
            return true;
        }
        return false;
    }

    if (property == "RequestedTheme" && value.kind == theme::ValueKind::Keyword) {
        if (auto element = object.try_as<xaml::FrameworkElement>()) {
            element.RequestedTheme(value.text == "Dark"    ? xaml::ElementTheme::Dark
                                   : value.text == "Light" ? xaml::ElementTheme::Light
                                                           : xaml::ElementTheme::Default);
            return true;
        }
        return false;
    }

    if (property == "HorizontalAlignment" && value.kind == theme::ValueKind::Keyword) {
        auto element = object.try_as<xaml::FrameworkElement>();
        if (!element) return false;
        if (value.text == "Left") element.HorizontalAlignment(xaml::HorizontalAlignment::Left);
        else if (value.text == "Center") element.HorizontalAlignment(xaml::HorizontalAlignment::Center);
        else if (value.text == "Right") element.HorizontalAlignment(xaml::HorizontalAlignment::Right);
        else element.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
        return true;
    }

    if (property == "VerticalAlignment" && value.kind == theme::ValueKind::Keyword) {
        auto element = object.try_as<xaml::FrameworkElement>();
        if (!element) return false;
        if (value.text == "Top") element.VerticalAlignment(xaml::VerticalAlignment::Top);
        else if (value.text == "Center") element.VerticalAlignment(xaml::VerticalAlignment::Center);
        else if (value.text == "Bottom") element.VerticalAlignment(xaml::VerticalAlignment::Bottom);
        else element.VerticalAlignment(xaml::VerticalAlignment::Stretch);
        return true;
    }
    return std::nullopt;
}

// Where an element stands among its siblings: its grid cell, its layer, its order.
std::optional<bool> XamlTreeBackend::WritePlace(theme::NodeId id, const xaml::DependencyObject& object, std::string_view property, const theme::Value& value) {
    if (property.starts_with("Grid.") && value.kind == theme::ValueKind::Number) {
        auto element = object.try_as<xaml::FrameworkElement>();
        if (!element) return false;
        auto cell = static_cast<std::int32_t>(std::stoi(value.text));
        if (property == "Grid.Row") controls::Grid::SetRow(element, cell);
        else if (property == "Grid.Column") controls::Grid::SetColumn(element, cell);
        else if (property == "Grid.RowSpan") controls::Grid::SetRowSpan(element, cell);
        else if (property == "Grid.ColumnSpan") controls::Grid::SetColumnSpan(element, cell);
        else return false;
        return true;
    }

    if (property == "Canvas.ZIndex" && value.kind == theme::ValueKind::Number) {
        if (auto element = object.try_as<xaml::UIElement>()) {
            controls::Canvas::SetZIndex(element, static_cast<std::int32_t>(std::stoi(value.text)));
            return true;
        }
        return false;
    }

    if (property == th::kOrderProperty && value.kind == theme::ValueKind::Number) {
        return ItemPosition(object, static_cast<std::uint32_t>(std::stoul(value.text))).has_value();
    }
    return std::nullopt;
}

// Transitions and keyframe animations.
std::optional<bool> XamlTreeBackend::WriteMotion(theme::NodeId id, const xaml::DependencyObject& object, std::string_view property, const theme::Value& value) {
    if (property == "Transition" && value.kind == theme::ValueKind::Transition) {
        return WriteTransition(id, object, value);
    }

    if (property == "Animation" && value.kind == theme::ValueKind::Animation) {
        auto element = object.try_as<xaml::UIElement>();
        if (!element) return false;
        // The animations turn and scale around UIElement.CenterPoint, which WinUI refuses while RenderTransformOrigin
        // is in use: a decor with `transform-origin` never turned (09/11). The origin is kept aside, as a refused one.
        auto origin = element.RenderTransformOrigin();
        if (!value.text.empty() && (origin.X != 0 || origin.Y != 0) && m_facadeOrigins.count(id) == 0 &&
            HasLocalValue(element, xaml::UIElement::RenderTransformOriginProperty())) {
            m_facadeOrigins[id] = origin;
            element.ClearValue(xaml::UIElement::RenderTransformOriginProperty());
        }
        return m_animations.Write(id, element, value.text, OriginOf(id, element));
    }
    return std::nullopt;
}

// The render transform and its origin.
std::optional<bool> XamlTreeBackend::WriteTransform(theme::NodeId id, const xaml::DependencyObject& object, std::string_view property, const theme::Value& value) {
    if (property == "RenderTransform") {
        auto element = object.try_as<xaml::UIElement>();
        if (!element) return false;
        auto transition = m_transitions.find(id);
        if (transition != m_transitions.end() && transition->second.find("transform:") != std::string::npos &&
            WriteAnimatedTransform(id, element, value)) {
            return true;
        }
        if (auto story = m_storyDurations.find(id);
            story != m_storyDurations.end() && WriteStoryTransform(id, element, value, story->second)) {
            return true;
        }
        if (value.kind == theme::ValueKind::Transform) {
            std::vector<double> parts = Numbers(value.text, ',');
            if (parts.size() != 5) return false;
            media::CompositeTransform transform;
            transform.ScaleX(parts[0]);
            transform.ScaleY(parts[1]);
            transform.Rotation(parts[2]);
            transform.TranslateX(parts[3]);
            transform.TranslateY(parts[4]);
            CentreOn(id, element, transform);
            element.RenderTransform(transform);
            Remember(id, property, value, transform);
            return true;
        }
        if (value.kind == theme::ValueKind::Opaque) {
            element.RenderTransform(Restore(value).try_as<media::Transform>());
            Forget(id, property);
            return true;
        }
        return false;
    }

    if (property == "RenderTransformOrigin" && value.kind == theme::ValueKind::Point) {
        std::vector<double> point = Numbers(value.text, ',');
        auto element = object.try_as<xaml::UIElement>();
        if (!element || point.size() != 2) return false;
        winrt::Windows::Foundation::Point origin{static_cast<float>(point[0]), static_cast<float>(point[1])};
        try {
            element.RenderTransformOrigin(origin);
            m_facadeOrigins.erase(id);
        } catch (...) {
            // Apple Music animates some elements through Scale and Translation, and WinUI then refuses this property.
            m_facadeOrigins[id] = origin;
        }
        return true;
    }
    return std::nullopt;
}

// Blur and Shadow, drawn by composition.
std::optional<bool> XamlTreeBackend::WriteEffect(theme::NodeId id, const xaml::DependencyObject& object, std::string_view property, const theme::Value& value) {
    if (property == "Blur" && value.kind == theme::ValueKind::Blur) {
        auto element = object.try_as<xaml::UIElement>();
        if (!element) return false;
        double ms = 0;
        if (auto transition = m_transitions.find(id); transition != m_transitions.end()) {
            for (const std::string& item : Split(transition->second, ';')) {
                constexpr std::string_view kFilter = "filter:";
                if (item.starts_with(kFilter)) ms = std::atof(item.c_str() + kFilter.size());
            }
        }
        const bool written = m_blur.Write(id, element, value.text, ms);
        NoteArtworkFilter(std::to_string(id % 100000) + " " + value.text.substr(0, 24) + " ms " +
                          std::to_string(static_cast<int>(ms)) + (written ? "" : " refused"));
        return written;
    }

    if (property == "Shadow") {
        auto element = object.try_as<xaml::UIElement>();
        if (!element) return false;
        if (value.kind == theme::ValueKind::Shadow) {
            // A ThemeShadow grows with the Z of the element. The Z from before the first box-shadow comes back with the
            // original shadow (the Opaque branch below).
            float depth = static_cast<float>(std::stod(value.text));
            auto translation = element.Translation();
            auto before = m_translations.try_emplace(id, translation.z).first;
            media::ThemeShadow shadow{nullptr};
            if (depth > 0) shadow = media::ThemeShadow();
            element.Shadow(shadow);
            translation.z = depth > 0 ? depth : before->second;
            element.Translation(translation);
            Remember(id, property, value, shadow);
            return true;
        }
        if (value.kind == theme::ValueKind::Opaque) {
            element.Shadow(Restore(value).try_as<media::Shadow>());
            if (auto before = m_translations.find(id); before != m_translations.end()) {
                auto translation = element.Translation();
                translation.z = before->second;
                element.Translation(translation);
                m_translations.erase(before);
            }
            Forget(id, property);
            return true;
        }
        return false;
    }
    return std::nullopt;
}

// Clip.
std::optional<bool> XamlTreeBackend::WriteClip(theme::NodeId id, const xaml::DependencyObject& object, std::string_view property, const theme::Value& value) {
    if (property == "Clip") {
        auto element = object.try_as<xaml::FrameworkElement>();
        if (!element) return false;
        if (value.kind == theme::ValueKind::Clip) {
            media::RectangleGeometry clip{nullptr};
            if (!value.text.empty()) {
                std::vector<double> insets = Numbers(value.text, ',');
                if (insets.size() != 4) return false;
                clip = media::RectangleGeometry();
                auto fit = [clip, insets](double width, double height) {
                    clip.Rect({static_cast<float>(insets[0]), static_cast<float>(insets[1]),
                               static_cast<float>(std::max(0.0, width - insets[0] - insets[2])),
                               static_cast<float>(std::max(0.0, height - insets[1] - insets[3]))});
                };
                fit(element.ActualWidth(), element.ActualHeight());
                m_clipSizes[id] = element.SizeChanged(winrt::auto_revoke, [fit](const IInspectable&,
                                                                               const xaml::SizeChangedEventArgs& args) {
                    fit(args.NewSize().Width, args.NewSize().Height);
                });
            } else {
                m_clipSizes.erase(id);
            }
            element.Clip(clip);
            Remember(id, property, value, clip);
            return true;
        }
        if (value.kind == theme::ValueKind::Opaque) {
            m_clipSizes.erase(id);
            element.Clip(Restore(value).try_as<media::RectangleGeometry>());
            Forget(id, property);
            return true;
        }
        return false;
    }
    return std::nullopt;
}

// A control's template, or Apple's put back.
std::optional<bool> XamlTreeBackend::WriteTemplate(theme::NodeId id, const xaml::DependencyObject& object, std::string_view property, const theme::Value& value) {
    if (property == "Template") {
        auto control = object.try_as<controls::Control>();
        if (!control) return false;

        if (value.kind == theme::ValueKind::Template) {
            if (!NoNegativeSide(control.Padding()) || !NoNegativeSide(control.BorderThickness())) return false;
            controls::ControlTemplate templ = xaml::Markup::XamlReader::Load(winrt::to_hstring(value.text)).try_as<controls::ControlTemplate>();
            if (!templ) return false;
            control.Template(templ);
            m_templates[id] = value.text;
            return true;
        }
        if (value.kind == theme::ValueKind::Opaque) {
            control.Template(Restore(value).try_as<controls::ControlTemplate>());
            m_templates.erase(id);
            return true;
        }
    }
    return false;
}

// The theme's value goes: Apple's own comes back, and what followed the theme's value lets go.
bool XamlTreeBackend::ClearOwnValue(theme::NodeId id, const xaml::DependencyObject& object, std::string_view property) {
    xaml::DependencyProperty own = OwnProperty(object, property);
    if (!own) return false;
    if (property == "Visibility") ReleaseVisible(id);
    object.ClearValue(own);
    if (auto control = object.try_as<controls::Control>(); control && property == "Foreground") {
        FollowForegroundInSprites(control);
    }
    if (auto text = object.try_as<controls::TextBlock>(); text && property == "Foreground") PaintLinks(text, nullptr);
    if (auto element = object.try_as<xaml::FrameworkElement>(); element && (property == "Width" || property == "Height")) {
        FollowArtworkCap(element);
    }
    if (property == "CornerRadius" || property == "BorderThickness") m_paneCorners.Follow(id, object);
    Forget(id, property);
    UpdateStates(id, object, property, nullptr);  // or its states would keep the theme's colour under the pointer
    return true;
}

bool XamlTreeBackend::WriteRaw(theme::NodeId id, std::string_view property, const theme::Value& value) {
    xaml::DependencyObject object = Resolve(id);
    if (!object) return false;

    // A colour animation draws this background (ThemeAnimations): the theme's own value waits until it ends or stops.
    if (property == "Background" && m_animations.OwnsBackground(id)) {
        m_animations.WantBackground(id, value);
        return true;
    }

    if (property == th::kCheckedForegroundProperty) return WriteCheckedForeground(id, object, value);

    if (value == theme::kNoOwnValue) return ClearOwnValue(id, object, property);

    if (value == kControlMarginCopy) {
        if (!FollowControlMargin(object)) return false;
        Forget(id, property);
        return true;
    }

    if (IsAppleProperty(property)) return WriteAppleProperty(object, property, value);

    using Writer = std::optional<bool> (XamlTreeBackend::*)(theme::NodeId, const xaml::DependencyObject&,
                                                            std::string_view, const theme::Value&);
    static constexpr Writer kWriters[] = {
        &XamlTreeBackend::WriteBrush,
        &XamlTreeBackend::WriteFont,
        &XamlTreeBackend::WriteThickness,
        &XamlTreeBackend::WriteCorner,
        &XamlTreeBackend::WriteSize,
        &XamlTreeBackend::WriteShown,
        &XamlTreeBackend::WriteKeyword,
        &XamlTreeBackend::WritePlace,
        &XamlTreeBackend::WriteMotion,
        &XamlTreeBackend::WriteTransform,
        &XamlTreeBackend::WriteEffect,
        &XamlTreeBackend::WriteClip,
        &XamlTreeBackend::WriteTemplate,
    };
    for (Writer writer : kWriters) {
        if (std::optional<bool> written = (this->*writer)(id, object, property, value)) return *written;
    }
    return false;
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
