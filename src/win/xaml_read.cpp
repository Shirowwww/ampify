// Reading a property of an element as a theme value: ReadRaw and its readers, one per family of properties.

#if defined(AMPIFY_WITH_XAML)

#include "xaml_backend.hpp"
#include "xaml_properties_detail.hpp"

namespace ampify::win {

using namespace xaml_detail;

// Apple Music's own brush is kept as the object itself: a colour would lose the brush's Opacity (its LCD is white
// at low opacity, restored as plain white) and its theme dictionary. The colour rides along for the inspector.
theme::Value XamlTreeBackend::BrushValue(theme::NodeId id, std::string_view property, const media::Brush& value) {
    if (!value) return Keep(nullptr);
    if (std::optional<theme::Value> known = Recognize(id, property, value)) return *known;
    theme::Value kept = Keep(value);
    if (auto solid = value.try_as<media::SolidColorBrush>()) {
        ColorValue color = solid.Color();
        color.A = static_cast<std::uint8_t>(std::lround(color.A * std::clamp(solid.Opacity(), 0.0, 1.0)));
        kept.text += ";" + ColourText(color);
    }
    return kept;
}

// A transform or a shadow: the value Ampify wrote while the element still holds its object, a token otherwise.
theme::Value XamlTreeBackend::KeptValue(theme::NodeId id, std::string_view property, const IInspectable& value) {
    if (std::optional<theme::Value> known = Recognize(id, property, value)) return *known;
    return Keep(value);
}

// Background, Foreground, BorderBrush and the selection brushes.
std::optional<theme::Value> XamlTreeBackend::ReadBrush(theme::NodeId id, const xaml::DependencyObject& object, std::string_view property) {
    if (property == "Background") {
        if (auto control = object.try_as<controls::Control>()) return BrushValue(id, property, control.Background());
        if (auto panel = object.try_as<controls::Panel>()) return BrushValue(id, property, panel.Background());
        if (auto border = object.try_as<controls::Border>()) return BrushValue(id, property, border.Background());
    } else if (property == "Foreground") {
        if (auto control = object.try_as<controls::Control>()) return BrushValue(id, property, control.Foreground());
        if (auto text = object.try_as<controls::TextBlock>()) {
            std::vector<xaml::Documents::Inline> inlines = InlinesOf(text);
            return BrushValue(id, property, HasLink(inlines) ? inlines.back().Foreground() : text.Foreground());
        }
        if (auto rich = object.try_as<controls::RichTextBlock>()) return BrushValue(id, property, rich.Foreground());
        if (auto icon = object.try_as<controls::IconElement>()) return BrushValue(id, property, icon.Foreground());
        if (auto presenter = object.try_as<controls::ContentPresenter>()) return BrushValue(id, property, presenter.Foreground());
    } else if (IsSelectionBrush(property)) {
        if (auto row = object.try_as<primitives::ListViewItemPresenter>()) return BrushValue(id, property, SelectionBrush(row, property));
    } else if (property == "BorderBrush") {
        if (auto control = object.try_as<controls::Control>()) return BrushValue(id, property, control.BorderBrush());
        if (auto border = object.try_as<controls::Border>()) return BrushValue(id, property, border.BorderBrush());
        if (auto grid = object.try_as<controls::Grid>()) return BrushValue(id, property, grid.BorderBrush());
        if (auto stack = object.try_as<controls::StackPanel>()) return BrushValue(id, property, stack.BorderBrush());
    }
    return std::nullopt;
}

// The size, family and weight of the text, and the text properties of a TextBlock.
std::optional<theme::Value> XamlTreeBackend::ReadFont(theme::NodeId id, const xaml::DependencyObject& object, std::string_view property) {
    if (property == "FontSize") {
        if (auto control = object.try_as<controls::Control>()) return theme::Value{theme::ValueKind::Length, th::FormatNumber(control.FontSize())};
        if (auto text = object.try_as<controls::TextBlock>()) return theme::Value{theme::ValueKind::Length, th::FormatNumber(text.FontSize())};
        if (auto rich = object.try_as<controls::RichTextBlock>()) return theme::Value{theme::ValueKind::Length, th::FormatNumber(rich.FontSize())};
    } else if (property == "FontFamily") {
        // A font of the theme reads back as the file URI and family it was written with (FontFamily.Source).
        if (auto control = object.try_as<controls::Control>()) return theme::Value{theme::ValueKind::Text, Utf8(control.FontFamily().Source())};
        if (auto text = object.try_as<controls::TextBlock>()) return theme::Value{theme::ValueKind::Text, Utf8(text.FontFamily().Source())};
        if (auto rich = object.try_as<controls::RichTextBlock>()) return theme::Value{theme::ValueKind::Text, Utf8(rich.FontFamily().Source())};
    } else if (property == "FontWeight") {
        if (auto control = object.try_as<controls::Control>()) return NumberValue(control.FontWeight().Weight);
        if (auto text = object.try_as<controls::TextBlock>()) return NumberValue(text.FontWeight().Weight);
        if (auto rich = object.try_as<controls::RichTextBlock>()) return NumberValue(rich.FontWeight().Weight);
    } else if (IsTextProperty(property)) {
        return ReadTextProperty(object, property);
    }
    return std::nullopt;
}

// Margin, Padding, BorderThickness and CornerRadius.
std::optional<theme::Value> XamlTreeBackend::ReadThickness(theme::NodeId id, const xaml::DependencyObject& object, std::string_view property) {
    if (property == "Margin") {
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
    }
    return std::nullopt;
}

// Visibility, Opacity, the size an element asks for and the panes' widths.
std::optional<theme::Value> XamlTreeBackend::ReadSize(theme::NodeId id, const xaml::DependencyObject& object, std::string_view property) {
    if (property == "Visibility") {
        if (auto element = object.try_as<xaml::UIElement>()) {
            const bool collapsed = element.Visibility() == xaml::Visibility::Collapsed;
            return theme::Value{theme::ValueKind::Visibility, std::string(collapsed ? th::kCollapsed : th::kVisible)};
        }
    } else if (property == "Opacity") {
        if (auto element = object.try_as<xaml::UIElement>()) return NumberValue(element.Opacity());
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
    }
    return std::nullopt;
}

// The keyword properties: the direction, the light or dark theme, the alignments.
std::optional<theme::Value> XamlTreeBackend::ReadKeyword(theme::NodeId id, const xaml::DependencyObject& object, std::string_view property) {
    if (property == "FlowDirection") {
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
    }
    return std::nullopt;
}

// Where an element stands among its siblings: its grid cell, its layer, its order.
std::optional<theme::Value> XamlTreeBackend::ReadPlace(theme::NodeId id, const xaml::DependencyObject& object, std::string_view property) {
    if (property.starts_with("Grid.")) {
        // Only for the children of a Grid: anywhere else the attached value would be read but change nothing.
        auto element = object.try_as<xaml::FrameworkElement>();
        xaml::DependencyObject parent = media::VisualTreeHelper::GetParent(object);
        if (element && parent && parent.try_as<controls::Grid>()) {
            if (property == "Grid.Row") return NumberValue(controls::Grid::GetRow(element));
            if (property == "Grid.Column") return NumberValue(controls::Grid::GetColumn(element));
            if (property == "Grid.RowSpan") return NumberValue(controls::Grid::GetRowSpan(element));
            if (property == "Grid.ColumnSpan") return NumberValue(controls::Grid::GetColumnSpan(element));
        }
    } else if (property == "Canvas.ZIndex") {
        if (auto element = object.try_as<xaml::UIElement>()) return NumberValue(controls::Canvas::GetZIndex(element));
    } else if (property == th::kOrderProperty) {
        if (std::optional<std::uint32_t> position = ItemPosition(object, std::nullopt)) {
            return theme::Value{theme::ValueKind::Number, std::to_string(*position)};
        }
    }
    return std::nullopt;
}

// The transform and its origin, transitions, animations and the composition effects.
std::optional<theme::Value> XamlTreeBackend::ReadMotion(theme::NodeId id, const xaml::DependencyObject& object, std::string_view property) {
    if (property == "RenderTransform") {
        if (auto facade = m_facadeTransforms.find(id); facade != m_facadeTransforms.end()) {
            return theme::Value{theme::ValueKind::Transform, facade->second};
        }
        if (auto element = object.try_as<xaml::UIElement>()) return KeptValue(id, property, element.RenderTransform());
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
    } else if (property == "Blur") {
        if (object.try_as<xaml::UIElement>()) return theme::Value{theme::ValueKind::Blur, m_blur.Read(id)};
    } else if (property == "Shadow") {
        if (auto element = object.try_as<xaml::UIElement>()) return KeptValue(id, property, element.Shadow());
    } else if (property == "Clip") {
        if (auto element = object.try_as<xaml::FrameworkElement>()) return KeptValue(id, property, element.Clip());
    }
    return std::nullopt;
}

// What only Ampify writes: the checked foreground, a template, the visual states.
std::optional<theme::Value> XamlTreeBackend::ReadOwn(theme::NodeId id, const xaml::DependencyObject& object, std::string_view property) {
    if (property == th::kCheckedForegroundProperty) {
        if (auto control = object.try_as<controls::Control>()) {
            CheckedKeys* known = LiveChecked(id, control);
            if (!known) return theme::kNoOwnValue;
            return theme::Value{theme::ValueKind::Color, known->colour};
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

std::optional<theme::Value> XamlTreeBackend::ReadRaw(theme::NodeId id, std::string_view property) {
    xaml::DependencyObject object = Resolve(id);
    if (!object) return std::nullopt;
    if (IsAppleProperty(property)) return ReadAppleProperty(object, property);

    // While a colour animation draws the background, the theme's own value reads back: the animated brush is not it.
    if (property == "Background") {
        if (std::optional<theme::Value> wanted = m_animations.WantedBackground(id)) return wanted;
    }

    using Reader = std::optional<theme::Value> (XamlTreeBackend::*)(theme::NodeId, const xaml::DependencyObject&,
                                                                    std::string_view);
    static constexpr Reader kReaders[] = {
        &XamlTreeBackend::ReadBrush,
        &XamlTreeBackend::ReadFont,
        &XamlTreeBackend::ReadThickness,
        &XamlTreeBackend::ReadSize,
        &XamlTreeBackend::ReadKeyword,
        &XamlTreeBackend::ReadPlace,
        &XamlTreeBackend::ReadMotion,
        &XamlTreeBackend::ReadOwn,
    };
    for (Reader reader : kReaders) {
        if (std::optional<theme::Value> read = (this->*reader)(id, object, property)) return read;
    }
    return std::nullopt;
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
