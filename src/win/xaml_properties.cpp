// XamlTreeBackend: reading and writing one property of one element (ReadRaw, WriteRaw and the transitions and
// animated transforms they start). The rest of the class is in xaml_backend.cpp.

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

std::vector<xaml::Documents::Inline> InlinesOf(const controls::TextBlock& text) {
    std::vector<xaml::Documents::Inline> all;
    CollectInlines(text.Inlines(), all);
    return all;
}

bool HasLink(const std::vector<xaml::Documents::Inline>& inlines) {
    return std::any_of(inlines.begin(), inlines.end(),
                       [](const auto& inline_) { return inline_.try_as<xaml::Documents::Hyperlink>() != nullptr; });
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

}  // namespace

std::optional<theme::Value> XamlTreeBackend::ReadRaw(theme::NodeId id, std::string_view property) {
    xaml::DependencyObject object = Resolve(id);
    if (!object) return std::nullopt;
    if (IsAppleProperty(property)) return ReadAppleProperty(object, property);

    // While a colour animation draws the background, the theme's own value reads back: the animated brush is not it.
    if (property == "Background") {
        if (std::optional<theme::Value> wanted = m_animations.WantedBackground(id)) return wanted;
    }

    // Apple Music's own brush is kept as the object itself: a colour would lose the brush's Opacity (its LCD is white
    // at low opacity, restored as plain white) and its theme dictionary. The colour rides along for the inspector.
    auto brush = [&](const media::Brush& value) -> theme::Value {
        if (!value) return Keep(nullptr);
        if (std::optional<theme::Value> known = Recognize(id, property, value)) return *known;
        theme::Value kept = Keep(value);
        if (auto solid = value.try_as<media::SolidColorBrush>()) {
            ColorValue color = solid.Color();
            color.A = static_cast<std::uint8_t>(std::lround(color.A * std::clamp(solid.Opacity(), 0.0, 1.0)));
            kept.text += ";" + ColourText(color);
        }
        return kept;
    };
    // A transform or a shadow: the value Ampify wrote while the element still holds its object, a token otherwise.
    auto kept = [&](const IInspectable& value) -> theme::Value {
        if (std::optional<theme::Value> known = Recognize(id, property, value)) return *known;
        return Keep(value);
    };
    auto number = [](double value) { return theme::Value{theme::ValueKind::Number, th::FormatNumber(value)}; };

    if (property == "Background") {
        if (auto control = object.try_as<controls::Control>()) return brush(control.Background());
        if (auto panel = object.try_as<controls::Panel>()) return brush(panel.Background());
        if (auto border = object.try_as<controls::Border>()) return brush(border.Background());
    } else if (property == "Foreground") {
        if (auto control = object.try_as<controls::Control>()) return brush(control.Foreground());
        if (auto text = object.try_as<controls::TextBlock>()) {
            std::vector<xaml::Documents::Inline> inlines = InlinesOf(text);
            return brush(HasLink(inlines) ? inlines.back().Foreground() : text.Foreground());
        }
        if (auto rich = object.try_as<controls::RichTextBlock>()) return brush(rich.Foreground());
        if (auto icon = object.try_as<controls::IconElement>()) return brush(icon.Foreground());
        if (auto presenter = object.try_as<controls::ContentPresenter>()) return brush(presenter.Foreground());
    } else if (IsSelectionBrush(property)) {
        if (auto row = object.try_as<primitives::ListViewItemPresenter>()) return brush(SelectionBrush(row, property));
    } else if (property == "BorderBrush") {
        if (auto control = object.try_as<controls::Control>()) return brush(control.BorderBrush());
        if (auto border = object.try_as<controls::Border>()) return brush(border.BorderBrush());
        if (auto grid = object.try_as<controls::Grid>()) return brush(grid.BorderBrush());
        if (auto stack = object.try_as<controls::StackPanel>()) return brush(stack.BorderBrush());
    } else if (property == "FontSize") {
        if (auto control = object.try_as<controls::Control>()) return theme::Value{theme::ValueKind::Length, th::FormatNumber(control.FontSize())};
        if (auto text = object.try_as<controls::TextBlock>()) return theme::Value{theme::ValueKind::Length, th::FormatNumber(text.FontSize())};
        if (auto rich = object.try_as<controls::RichTextBlock>()) return theme::Value{theme::ValueKind::Length, th::FormatNumber(rich.FontSize())};
    } else if (property == "FontFamily") {
        // A font of the theme reads back as the file URI and family it was written with (FontFamily.Source).
        if (auto control = object.try_as<controls::Control>()) return theme::Value{theme::ValueKind::Text, Utf8(control.FontFamily().Source())};
        if (auto text = object.try_as<controls::TextBlock>()) return theme::Value{theme::ValueKind::Text, Utf8(text.FontFamily().Source())};
        if (auto rich = object.try_as<controls::RichTextBlock>()) return theme::Value{theme::ValueKind::Text, Utf8(rich.FontFamily().Source())};
    } else if (property == "FontWeight") {
        if (auto control = object.try_as<controls::Control>()) return number(control.FontWeight().Weight);
        if (auto text = object.try_as<controls::TextBlock>()) return number(text.FontWeight().Weight);
        if (auto rich = object.try_as<controls::RichTextBlock>()) return number(rich.FontWeight().Weight);
    } else if (IsTextProperty(property)) {
        return ReadTextProperty(object, property);
    } else if (property == "Margin") {
        if (auto element = object.try_as<xaml::FrameworkElement>()) return theme::Value{theme::ValueKind::Thickness, ThicknessText(element.Margin())};
    } else if (property == "Padding") {
        if (auto control = object.try_as<controls::Control>()) return theme::Value{theme::ValueKind::Thickness, ThicknessText(control.Padding())};
        if (auto border = object.try_as<controls::Border>()) return theme::Value{theme::ValueKind::Thickness, ThicknessText(border.Padding())};
        if (auto grid = object.try_as<controls::Grid>()) return theme::Value{theme::ValueKind::Thickness, ThicknessText(grid.Padding())};
        if (auto stack = object.try_as<controls::StackPanel>()) return theme::Value{theme::ValueKind::Thickness, ThicknessText(stack.Padding())};
    } else if (property == "BorderThickness") {
        if (auto control = object.try_as<controls::Control>()) return theme::Value{theme::ValueKind::Thickness, ThicknessText(control.BorderThickness())};
        if (auto border = object.try_as<controls::Border>()) return theme::Value{theme::ValueKind::Thickness, ThicknessText(border.BorderThickness())};
        if (auto grid = object.try_as<controls::Grid>()) return theme::Value{theme::ValueKind::Thickness, ThicknessText(grid.BorderThickness())};
        if (auto stack = object.try_as<controls::StackPanel>()) return theme::Value{theme::ValueKind::Thickness, ThicknessText(stack.BorderThickness())};
    } else if (property == "CornerRadius") {
        if (auto control = object.try_as<controls::Control>()) return theme::Value{theme::ValueKind::Thickness, CornerText(control.CornerRadius())};
        if (auto border = object.try_as<controls::Border>()) return theme::Value{theme::ValueKind::Thickness, CornerText(border.CornerRadius())};
        // Grid and StackPanel draw their own border in WinUI 3: a card of Apple Music is often one of them.
        if (auto grid = object.try_as<controls::Grid>()) return theme::Value{theme::ValueKind::Thickness, CornerText(grid.CornerRadius())};
        if (auto stack = object.try_as<controls::StackPanel>()) return theme::Value{theme::ValueKind::Thickness, CornerText(stack.CornerRadius())};
        // A Rectangle (a slider's track) rounds its four corners alike, by RadiusX and RadiusY.
        if (auto rectangle = object.try_as<xaml::Shapes::Rectangle>()) {
            double radius = rectangle.RadiusX();
            return theme::Value{theme::ValueKind::Thickness, CornerText(xaml::CornerRadius{radius, radius, radius, radius})};
        }
    } else if (property == "Visibility") {
        if (auto element = object.try_as<xaml::UIElement>()) {
            return theme::Value{theme::ValueKind::Visibility, element.Visibility() == xaml::Visibility::Collapsed ? "Collapsed" : "Visible"};
        }
    } else if (property == "Opacity") {
        if (auto element = object.try_as<xaml::UIElement>()) return number(element.Opacity());
    } else if (property == "Width" || property == "Height" || property == "MinWidth" || property == "MaxWidth" ||
               property == "MinHeight" || property == "MaxHeight") {
        if (auto element = object.try_as<xaml::FrameworkElement>()) {
            double value = property == "Width"       ? element.Width()
                           : property == "Height"    ? element.Height()
                           : property == "MinWidth"  ? element.MinWidth()
                           : property == "MaxWidth"  ? element.MaxWidth()
                           : property == "MinHeight" ? element.MinHeight()
                                                     : element.MaxHeight();
            return theme::Value{theme::ValueKind::Length, th::FormatNumber(value)};
        }
    } else if (property == "CompactPaneLength") {
        if (auto nav = object.try_as<controls::NavigationView>()) {
            return theme::Value{theme::ValueKind::Length, th::FormatNumber(nav.CompactPaneLength())};
        }
    } else if (property == "OpenPaneLength") {  // read only: the engine fits a pane's `width: auto` to it (pane.hpp)
        if (auto split = object.try_as<controls::SplitView>()) {
            return theme::Value{theme::ValueKind::Length, th::FormatNumber(split.OpenPaneLength())};
        }
    } else if (property == "FlowDirection") {
        if (auto element = object.try_as<xaml::FrameworkElement>()) {
            bool rtl = element.FlowDirection() == xaml::FlowDirection::RightToLeft;
            return theme::Value{theme::ValueKind::Keyword, rtl ? "RightToLeft" : "LeftToRight"};
        }
    } else if (property == "RequestedTheme") {
        if (auto element = object.try_as<xaml::FrameworkElement>()) {
            xaml::ElementTheme mode = element.RequestedTheme();
            return theme::Value{theme::ValueKind::Keyword, mode == xaml::ElementTheme::Dark    ? "Dark"
                                                           : mode == xaml::ElementTheme::Light ? "Light"
                                                                                               : "Default"};
        }
    } else if (property == "HorizontalAlignment") {
        if (auto element = object.try_as<xaml::FrameworkElement>()) return theme::Value{theme::ValueKind::Keyword, AlignmentText(element.HorizontalAlignment())};
    } else if (property == "VerticalAlignment") {
        if (auto element = object.try_as<xaml::FrameworkElement>()) return theme::Value{theme::ValueKind::Keyword, AlignmentText(element.VerticalAlignment())};
    } else if (property.starts_with("Grid.")) {
        // Only for the children of a Grid: anywhere else the attached value would be read but change nothing.
        auto element = object.try_as<xaml::FrameworkElement>();
        xaml::DependencyObject parent = media::VisualTreeHelper::GetParent(object);
        if (element && parent && parent.try_as<controls::Grid>()) {
            if (property == "Grid.Row") return number(controls::Grid::GetRow(element));
            if (property == "Grid.Column") return number(controls::Grid::GetColumn(element));
            if (property == "Grid.RowSpan") return number(controls::Grid::GetRowSpan(element));
            if (property == "Grid.ColumnSpan") return number(controls::Grid::GetColumnSpan(element));
        }
    } else if (property == "Canvas.ZIndex") {
        if (auto element = object.try_as<xaml::UIElement>()) return number(controls::Canvas::GetZIndex(element));
    } else if (property == "RenderTransform") {
        if (auto facade = m_facadeTransforms.find(id); facade != m_facadeTransforms.end()) {
            return theme::Value{theme::ValueKind::Transform, facade->second};
        }
        if (auto element = object.try_as<xaml::UIElement>()) return kept(element.RenderTransform());
    } else if (property == "Transition") {
        if (object.try_as<xaml::UIElement>()) {
            auto known = m_transitions.find(id);
            return theme::Value{theme::ValueKind::Transition, known == m_transitions.end() ? "" : known->second};
        }
    } else if (property == "Animation") {
        if (object.try_as<xaml::UIElement>()) return theme::Value{theme::ValueKind::Animation, m_animations.Read(id)};
    } else if (property == "RenderTransformOrigin") {
        if (auto element = object.try_as<xaml::UIElement>()) {
            winrt::Windows::Foundation::Point origin = OriginOf(id, element);
            return theme::Value{theme::ValueKind::Point, FractionText(origin.X) + "," + FractionText(origin.Y)};
        }
    } else if (property == "Shadow") {
        if (auto element = object.try_as<xaml::UIElement>()) return kept(element.Shadow());
    } else if (property == "Clip") {
        if (auto element = object.try_as<xaml::FrameworkElement>()) return kept(element.Clip());
    } else if (property == th::kCheckedForegroundProperty) {
        if (auto control = object.try_as<controls::Control>()) {
            CheckedKeys* known = LiveChecked(id, control);
            if (!known) return theme::kNoOwnValue;
            return theme::Value{theme::ValueKind::Color, known->colour};
        }
    } else if (property == th::kOrderProperty) {
        if (std::optional<std::uint32_t> position = ItemPosition(object, std::nullopt)) {
            return theme::Value{theme::ValueKind::Number, std::to_string(*position)};
        }
    } else if (property == "Template") {
        if (auto control = object.try_as<controls::Control>()) {
            auto known = m_templates.find(id);
            if (known != m_templates.end()) return theme::Value{theme::ValueKind::Template, known->second};
            return Keep(control.Template());
        }
    } else if (property == "States") {
        if (auto element = object.try_as<xaml::FrameworkElement>()) {
            std::string states = DescribeStates(element);
            if (!states.empty()) return theme::Value{theme::ValueKind::Text, states};
        }
    }
    return std::nullopt;
}

bool XamlTreeBackend::WriteTransition(theme::NodeId id, const xaml::DependencyObject& object, const theme::Value& value) {
    auto element = object.try_as<xaml::UIElement>();
    if (!element) return false;
    // "transform:200;opacity:150;background:300": duration in ms by property.
    std::map<std::string, double, std::less<>> durations;
    for (const std::string& item : Split(value.text, ';')) {
        size_t colon = item.find(':');
        if (colon != std::string::npos) durations[item.substr(0, colon)] = std::atof(item.c_str() + colon + 1);
    }
    auto panel = object.try_as<controls::Panel>();
    auto border = object.try_as<controls::Border>();
    auto presenter = object.try_as<controls::ContentPresenter>();
    try {
        // The transitions Apple Music set come back when the theme stops animating this element.
        auto [originals, first] = m_transitionOriginals.try_emplace(id);
        if (first) {
            originals->second.opacity = element.OpacityTransition();
            originals->second.scale = element.ScaleTransition();
            originals->second.rotation = element.RotationTransition();
            originals->second.translation = element.TranslationTransition();
            if (panel) originals->second.background = panel.BackgroundTransition();
            else if (border) originals->second.background = border.BackgroundTransition();
            else if (presenter) originals->second.background = presenter.BackgroundTransition();
        }
        const TransitionOriginals& before = originals->second;

        xaml::ScalarTransition opacity = before.opacity;
        if (auto ms = durations.find("opacity"); ms != durations.end()) {
            opacity = xaml::ScalarTransition();
            opacity.Duration(Milliseconds(ms->second));
        }
        element.OpacityTransition(opacity);

        xaml::BrushTransition background = before.background;
        if (auto ms = durations.find("background"); ms != durations.end()) {
            background = xaml::BrushTransition();
            background.Duration(Milliseconds(ms->second));
        }
        if (panel) panel.BackgroundTransition(background);
        else if (border) border.BackgroundTransition(background);
        else if (presenter) presenter.BackgroundTransition(background);

        xaml::Vector3Transition scale = before.scale;
        xaml::ScalarTransition rotation = before.rotation;
        xaml::Vector3Transition translation = before.translation;
        if (auto ms = durations.find("transform"); ms != durations.end()) {
            scale = xaml::Vector3Transition();
            scale.Duration(Milliseconds(ms->second));
            rotation = xaml::ScalarTransition();
            rotation.Duration(Milliseconds(ms->second));
            translation = xaml::Vector3Transition();
            translation.Duration(Milliseconds(ms->second));
        }
        element.ScaleTransition(scale);
        element.RotationTransition(rotation);
        element.TranslationTransition(translation);
    } catch (...) {
        // An element Apple Music drives through its composition visual refuses these properties. A transition of the
        // transform alone still eases, on its RenderTransform (WriteStoryTransform); any other is refused.
        auto transform = durations.find("transform");
        if (!value.text.empty() && (transform == durations.end() || durations.size() != 1)) return false;
        m_transitionOriginals.erase(id);
        if (value.text.empty()) {
            m_storyDurations.erase(id);
            m_transitions.erase(id);
        } else {
            m_storyDurations[id] = transform->second;
            m_transitions[id] = value.text;
        }
        return true;
    }
    if (value.text.empty()) {
        m_transitions.erase(id);
        m_transitionOriginals.erase(id);
    } else {
        m_transitions[id] = value.text;
    }
    return true;
}

// For an element whose transform animates: the transform goes to UIElement.Scale, Rotation and Translation, which
// the transitions above animate, around the element's RenderTransformOrigin. A value from before the theme (Opaque)
// animates back to no transform. False when the element refuses, and the caller writes RenderTransform instead.
bool XamlTreeBackend::WriteAnimatedTransform(theme::NodeId id, const xaml::UIElement& element, const theme::Value& value) {
    std::vector<double> parts{1, 1, 0, 0, 0};
    if (value.kind == theme::ValueKind::Transform) {
        parts = Numbers(value.text, ',');
        if (parts.size() != 5) return false;
    } else if (value.kind != theme::ValueKind::Opaque) {
        return false;
    }
    try {
        // CSS scales around the centre by default, XAML around the top left corner: an origin left at 0,0 means the
        // theme did not choose one.
        auto origin = OriginOf(id, element);
        if (origin.X == 0 && origin.Y == 0 && m_facadeOrigins.count(id) == 0) origin = {0.5f, 0.5f};
        auto size = element.ActualSize();
        element.CenterPoint({size.x * origin.X, size.y * origin.Y, 0});
        element.Scale({static_cast<float>(parts[0]), static_cast<float>(parts[1]), 1});
        element.Rotation(static_cast<float>(parts[2]));
        auto translation = element.Translation();
        element.Translation({static_cast<float>(parts[3]), static_cast<float>(parts[4]), translation.z});
    } catch (...) {
        return false;
    }
    if (value.kind == theme::ValueKind::Transform) {
        m_facadeTransforms[id] = value.text;
    } else {
        m_facadeTransforms.erase(id);
    }
    return true;
}

winrt::Windows::Foundation::Point XamlTreeBackend::OriginOf(theme::NodeId id, const xaml::UIElement& element) const {
    auto kept = m_facadeOrigins.find(id);
    return kept != m_facadeOrigins.end() ? kept->second : element.RenderTransformOrigin();
}

// A CompositeTransform turns around the element's RenderTransformOrigin by itself. Its centre takes the origin the
// element refused, or the middle when none was chosen: CSS scales from the centre, XAML from the top left corner (a
// hovered button grew towards its bottom right corner).
void XamlTreeBackend::CentreOn(theme::NodeId id, const xaml::UIElement& element,
                               const media::CompositeTransform& transform) const {
    winrt::Windows::Foundation::Point origin = OriginOf(id, element);
    bool refused = m_facadeOrigins.count(id) > 0;
    if (!refused && (origin.X != 0 || origin.Y != 0)) return;
    if (!refused) origin = {0.5f, 0.5f};
    auto size = element.ActualSize();
    transform.CenterX(size.x * origin.X);
    transform.CenterY(size.y * origin.Y);
}

// For an element Apple Music animates itself, whose facade refuses the transitions: the transform eases from where it
// is to the theme's value on Ampify's CompositeTransform, a Storyboard WinUI runs off the interface thread. Back to the
// value from before the theme (Opaque), it eases to no transform, then puts that value back.
bool XamlTreeBackend::WriteStoryTransform(theme::NodeId id, const xaml::UIElement& element, const theme::Value& value,
                                          double ms) {
    std::vector<double> target{1, 1, 0, 0, 0};
    if (value.kind == theme::ValueKind::Transform) {
        target = Numbers(value.text, ',');
        if (target.size() != 5) return false;
    } else if (value.kind != theme::ValueKind::Opaque) {
        return false;
    }
    try {
        media::CompositeTransform transform{nullptr};
        if (auto ours = m_storyTransforms.find(id);
            ours != m_storyTransforms.end() && ComIdentity(element.RenderTransform()) == ComIdentity(ours->second)) {
            transform = ours->second;
        }
        if (!transform) {
            if (value.kind == theme::ValueKind::Opaque) return false;  // nothing of Ampify's to ease back
            transform = media::CompositeTransform();
            element.RenderTransform(transform);
            m_storyTransforms[id] = transform;
        }
        CentreOn(id, element, transform);
        // Read before stopping the last storyboard, which would put its end values back.
        const double from[5] = {transform.ScaleX(), transform.ScaleY(), transform.Rotation(), transform.TranslateX(),
                                transform.TranslateY()};
        if (auto running = m_stories.find(id); running != m_stories.end()) {
            running->second.Stop();
            m_stories.erase(running);
        }
        transform.ScaleX(target[0]);
        transform.ScaleY(target[1]);
        transform.Rotation(target[2]);
        transform.TranslateX(target[3]);
        transform.TranslateY(target[4]);

        animation::Storyboard story = EaseTransform(transform, from, target, ms);
        Remember(id, "RenderTransform", value, transform);
        bool back = value.kind == theme::ValueKind::Opaque;
        IInspectable original = back ? Restore(value) : nullptr;
        if (story.Children().Size() == 0) {
            if (back) {
                m_storyTransforms.erase(id);
                Forget(id, "RenderTransform");
                element.RenderTransform(original.try_as<media::Transform>());
            }
            return true;
        }
        // The handler knows its storyboard by address only: holding it would keep both alive for good.
        void* self = winrt::get_abi(story);
        winrt::weak_ref<xaml::UIElement> weak = winrt::make_weak(element);
        std::weak_ptr<bool> alive = m_alive;
        story.Completed([this, alive, id, self, back, weak, original](IInspectable const&, IInspectable const&) noexcept {
            try {
                if (alive.expired()) return;
                auto running = m_stories.find(id);
                if (running == m_stories.end() || winrt::get_abi(running->second) != self) return;  // a newer motion
                m_stories.erase(running);
                if (!back) return;
                // Only over Ampify's transform: a theme value written since, by another path, stays.
                auto ours = m_storyTransforms.find(id);
                xaml::UIElement live = weak.get();
                if (ours == m_storyTransforms.end() || !live ||
                    ComIdentity(live.RenderTransform()) != ComIdentity(ours->second)) {
                    return;
                }
                m_storyTransforms.erase(ours);
                Forget(id, "RenderTransform");
                live.RenderTransform(original.try_as<media::Transform>());
            } catch (...) {
                // nothing may escape into Apple Music's XAML; the next write of this transform puts it right
            }
        });
        m_stories[id] = story;
        story.Begin();
    } catch (...) {
        return false;
    }
    return true;
}

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

bool XamlTreeBackend::WriteRaw(theme::NodeId id, std::string_view property, const theme::Value& value) {
    xaml::DependencyObject object = Resolve(id);
    if (!object) return false;

    // A colour animation draws this background (ThemeAnimations): the theme's own value waits until it ends or stops.
    if (property == "Background" && m_animations.OwnsBackground(id)) {
        m_animations.WantBackground(id, value);
        return true;
    }

    if (property == th::kCheckedForegroundProperty) return WriteCheckedForeground(id, object, value);

    if (value == theme::kNoOwnValue) {
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

    if (IsAppleProperty(property)) return WriteAppleProperty(object, property, value);

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

    if (property == "CornerRadius" && value.kind == theme::ValueKind::Thickness) {
        // Bottom-left, top-left, top-right, bottom-right (properties.cpp), the order CornerText reads back.
        std::vector<double> corners = Numbers(value.text, ',');
        if (corners.size() != 4) return false;
        xaml::CornerRadius radius{corners[1], corners[2], corners[3], corners[0]};  // TopLeft, TopRight, BottomRight, BottomLeft
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

    if (property == "Opacity" && value.kind == theme::ValueKind::Number) {
        if (auto element = object.try_as<xaml::UIElement>()) { element.Opacity(std::stod(value.text)); return true; }
        return false;
    }

    if (property == "Visibility" && value.kind == theme::ValueKind::Visibility) {
        if (auto element = object.try_as<xaml::UIElement>()) {
            // Shown by an animation of Ampify's (HoldVisible), never a local value: that would replace Apple's binding of
            // the slider's thumb for good. Ending the hold gives back Apple's value as it is now, which a write would not.
            if (ToLowerAscii(value.text) != "collapsed") {
                if (element.Visibility() != xaml::Visibility::Visible) HoldVisible(id, element);
            } else if (!ReleaseVisible(id, element)) {
                element.Visibility(xaml::Visibility::Collapsed);
            }
            return true;
        }
        return false;
    }

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

    if (property == "Transition" && value.kind == theme::ValueKind::Transition) {
        return WriteTransition(id, object, value);
    }

    if (property == "Animation" && value.kind == theme::ValueKind::Animation) {
        auto element = object.try_as<xaml::UIElement>();
        return element && m_animations.Write(id, element, value.text, OriginOf(id, element));
    }

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

    if (property == th::kOrderProperty && value.kind == theme::ValueKind::Number) {
        return ItemPosition(object, static_cast<std::uint32_t>(std::stoul(value.text))).has_value();
    }

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

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
