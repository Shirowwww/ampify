// Conversions between the theme engine's value texts and XAML values, shared by the files of XamlTreeBackend
// (xaml_backend.cpp, xaml_properties.cpp, xaml_states.cpp). Declared in xaml_backend_detail.hpp.

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
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.Foundation.Numerics.h>

#include "../core/theme/colour_keys.hpp"
#include "../core/theme/properties.hpp"
#include "../core/text_util.hpp"
#include "../core/ui_render/render_tree.hpp"
#include "xaml_host.hpp"
#include "xaml_backend_detail.hpp"

namespace ampify::win::xaml_detail {

// `#AARRGGBB` to a colour. Nothing when the text is not that form.
std::optional<ColorValue> ColorFromText(const std::string& text) {
    if (text.size() != 9 || text[0] != '#') return std::nullopt;
    unsigned long packed = std::stoul(text.substr(1), nullptr, 16);
    return ColorValue{static_cast<std::uint8_t>(packed >> 24), static_cast<std::uint8_t>(packed >> 16),
                      static_cast<std::uint8_t>(packed >> 8), static_cast<std::uint8_t>(packed)};
}

// True when the text only holds icon glyphs: private-use characters (U+E000-U+F8FF, planes 15 and 16 as surrogates),
// where Segoe Fluent Icons and Apple's symbol fonts put their icons. Spaces around them do not count.
bool IsIconText(const winrt::hstring& text) {
    bool glyph = false;
    for (wchar_t unit : text) {
        if (unit == L' ') continue;
        bool privateUse = (unit >= 0xE000 && unit <= 0xF8FF) || (unit >= 0xDB80 && unit <= 0xDFFF);
        if (!privateUse) return false;
        glyph = true;
    }
    return glyph;
}

std::string ColorText(ColorValue color) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X%02X", color.A, color.R, color.G, color.B);
    return buffer;
}

std::vector<double> Numbers(const std::string& text, char separator) {
    std::vector<double> numbers;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(separator, start);
        if (end == std::string::npos) end = text.size();
        numbers.push_back(std::stod(text.substr(start, end - start)));
        start = end + 1;
    }
    return numbers;
}

namespace {

// `file:///C:/...` for a path with '/' separators.
winrt::hstring FileUri(const std::string& path) {
    return winrt::to_hstring("file:///" + path);
}

}  // namespace

std::vector<std::string> Split(const std::string& text, char separator) {
    std::vector<std::string> pieces;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(separator, start);
        if (end == std::string::npos) end = text.size();
        pieces.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return pieces;
}

// Four decimals, as the core writes the points of a value (properties.cpp): a float read back from XAML (0.3f is
// 0.30000001...) then gives the same text as the value the theme asked for.
std::string FractionText(double number) {
    number = std::round(number * 10000) / 10000;
    if (number == 0) number = 0;
    return th::FormatNumber(number);
}

std::string ThicknessText(const xaml::Thickness& value) {
    return th::FormatNumber(value.Left) + "," + th::FormatNumber(value.Top) + "," + th::FormatNumber(value.Right) + "," +
           th::FormatNumber(value.Bottom);
}

namespace {

// BorderBrush, Padding, BorderThickness and CornerRadius, on the four kinds of element that carry them.
xaml::DependencyProperty OwnBoxProperty(const xaml::DependencyObject& object, std::string_view property) {
    const bool control = static_cast<bool>(object.try_as<controls::Control>());
    const bool border = static_cast<bool>(object.try_as<controls::Border>());
    const bool grid = static_cast<bool>(object.try_as<controls::Grid>());
    const bool stack = static_cast<bool>(object.try_as<controls::StackPanel>());
    bool brush = property == "BorderBrush", padding = property == "Padding", thickness = property == "BorderThickness";
    if (control) {
        return brush ? controls::Control::BorderBrushProperty() : padding ? controls::Control::PaddingProperty()
               : thickness ? controls::Control::BorderThicknessProperty() : controls::Control::CornerRadiusProperty();
    }
    if (border) {
        return brush ? controls::Border::BorderBrushProperty() : padding ? controls::Border::PaddingProperty()
               : thickness ? controls::Border::BorderThicknessProperty() : controls::Border::CornerRadiusProperty();
    }
    if (grid) {
        return brush ? controls::Grid::BorderBrushProperty() : padding ? controls::Grid::PaddingProperty()
               : thickness ? controls::Grid::BorderThicknessProperty() : controls::Grid::CornerRadiusProperty();
    }
    if (stack) {
        return brush ? controls::StackPanel::BorderBrushProperty() : padding ? controls::StackPanel::PaddingProperty()
               : thickness ? controls::StackPanel::BorderThicknessProperty()
                           : controls::StackPanel::CornerRadiusProperty();
    }
    return nullptr;
}

// The text properties written as the name of an enumeration member (theme::ValueKind::Keyword), indexed by the
// member's value: TextAlignment's Start and End are Left and Right under other names. Empty where no member is.
struct TextEnum {
    std::string_view property;
    std::array<std::string_view, 5> names;
};

constexpr std::array<TextEnum, 4> kTextEnums{{
    {"TextAlignment", {"Center", "Left", "Right", "Justify", "DetectFromContent"}},
    {"TextTrimming", {"None", "CharacterEllipsis", "WordEllipsis", "Clip", ""}},
    {"TextWrapping", {"", "NoWrap", "Wrap", "WrapWholeWords", ""}},
    {"FontStyle", {"Normal", "Oblique", "Italic", "", ""}},
}};

const TextEnum* FindTextEnum(std::string_view property) {
    for (const TextEnum& entry : kTextEnums) {
        if (entry.property == property) return &entry;
    }
    return nullptr;
}

// FontStyle of a Control or a TextBlock, the others of a TextBlock only. Nothing when the element has no such value.
std::optional<std::int32_t> ReadTextEnum(const xaml::DependencyObject& object, std::string_view property) {
    auto text = object.try_as<controls::TextBlock>();
    if (property == "FontStyle") {
        if (auto control = object.try_as<controls::Control>()) return static_cast<std::int32_t>(control.FontStyle());
        if (text) return static_cast<std::int32_t>(text.FontStyle());
        return std::nullopt;
    }
    if (!text) return std::nullopt;
    if (property == "TextAlignment") return static_cast<std::int32_t>(text.TextAlignment());
    if (property == "TextTrimming") return static_cast<std::int32_t>(text.TextTrimming());
    return static_cast<std::int32_t>(text.TextWrapping());
}

bool WriteTextEnum(const xaml::DependencyObject& object, std::string_view property, std::int32_t value) {
    auto text = object.try_as<controls::TextBlock>();
    if (property == "FontStyle") {
        auto style = static_cast<winrt::Windows::UI::Text::FontStyle>(value);
        if (auto control = object.try_as<controls::Control>()) {
            control.FontStyle(style);
            return true;
        }
        if (text) text.FontStyle(style);
        return static_cast<bool>(text);
    }
    if (!text) return false;
    if (property == "TextAlignment") text.TextAlignment(static_cast<xaml::TextAlignment>(value));
    else if (property == "TextTrimming") text.TextTrimming(static_cast<xaml::TextTrimming>(value));
    else text.TextWrapping(static_cast<xaml::TextWrapping>(value));
    return true;
}

// CharacterSpacing and FontStyle on a Control or a TextBlock, the rest of the text properties on a TextBlock.
xaml::DependencyProperty OwnTextProperty(const xaml::DependencyObject& object, std::string_view property) {
    if (object.try_as<controls::Control>()) {
        if (property == "CharacterSpacing") return controls::Control::CharacterSpacingProperty();
        if (property == "FontStyle") return controls::Control::FontStyleProperty();
        return nullptr;
    }
    if (!object.try_as<controls::TextBlock>()) return nullptr;
    if (property == "CharacterSpacing") return controls::TextBlock::CharacterSpacingProperty();
    if (property == "FontStyle") return controls::TextBlock::FontStyleProperty();
    if (property == "LineHeight") return controls::TextBlock::LineHeightProperty();
    if (property == "TextAlignment") return controls::TextBlock::TextAlignmentProperty();
    if (property == "TextTrimming") return controls::TextBlock::TextTrimmingProperty();
    return controls::TextBlock::TextWrappingProperty();
}

}  // namespace

bool IsTextProperty(std::string_view property) {
    return property == "CharacterSpacing" || property == "LineHeight" || FindTextEnum(property) != nullptr;
}

std::optional<th::Value> ReadTextProperty(const xaml::DependencyObject& object, std::string_view property) {
    auto text = object.try_as<controls::TextBlock>();
    if (property == "CharacterSpacing") {
        if (auto control = object.try_as<controls::Control>()) {
            return th::Value{th::ValueKind::Number, std::to_string(control.CharacterSpacing())};
        }
        if (text) return th::Value{th::ValueKind::Number, std::to_string(text.CharacterSpacing())};
        return std::nullopt;
    }
    if (property == "LineHeight") {
        if (text) return th::Value{th::ValueKind::Length, th::FormatNumber(text.LineHeight())};
        return std::nullopt;
    }
    const TextEnum* entry = FindTextEnum(property);
    std::optional<std::int32_t> member = entry ? ReadTextEnum(object, property) : std::nullopt;
    if (!member || *member < 0 || *member >= static_cast<std::int32_t>(entry->names.size())) return std::nullopt;
    std::string_view name = entry->names[static_cast<size_t>(*member)];
    if (name.empty()) return std::nullopt;
    return th::Value{th::ValueKind::Keyword, std::string(name)};
}

bool WriteTextProperty(const xaml::DependencyObject& object, std::string_view property, const th::Value& value) {
    auto text = object.try_as<controls::TextBlock>();
    if (property == "CharacterSpacing" && value.kind == th::ValueKind::Number) {
        auto spacing = static_cast<std::int32_t>(std::lround(std::stod(value.text)));
        if (auto control = object.try_as<controls::Control>()) {
            control.CharacterSpacing(spacing);
            return true;
        }
        if (text) text.CharacterSpacing(spacing);
        return static_cast<bool>(text);
    }
    if (property == "LineHeight" && value.kind == th::ValueKind::Length) {
        double height = ParseLength(value.text);
        if (!text || !(height >= 0)) return false;  // XAML throws on a negative or NaN height
        text.LineHeight(height);
        return true;
    }
    const TextEnum* entry = FindTextEnum(property);
    if (!entry || value.kind != th::ValueKind::Keyword || value.text.empty()) return false;
    auto member = std::find(entry->names.begin(), entry->names.end(), std::string_view(value.text));
    if (member == entry->names.end()) return false;
    return WriteTextEnum(object, property, static_cast<std::int32_t>(member - entry->names.begin()));
}

// The dependency property behind a theme property on this element, checked in the same order as ReadRaw and WriteRaw.
// Null for a property that is not one value on the element (a transform, the order, an attached Grid value).
xaml::DependencyProperty OwnProperty(const xaml::DependencyObject& object, std::string_view property) {
    const bool control = static_cast<bool>(object.try_as<controls::Control>());
    const bool text = static_cast<bool>(object.try_as<controls::TextBlock>());
    if (property == "Background") {
        if (control) return controls::Control::BackgroundProperty();
        if (object.try_as<controls::Panel>()) return controls::Panel::BackgroundProperty();
        if (object.try_as<controls::Border>()) return controls::Border::BackgroundProperty();
    } else if (property == "Foreground") {
        if (control) return controls::Control::ForegroundProperty();
        if (text) return controls::TextBlock::ForegroundProperty();
        if (object.try_as<controls::IconElement>()) return controls::IconElement::ForegroundProperty();
        if (object.try_as<controls::ContentPresenter>()) return controls::ContentPresenter::ForegroundProperty();
    } else if (IsSelectionBrush(property)) {
        if (!object.try_as<primitives::ListViewItemPresenter>()) return nullptr;
        using Presenter = primitives::ListViewItemPresenter;
        if (property == "PointerOverBackground") return Presenter::PointerOverBackgroundProperty();
        if (property == "PressedBackground") return Presenter::PressedBackgroundProperty();
        if (property == "SelectedBackground") return Presenter::SelectedBackgroundProperty();
        if (property == "SelectedPointerOverBackground") return Presenter::SelectedPointerOverBackgroundProperty();
        return Presenter::SelectedPressedBackgroundProperty();
    } else if (property == "BorderBrush" || property == "Padding" || property == "BorderThickness" ||
               property == "CornerRadius") {
        return OwnBoxProperty(object, property);
    } else if (property == "FontSize" || property == "FontFamily" || property == "FontWeight") {
        bool size = property == "FontSize", family = property == "FontFamily";
        if (control) {
            return size ? controls::Control::FontSizeProperty() : family ? controls::Control::FontFamilyProperty()
                                                                         : controls::Control::FontWeightProperty();
        }
        if (text) {
            return size ? controls::TextBlock::FontSizeProperty() : family ? controls::TextBlock::FontFamilyProperty()
                                                                           : controls::TextBlock::FontWeightProperty();
        }
    } else if (IsTextProperty(property)) {
        return OwnTextProperty(object, property);
    } else if (object.try_as<xaml::FrameworkElement>()) {
        if (property == "Margin") return xaml::FrameworkElement::MarginProperty();
        if (property == "Width") return xaml::FrameworkElement::WidthProperty();
        if (property == "Height") return xaml::FrameworkElement::HeightProperty();
        if (property == "MinWidth") return xaml::FrameworkElement::MinWidthProperty();
        if (property == "MaxWidth") return xaml::FrameworkElement::MaxWidthProperty();
        if (property == "MinHeight") return xaml::FrameworkElement::MinHeightProperty();
        if (property == "MaxHeight") return xaml::FrameworkElement::MaxHeightProperty();
        if (property == "Opacity") return xaml::UIElement::OpacityProperty();
        if (property == "Visibility") return xaml::UIElement::VisibilityProperty();
        // Inherited: read after an ancestor was flipped, the child's direction was the theme's, and putting it back
        // froze the theme's mirror on every theme that followed.
        if (property == "FlowDirection") return xaml::FrameworkElement::FlowDirectionProperty();
        if (property == "RequestedTheme") return xaml::FrameworkElement::RequestedThemeProperty();
    }
    return nullptr;
}

media::Brush SelectionBrush(const primitives::ListViewItemPresenter& row, std::string_view property) {
    if (property == "PointerOverBackground") return row.PointerOverBackground();
    if (property == "PressedBackground") return row.PressedBackground();
    if (property == "SelectedBackground") return row.SelectedBackground();
    if (property == "SelectedPointerOverBackground") return row.SelectedPointerOverBackground();
    return row.SelectedPressedBackground();
}

void SetSelectionBrush(const primitives::ListViewItemPresenter& row, std::string_view property, const media::Brush& brush) {
    if (property == "PointerOverBackground") row.PointerOverBackground(brush);
    else if (property == "PressedBackground") row.PressedBackground(brush);
    else if (property == "SelectedBackground") row.SelectedBackground(brush);
    else if (property == "SelectedPointerOverBackground") row.SelectedPointerOverBackground(brush);
    else row.SelectedPressedBackground(brush);
}

// The order of a corner-radius value (properties.cpp): bottom-left, top-left, top-right, bottom-right.
std::string CornerText(const xaml::CornerRadius& value) {
    return th::FormatNumber(value.BottomLeft) + "," + th::FormatNumber(value.TopLeft) + "," +
           th::FormatNumber(value.TopRight) + "," + th::FormatNumber(value.BottomRight);
}

// A theme template's Border takes the control's Padding and BorderThickness and refuses a negative side at the next
// layout, where WinUI stops Apple Music instead of throwing. The search box's own buttons have a padding of 0,0,-2,0.
bool NoNegativeSide(const xaml::Thickness& sides) {
    return sides.Left >= 0 && sides.Top >= 0 && sides.Right >= 0 && sides.Bottom >= 0;  // false for NaN too
}

// "x1,y1,x2,y2;#AARRGGBB@offset;..." (theme::ValueKind::Gradient) to a LinearGradientBrush. Null for another text.
media::Brush GradientFrom(const std::string& text) {
    std::vector<std::string> parts = Split(text, ';');
    if (parts.size() < 3) return nullptr;
    std::vector<double> points = Numbers(parts[0], ',');
    if (points.size() != 4) return nullptr;

    media::LinearGradientBrush brush;
    brush.StartPoint(winrt::Windows::Foundation::Point{static_cast<float>(points[0]), static_cast<float>(points[1])});
    brush.EndPoint(winrt::Windows::Foundation::Point{static_cast<float>(points[2]), static_cast<float>(points[3])});
    for (size_t i = 1; i < parts.size(); i++) {
        size_t at = parts[i].find('@');
        if (at == std::string::npos) return nullptr;
        std::optional<ColorValue> color = ColorFromText(parts[i].substr(0, at));
        if (!color) return nullptr;
        media::GradientStop stop;
        stop.Color(*color);
        stop.Offset(std::stod(parts[i].substr(at + 1)));
        brush.GradientStops().Append(stop);
    }
    return brush;
}

// "#AARRGGBB;opacity" (theme::ValueKind::Acrylic) to the in-app AcrylicBrush of WinUI 3. Null for another text.
media::Brush AcrylicFrom(const std::string& text) {
    std::vector<std::string> parts = Split(text, ';');
    if (parts.size() != 2) return nullptr;
    std::optional<ColorValue> tint = ColorFromText(parts[0]);
    if (!tint) return nullptr;
    media::AcrylicBrush acrylic;
    acrylic.TintColor(*tint);
    acrylic.TintOpacity(std::stod(parts[1]));
    acrylic.FallbackColor(*tint);  // shown when Windows turns the effect off (transparency off, battery saver)
    return acrylic;
}

// "path|Stretch" (theme::ValueKind::Image, the stretch optional) to an ImageBrush on the file.
media::Brush ImageFrom(const std::string& text) {
    std::string path = text;
    std::string stretch;
    if (size_t bar = path.find('|'); bar != std::string::npos) {
        stretch = path.substr(bar + 1);
        path.resize(bar);
    }
    media::ImageBrush image;
    image.ImageSource(media::Imaging::BitmapImage(winrt::Windows::Foundation::Uri(FileUri(path))));
    if (stretch == "UniformToFill") image.Stretch(media::Stretch::UniformToFill);
    else if (stretch == "Uniform") image.Stretch(media::Stretch::Uniform);
    else if (stretch == "Fill") image.Stretch(media::Stretch::Fill);
    return image;
}

std::string AlignmentText(xaml::HorizontalAlignment value) {
    switch (value) {
        case xaml::HorizontalAlignment::Left: return "Left";
        case xaml::HorizontalAlignment::Center: return "Center";
        case xaml::HorizontalAlignment::Right: return "Right";
        default: return "Stretch";
    }
}

std::string AlignmentText(xaml::VerticalAlignment value) {
    switch (value) {
        case xaml::VerticalAlignment::Top: return "Top";
        case xaml::VerticalAlignment::Center: return "Center";
        case xaml::VerticalAlignment::Bottom: return "Bottom";
        default: return "Stretch";
    }
}

double ParseLength(const std::string& text) {
    if (text == "NaN") return std::numeric_limits<double>::quiet_NaN();
    return std::stod(text);
}

std::string ShortName(const std::string& fullType) {
    size_t dot = fullType.rfind('.');
    return dot == std::string::npos ? fullType : fullType.substr(dot + 1);
}

// The position of `object` in the list that holds it and can reorder it (theme::kOrderProperty). A NavigationViewItem
// lives in the MenuItems of the nearest NavigationView; another element in the Children of its direct parent Panel
// (not an ItemsRepeater, whose children it manages itself). With `target`, the element moves there first. Nothing when
// no such list holds it, for example items generated from MenuItemsSource or footer items. To check on the PC: moving
// the selected entry may change the selection.
std::optional<std::uint32_t> ItemPosition(const xaml::DependencyObject& object, std::optional<std::uint32_t> target) {
    if (object.try_as<controls::NavigationViewItemBase>()) {
        for (xaml::DependencyObject parent = media::VisualTreeHelper::GetParent(object); parent;
             parent = media::VisualTreeHelper::GetParent(parent)) {
            auto navigation = parent.try_as<controls::NavigationView>();
            if (!navigation) continue;
            auto items = navigation.MenuItems();
            std::uint32_t index = 0;
            if (!items.IndexOf(object, index)) return std::nullopt;
            if (!target) return index;
            if (*target >= items.Size()) return std::nullopt;
            items.RemoveAt(index);
            items.InsertAt(*target, object);
            return *target;
        }
        return std::nullopt;
    }

    xaml::DependencyObject parent = media::VisualTreeHelper::GetParent(object);
    if (!parent) return std::nullopt;
    auto panel = parent.try_as<controls::Panel>();
    auto element = object.try_as<xaml::UIElement>();
    if (!panel || !element || parent.try_as<controls::ItemsRepeater>()) return std::nullopt;
    auto children = panel.Children();
    std::uint32_t index = 0;
    if (!children.IndexOf(element, index)) return std::nullopt;
    // A ::before or ::after image does not count: the theme's positions are among Apple Music's own children.
    std::vector<std::uint32_t> own;
    for (std::uint32_t i = 0; i < children.Size(); i++) {
        if (!IsDecor(children.GetAt(i))) own.push_back(i);
    }
    auto position = static_cast<std::uint32_t>(std::find(own.begin(), own.end(), index) - own.begin());
    if (!target) return position;
    if (*target >= own.size()) return std::nullopt;
    children.Move(index, own[*target]);
    return *target;
}

namespace {

// The ::before and ::after images carry these names (DecorImageFrom), so the walk and the positions can leave them out.
constexpr std::wstring_view kDecorPrefix = L"AmpifyDecor";

}  // namespace

bool IsDecor(const xaml::DependencyObject& object) {
    auto element = object.try_as<xaml::FrameworkElement>();
    return element && std::wstring_view(element.Name()).starts_with(kDecorPrefix);
}

bool TakesDecor(const controls::Panel& panel) {
    return panel && !panel.IsItemsHost() && !panel.try_as<controls::ItemsRepeater>();
}

// BitmapImage plays an animated GIF in an Image, which an ImageBrush would show still. The image never takes the
// pointer and stays out of the accessibility tree: it is decoration.
controls::Image DecorImageFrom(const std::string& text, th::PseudoElement slot) {
    size_t bar = text.find('|');
    std::string stretch = bar == std::string::npos ? std::string() : text.substr(bar + 1);
    media::Imaging::BitmapImage bitmap;
    bitmap.AutoPlay(true);
    bitmap.UriSource(winrt::Windows::Foundation::Uri(FileUri(text.substr(0, bar))));
    controls::Image image;
    image.Name(slot == th::PseudoElement::Before ? L"AmpifyDecorBefore" : L"AmpifyDecorAfter");
    image.IsHitTestVisible(false);
    image.Stretch(stretch == "UniformToFill" ? media::Stretch::UniformToFill
                  : stretch == "Fill"        ? media::Stretch::Fill
                                             : media::Stretch::Uniform);
    xaml::Automation::AutomationProperties::SetAccessibilityView(image, xaml::Automation::Peers::AccessibilityView::Raw);
    image.Source(bitmap);
    return image;
}

bool KeepDecorInPlace(const controls::UIElementCollection& children, const xaml::UIElement& image,
                      th::PseudoElement slot) {
    std::uint32_t index = 0;
    if (!children.IndexOf(image, index)) return false;
    std::uint32_t wanted = slot == th::PseudoElement::Before ? 0 : children.Size() - 1;
    if (index != wanted) children.Move(index, wanted);
    return true;
}

// True when the key is in the dictionary itself, not only in its merged or theme dictionaries (HasKey looks there too).
bool OwnsKey(const xaml::ResourceDictionary& resources, const IInspectable& name) {
    for (const auto& pair : resources) {
        if (pair.Key() == name) return true;
        auto a = pair.Key().try_as<winrt::Windows::Foundation::IReference<winrt::hstring>>();
        auto b = name.try_as<winrt::Windows::Foundation::IReference<winrt::hstring>>();
        if (a && b && a.Value() == b.Value()) return true;
    }
    return false;
}

namespace {

// What a state writes, as the element's property and, for a colour animation, the brush's own property:
// "(Border.Background).(SolidColorBrush.Color)" is {"Background", "Color"}; "Background" and "(Grid.Background)" are
// {"Background", ""}.
struct StatePath {
    std::string property;
    std::string sub;
};

StatePath SplitStatePath(const winrt::hstring& path) {
    std::vector<std::string> segments{""};
    int depth = 0;
    for (char c : Utf8(path)) {
        if (c == '(') depth++;
        else if (c == ')') depth--;
        else if (c == '.' && depth == 0) segments.emplace_back();
        else segments.back() += c;
    }
    for (std::string& segment : segments) {
        size_t dot = segment.rfind('.');  // "Border.Background" inside parentheses: the owner type, then the property
        if (dot != std::string::npos) segment = segment.substr(dot + 1);
    }
    return {segments[0], segments.size() > 1 ? segments[1] : std::string()};
}

std::string StatePathText(const StatePath& path) {
    return path.sub.empty() ? path.property : path.property + "." + path.sub;
}

}  // namespace

// For the inspector: the states an element's visual state groups hold and what each one writes, such as
// "CommonStates: PointerOver (Root.Background), Pressed (Root.Background, Content.Foreground)". A theme author sees
// which properties Apple Music changes under the pointer, while pressed, checked or selected.
std::string DescribeStates(const xaml::FrameworkElement& element) {
    std::string text;
    for (const xaml::VisualStateGroup& group : xaml::VisualStateManager::GetVisualStateGroups(element)) {
        std::string states;
        for (const xaml::VisualState& state : group.States()) {
            std::string writes;
            auto add = [&](const std::string& target, const StatePath& path) {
                writes += (writes.empty() ? "" : ", ") + target + "." + StatePathText(path);
            };
            for (const xaml::SetterBase& base : state.Setters()) {
                auto setter = base.try_as<xaml::Setter>();
                if (!setter || !setter.Target() || !setter.Target().Path()) continue;
                IInspectable aim = setter.Target().Target();
                std::string target;
                if (auto object = aim.try_as<xaml::FrameworkElement>()) {
                    target = object.Name().empty() ? ShortName(Utf8(winrt::get_class_name(object))) : Utf8(object.Name());
                } else {
                    target = Utf8(winrt::unbox_value_or<winrt::hstring>(aim, L"?"));
                }
                add(target, SplitStatePath(setter.Target().Path().Path()));
            }
            if (animation::Storyboard board = state.Storyboard()) {
                for (const animation::Timeline& child : board.Children()) {
                    add(Utf8(animation::Storyboard::GetTargetName(child)),
                        SplitStatePath(animation::Storyboard::GetTargetProperty(child)));
                }
            }
            if (writes.empty()) continue;
            states += (states.empty() ? "" : ", ") + Utf8(state.Name()) + " (" + writes + ")";
        }
        if (states.empty()) continue;
        text += (text.empty() ? "" : "; ") + Utf8(group.Name()) + ": " + states;
    }
    return text;
}

// An element's {ThemeResource} brushes are resolved when it is first shown, and again on a change of mode: the other
// mode for a moment, then the one it had, makes this element alone read them again. Nothing is drawn in between.
// Doing it for the whole window stopped Apple Music for half a second.
void ReadThemeAgain(const xaml::FrameworkElement& element) {
    xaml::ElementTheme requested = element.RequestedTheme();
    bool dark = element.ActualTheme() == xaml::ElementTheme::Dark;
    element.RequestedTheme(dark ? xaml::ElementTheme::Light : xaml::ElementTheme::Dark);
    element.RequestedTheme(requested);
}

// The open popups (Ampify's pages and panels, Apple Music's menus) are not under the window's content, so a change of
// mode there does not reach them: without this, a page kept the accent of the theme before.
// A popup that closes meanwhile is skipped: the change of mode and the caption still go through.
void ReadPopupsAgain(const xaml::XamlRoot& root) noexcept {
    try {
        if (!root) return;
        for (const primitives::Popup& popup : media::VisualTreeHelper::GetOpenPopupsForXamlRoot(root)) {
            try {
                if (auto child = popup.Child().try_as<xaml::FrameworkElement>()) ReadThemeAgain(child);
            } catch (...) {
            }
        }
    } catch (...) {
    }
}

}  // namespace ampify::win::xaml_detail

#endif  // AMPIFY_WITH_XAML
