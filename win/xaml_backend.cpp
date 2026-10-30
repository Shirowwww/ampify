// NOT COMPILED IN THE CURRENT BUILD: see xaml_backend.hpp. Written against the C++/WinRT projection of the Windows App SDK.
// The API names below are the ones documented for WinUI 3; each one is listed in docs/tests/M8-themes.md to check on the PC.

#if defined(AMK_WITH_XAML)

#include "xaml_backend.hpp"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>
#include <algorithm>
#include <cmath>
#include <functional>
#include <set>

#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.Foundation.Numerics.h>

#include "../core/theme/properties.hpp"
#include "xaml_host.hpp"

namespace amk::win {

namespace {

namespace th = amk::theme;
namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = xaml::Controls;
namespace media = xaml::Media;
using winrt::Windows::Foundation::IInspectable;
using ColorValue = winrt::Windows::UI::Color;

std::string Utf8(winrt::hstring text) {
    return winrt::to_string(text);
}

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

// `file:///C:/...` for a path with '/' separators.
winrt::hstring FileUri(const std::string& path) {
    return winrt::to_hstring("file:///" + path);
}

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

// The order of a corner-radius value (properties.cpp): bottom-left, top-left, top-right, bottom-right.
std::string CornerText(const xaml::CornerRadius& value) {
    return th::FormatNumber(value.BottomLeft) + "," + th::FormatNumber(value.TopLeft) + "," +
           th::FormatNumber(value.TopRight) + "," + th::FormatNumber(value.BottomRight);
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

std::string Lowered(std::string_view text) {
    std::string result(text);
    for (char& c : result) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return result;
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
    if (!target) return index;
    if (*target >= children.Size()) return std::nullopt;
    children.Move(index, *target);
    return *target;
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

}  // namespace

bool XamlTreeBackend::Attach() {
    if (*m_closed) return false;
    xaml::Window window = AppWindow(L"MainWindow");
    if (!window) return false;
    m_window = winrt::make_weak(window);
    // Walking or writing the tree while Apple Music closes crashed it in Microsoft.UI.Xaml.dll (bench, 30/10).
    std::shared_ptr<bool> closed = m_closed;
    window.Closed([closed](IInspectable const&, xaml::WindowEventArgs const&) noexcept { *closed = true; });
    return true;
}

void XamlTreeBackend::SetAppMode(std::optional<bool> dark) {
    if (*m_closed) return;
    bool refresh = std::exchange(m_resourcesDirty, false);
    xaml::FrameworkElement root = m_modeRoot.get();
    if (!root) {
        if (xaml::Window window = m_window.get()) root = window.Content().try_as<xaml::FrameworkElement>();
        if (!root) return;
        if (dark) {
            m_modeRoot = winrt::make_weak(root);
            m_modeOriginal = root.RequestedTheme();
        }
    }
    xaml::ElementTheme wanted = root.RequestedTheme();
    if (!dark) {
        if (m_modeOriginal) wanted = *m_modeOriginal;
        m_modeRoot = nullptr;
        m_modeOriginal.reset();
    } else {
        wanted = *dark ? xaml::ElementTheme::Dark : xaml::ElementTheme::Light;
    }
    if (refresh) {
        // {ThemeResource} is resolved again on a change of mode only: the other mode for a moment, then the wanted one.
        bool shownDark = root.ActualTheme() == xaml::ElementTheme::Dark;
        root.RequestedTheme(shownDark ? xaml::ElementTheme::Light : xaml::ElementTheme::Dark);
    }
    if (refresh || root.RequestedTheme() != wanted) root.RequestedTheme(wanted);
}

xaml::DependencyObject XamlTreeBackend::Resolve(theme::NodeId id) const {
    auto it = m_elements.find(id);
    if (it != m_elements.end()) return it->second.get();  // null when the element has been destroyed since the snapshot
    // An element followed for :hover may be out of the last snapshot (scrolled away, then back) and still alive.
    auto hover = m_hoverHandlers.find(id);
    if (hover != m_hoverHandlers.end()) {
        if (xaml::UIElement element = hover->second.element.get()) return element;
    }
    return nullptr;
}

theme::Value XamlTreeBackend::Keep(IInspectable object) {
    void* identity = object ? winrt::get_abi(object.as<winrt::Windows::Foundation::IUnknown>()) : nullptr;
    auto known = m_keptTokens.find(identity);
    if (known != m_keptTokens.end()) return {theme::ValueKind::Opaque, "handle:" + std::to_string(known->second)};

    std::uint64_t token = m_nextToken++;
    m_kept[token] = object;  // holds the object, so its identity cannot be reused while the token exists
    m_keptTokens[identity] = token;
    return {theme::ValueKind::Opaque, "handle:" + std::to_string(token)};
}

void XamlTreeBackend::Remember(theme::NodeId id, std::string_view property, const theme::Value& value, IInspectable object) {
    Written written{value, std::move(object), {}};
    if (xaml::DependencyObject element = Resolve(id)) written.element = winrt::make_weak(element);
    m_written[std::make_pair(id, std::string(property))] = std::move(written);
}

std::optional<theme::Value> XamlTreeBackend::Recognize(theme::NodeId id, std::string_view property,
                                                       const IInspectable& object) const {
    auto it = m_written.find(std::make_pair(id, std::string(property)));
    if (it == m_written.end() || it->second.object != object) return std::nullopt;
    return it->second.value;
}

void XamlTreeBackend::Forget(theme::NodeId id, std::string_view property) {
    m_written.erase(std::make_pair(id, std::string(property)));
}

IInspectable XamlTreeBackend::Restore(const theme::Value& token) const {
    std::uint64_t number = std::stoull(token.text.substr(std::string("handle:").size()));
    auto it = m_kept.find(number);
    return it == m_kept.end() ? IInspectable{nullptr} : it->second;
}

std::vector<theme::NodeInfo> XamlTreeBackend::Snapshot() {
    std::vector<theme::NodeInfo> nodes;
    m_elements.clear();

    if (*m_closed) return nodes;
    xaml::Window window = m_window.get();  // the main window, from AMP.Utils (Attach)
    if (!window) window = AppWindow(L"MainWindow");
    if (!window) return nodes;
    xaml::UIElement content = window.Content();
    if (!content) return nodes;

    // Preorder walk with an explicit stack. A parent is added before its children, as the engine expects.
    struct Pending {
        xaml::DependencyObject object;
        int parent;
        int depth;
    };
    std::vector<Pending> stack{{content, -1, 0}};
    while (!stack.empty()) {
        Pending item = stack.back();
        stack.pop_back();

        theme::NodeInfo node;
        node.id = reinterpret_cast<std::uint64_t>(winrt::get_abi(item.object));
        node.fullType = Utf8(winrt::get_class_name(item.object));
        node.type = ShortName(node.fullType);
        if (auto element = item.object.try_as<xaml::FrameworkElement>()) node.name = Utf8(element.Name());
        if (auto text = item.object.try_as<controls::TextBlock>()) node.icon = IsIconText(text.Text());
        node.parent = item.parent;
        node.depth = item.depth;

        int index = static_cast<int>(nodes.size());
        nodes.push_back(node);
        m_elements.emplace(node.id, winrt::weak_ref<xaml::DependencyObject>(item.object));

        int count = media::VisualTreeHelper::GetChildrenCount(item.object);
        for (int i = count - 1; i >= 0; i--) {
            stack.push_back({media::VisualTreeHelper::GetChild(item.object, i), index, item.depth + 1});
        }
    }

    // What the kit remembers of destroyed elements cannot be used any more: a long session stays small.
    for (auto it = m_written.begin(); it != m_written.end();) {
        if (it->second.element.get()) {
            ++it;
            continue;
        }
        m_translations.erase(it->first.first);
        it = m_written.erase(it);
    }
    for (auto it = m_hoverHandlers.begin(); it != m_hoverHandlers.end();) {
        if (it->second.element.get()) {
            ++it;
            continue;
        }
        m_hover.Forget(it->first);
        it = m_hoverHandlers.erase(it);
    }
    return nodes;
}

std::optional<theme::Value> XamlTreeBackend::Read(theme::NodeId id, std::string_view property) {
    return m_hover.Read(id, property, *this);
}

bool XamlTreeBackend::Write(theme::NodeId id, std::string_view property, const theme::Value& value) {
    return m_hover.Write(id, property, value, *this);
}

std::optional<theme::Value> XamlTreeBackend::ReadRaw(theme::NodeId id, std::string_view property) {
    xaml::DependencyObject object = Resolve(id);
    if (!object) return std::nullopt;

    // Apple Music's own brush is kept as the object itself: a colour would lose the brush's Opacity (its LCD is white
    // at low opacity, restored as plain white) and its theme dictionary. The colour rides along for the inspector.
    auto brush = [&](const media::Brush& value) -> theme::Value {
        if (!value) return Keep(nullptr);
        if (std::optional<theme::Value> known = Recognize(id, property, value)) return *known;
        theme::Value kept = Keep(value);
        if (auto solid = value.try_as<media::SolidColorBrush>()) {
            ColorValue color = solid.Color();
            color.A = static_cast<std::uint8_t>(std::lround(color.A * std::clamp(solid.Opacity(), 0.0, 1.0)));
            kept.text += ";" + ColorText(color);
        }
        return kept;
    };
    // A transform or a shadow: the value the kit wrote while the element still holds its object, a token otherwise.
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
        if (auto text = object.try_as<controls::TextBlock>()) return brush(text.Foreground());
    } else if (property == "BorderBrush") {
        if (auto control = object.try_as<controls::Control>()) return brush(control.BorderBrush());
        if (auto border = object.try_as<controls::Border>()) return brush(border.BorderBrush());
        if (auto grid = object.try_as<controls::Grid>()) return brush(grid.BorderBrush());
        if (auto stack = object.try_as<controls::StackPanel>()) return brush(stack.BorderBrush());
    } else if (property == "FontSize") {
        if (auto control = object.try_as<controls::Control>()) return theme::Value{theme::ValueKind::Length, th::FormatNumber(control.FontSize())};
        if (auto text = object.try_as<controls::TextBlock>()) return theme::Value{theme::ValueKind::Length, th::FormatNumber(text.FontSize())};
    } else if (property == "FontFamily") {
        // A font of the theme reads back as the file URI and family it was written with (FontFamily.Source).
        if (auto control = object.try_as<controls::Control>()) return theme::Value{theme::ValueKind::Text, Utf8(control.FontFamily().Source())};
        if (auto text = object.try_as<controls::TextBlock>()) return theme::Value{theme::ValueKind::Text, Utf8(text.FontFamily().Source())};
    } else if (property == "FontWeight") {
        if (auto control = object.try_as<controls::Control>()) return number(control.FontWeight().Weight);
        if (auto text = object.try_as<controls::TextBlock>()) return number(text.FontWeight().Weight);
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
    } else if (property == "Visibility") {
        if (auto element = object.try_as<xaml::UIElement>()) {
            return theme::Value{theme::ValueKind::Visibility, element.Visibility() == xaml::Visibility::Collapsed ? "Collapsed" : "Visible"};
        }
    } else if (property == "Opacity") {
        if (auto element = object.try_as<xaml::UIElement>()) return number(element.Opacity());
    } else if (property == "Width" || property == "Height") {
        if (auto element = object.try_as<xaml::FrameworkElement>()) {
            double value = property == "Width" ? element.Width() : element.Height();
            return theme::Value{theme::ValueKind::Length, th::FormatNumber(value)};
        }
    } else if (property == "FlowDirection") {
        if (auto element = object.try_as<xaml::FrameworkElement>()) {
            bool rtl = element.FlowDirection() == xaml::FlowDirection::RightToLeft;
            return theme::Value{theme::ValueKind::Keyword, rtl ? "RightToLeft" : "LeftToRight"};
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
        if (auto element = object.try_as<xaml::UIElement>()) return kept(element.RenderTransform());
    } else if (property == "RenderTransformOrigin") {
        if (auto element = object.try_as<xaml::UIElement>()) {
            winrt::Windows::Foundation::Point origin = element.RenderTransformOrigin();
            return theme::Value{theme::ValueKind::Point, FractionText(origin.X) + "," + FractionText(origin.Y)};
        }
    } else if (property == "Shadow") {
        if (auto element = object.try_as<xaml::UIElement>()) return kept(element.Shadow());
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
    }
    return std::nullopt;
}

bool XamlTreeBackend::WriteRaw(theme::NodeId id, std::string_view property, const theme::Value& value) {
    xaml::DependencyObject object = Resolve(id);
    if (!object) return false;

    if (property == "Background" || property == "Foreground" || property == "BorderBrush") {
        std::optional<media::Brush> target;
        bool made = false;  // an object made from the value, remembered so that it reads back as the value
        if (value.kind == theme::ValueKind::Color) {
            std::optional<ColorValue> color = ColorFromText(value.text);
            if (!color) return false;
            target = media::SolidColorBrush(*color);
            made = true;
        } else if (value.kind == theme::ValueKind::Image) {
            target = ImageFrom(value.text);
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
        if (property == "Background") {
            if (auto control = object.try_as<controls::Control>()) { control.Background(*target); written = true; }
            else if (auto panel = object.try_as<controls::Panel>()) { panel.Background(*target); written = true; }
            else if (auto border = object.try_as<controls::Border>()) { border.Background(*target); written = true; }
        } else if (property == "Foreground") {
            if (auto control = object.try_as<controls::Control>()) { control.Foreground(*target); written = true; }
            else if (auto text = object.try_as<controls::TextBlock>()) { text.Foreground(*target); written = true; }
        } else {
            if (auto control = object.try_as<controls::Control>()) { control.BorderBrush(*target); written = true; }
            else if (auto border = object.try_as<controls::Border>()) { border.BorderBrush(*target); written = true; }
            else if (auto grid = object.try_as<controls::Grid>()) { grid.BorderBrush(*target); written = true; }
            else if (auto stack = object.try_as<controls::StackPanel>()) { stack.BorderBrush(*target); written = true; }
        }
        if (!written) return false;
        if (made) {
            Remember(id, property, value, *target);
        } else {
            Forget(id, property);
        }
        return true;
    }

    if (property == "FontSize" && value.kind == theme::ValueKind::Length) {
        double size = ParseLength(value.text);
        if (auto control = object.try_as<controls::Control>()) { control.FontSize(size); return true; }
        if (auto text = object.try_as<controls::TextBlock>()) { text.FontSize(size); return true; }
        return false;
    }

    if (property == "FontFamily" && value.kind == theme::ValueKind::Text) {
        // A font of the theme is "file:///C:/.../fonts/x.ttf#Family" (properties.cpp), as images are given by path.
        media::FontFamily family(winrt::to_hstring(value.text));
        if (auto control = object.try_as<controls::Control>()) { control.FontFamily(family); return true; }
        if (auto text = object.try_as<controls::TextBlock>()) { text.FontFamily(family); return true; }
        return false;
    }

    if (property == "FontWeight" && value.kind == theme::ValueKind::Number) {
        auto weight = static_cast<std::uint16_t>(std::stod(value.text));
        if (auto control = object.try_as<controls::Control>()) { control.FontWeight(winrt::Windows::UI::Text::FontWeight{weight}); return true; }
        if (auto text = object.try_as<controls::TextBlock>()) { text.FontWeight(winrt::Windows::UI::Text::FontWeight{weight}); return true; }
        return false;
    }

    if ((property == "Margin" || property == "Padding" || property == "BorderThickness") && value.kind == theme::ValueKind::Thickness) {
        std::vector<double> sides = Numbers(value.text, ',');
        if (sides.size() != 4) return false;
        xaml::Thickness thickness{sides[0], sides[1], sides[2], sides[3]};  // left, top, right, bottom
        if (property == "Margin") {
            if (auto element = object.try_as<xaml::FrameworkElement>()) { element.Margin(thickness); return true; }
        } else if (property == "Padding") {
            if (auto control = object.try_as<controls::Control>()) { control.Padding(thickness); return true; }
            if (auto border = object.try_as<controls::Border>()) { border.Padding(thickness); return true; }
            if (auto grid = object.try_as<controls::Grid>()) { grid.Padding(thickness); return true; }
            if (auto stack = object.try_as<controls::StackPanel>()) { stack.Padding(thickness); return true; }
        } else {
            if (auto control = object.try_as<controls::Control>()) { control.BorderThickness(thickness); return true; }
            if (auto border = object.try_as<controls::Border>()) { border.BorderThickness(thickness); return true; }
            if (auto grid = object.try_as<controls::Grid>()) { grid.BorderThickness(thickness); return true; }
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
        if (auto grid = object.try_as<controls::Grid>()) { grid.CornerRadius(radius); return true; }
        if (auto stack = object.try_as<controls::StackPanel>()) { stack.CornerRadius(radius); return true; }
        return false;
    }

    if ((property == "Width" || property == "Height") && value.kind == theme::ValueKind::Length) {
        double length = ParseLength(value.text);
        if (auto element = object.try_as<xaml::FrameworkElement>()) {
            if (property == "Width") element.Width(length); else element.Height(length);
            return true;
        }
        return false;
    }

    if (property == "Opacity" && value.kind == theme::ValueKind::Number) {
        if (auto element = object.try_as<xaml::UIElement>()) { element.Opacity(std::stod(value.text)); return true; }
        return false;
    }

    if (property == "Visibility" && value.kind == theme::ValueKind::Visibility) {
        if (auto element = object.try_as<xaml::UIElement>()) {
            element.Visibility(Lowered(value.text) == "collapsed" ? xaml::Visibility::Collapsed : xaml::Visibility::Visible);
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

    if (property == "RenderTransform") {
        auto element = object.try_as<xaml::UIElement>();
        if (!element) return false;
        if (value.kind == theme::ValueKind::Transform) {
            std::vector<double> parts = Numbers(value.text, ',');
            if (parts.size() != 5) return false;
            media::CompositeTransform transform;
            transform.ScaleX(parts[0]);
            transform.ScaleY(parts[1]);
            transform.Rotation(parts[2]);
            transform.TranslateX(parts[3]);
            transform.TranslateY(parts[4]);
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
        element.RenderTransformOrigin(winrt::Windows::Foundation::Point{static_cast<float>(point[0]), static_cast<float>(point[1])});
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

    if (property == th::kOrderProperty && value.kind == theme::ValueKind::Number) {
        return ItemPosition(object, static_cast<std::uint32_t>(std::stoul(value.text))).has_value();
    }

    if (property == "Template") {
        auto control = object.try_as<controls::Control>();
        if (!control) return false;

        if (value.kind == theme::ValueKind::Template) {
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

bool XamlTreeBackend::SetHover(theme::NodeId id, const std::vector<theme::HoverValue>& values) {
    if (*m_closed) return false;
    xaml::DependencyObject object = Resolve(id);
    xaml::UIElement element{nullptr};
    if (object) element = object.try_as<xaml::UIElement>();

    if (values.empty()) {
        m_hover.Set(id, values, *this);  // under the pointer, the values underneath come back first
        RemoveHoverHandlers(id);
        return static_cast<bool>(element);
    }
    if (!element) return false;

    if (m_hover.Set(id, values, *this) == theme::HoverTracker::Change::Added) {
        RemoveHoverHandlers(id);  // none should be left; a stale pair would apply the hover twice
        // The handlers run inside Apple Music's XAML: OnPointer lets nothing escape. They hold a weak pointer, so a
        // backend destroyed before them leaves them doing nothing.
        std::weak_ptr<bool> alive = m_alive;
        HoverHandlers handlers;
        handlers.element = winrt::make_weak(element);
        handlers.entered = element.PointerEntered(
            [this, alive, id](IInspectable const&, xaml::Input::PointerRoutedEventArgs const&) noexcept {
                if (!alive.expired()) OnPointer(id, true);
            });
        handlers.exited = element.PointerExited(
            [this, alive, id](IInspectable const&, xaml::Input::PointerRoutedEventArgs const&) noexcept {
                if (!alive.expired()) OnPointer(id, false);
            });
        m_hoverHandlers[id] = handlers;
    }
    return true;
}

void XamlTreeBackend::OnPointer(theme::NodeId id, bool entered) noexcept {
    try {
        if (*m_closed) return;
        if (entered) {
            m_hover.Enter(id, *this);
        } else {
            m_hover.Exit(id, *this);
        }
    } catch (...) {
    }
}

void XamlTreeBackend::RemoveHoverHandlers(theme::NodeId id) {
    auto it = m_hoverHandlers.find(id);
    if (it == m_hoverHandlers.end()) return;
    if (xaml::UIElement element = it->second.element.get()) {
        element.PointerEntered(it->second.entered);
        element.PointerExited(it->second.exited);
    }
    m_hoverHandlers.erase(it);
}

// Every key of the application's resources: the dictionary itself, its merged dictionaries and the theme dictionaries
// (Default, Light, Dark), where Apple Music and WinUI keep their colours (the dictionary itself is empty in 1.1792).
std::vector<std::string> XamlTreeBackend::ResourceKeys() {
    std::vector<std::string> keys;
    std::set<std::string> seen;
    std::function<void(const xaml::ResourceDictionary&, int)> walk = [&](const xaml::ResourceDictionary& dictionary, int depth) {
        if (!dictionary || depth > 4) return;
        // Walking some of Apple Music's dictionaries fails with E_FAIL (bench, 30/10: the whole inspector task was
        // lost every tick). Each part is tried on its own; what fails is skipped.
        try {
            for (const auto& pair : dictionary) {
                std::string key = Utf8(winrt::unbox_value_or<winrt::hstring>(pair.Key(), L""));
                if (!key.empty() && seen.insert(key).second) keys.push_back(key);
            }
        } catch (...) {
        }
        try {
            for (const auto& pair : dictionary.ThemeDictionaries()) {
                walk(pair.Value().try_as<xaml::ResourceDictionary>(), depth + 1);
            }
        } catch (...) {
        }
        try {
            for (const auto& merged : dictionary.MergedDictionaries()) walk(merged, depth + 1);
        } catch (...) {
        }
    };
    walk(xaml::Application::Current().Resources(), 0);
    std::sort(keys.begin(), keys.end());
    return keys;
}

std::optional<theme::Value> XamlTreeBackend::ReadResource(std::string_view key) {
    xaml::ResourceDictionary resources = xaml::Application::Current().Resources();
    winrt::hstring name = winrt::to_hstring(std::string(key));
    IInspectable value{nullptr};
    try {
        if (!resources.HasKey(winrt::box_value(name))) return std::nullopt;
        value = resources.Lookup(winrt::box_value(name));
    } catch (...) {
        return std::nullopt;  // a resource Apple Music cannot give out of its context: read as absent
    }
    if (auto solid = value.try_as<media::SolidColorBrush>()) return theme::Value{theme::ValueKind::Color, ColorText(solid.Color())};
    // A Color resource reads as a colour too: an opaque handle would differ on every tick and be written again.
    if (auto color = value.try_as<winrt::Windows::Foundation::IReference<ColorValue>>()) {
        return theme::Value{theme::ValueKind::Color, ColorText(color.Value())};
    }
    return Keep(value);
}

bool XamlTreeBackend::WriteResource(std::string_view key, const theme::Value& value) {
    std::string keyText(key);
    xaml::ResourceDictionary resources = xaml::Application::Current().Resources();
    IInspectable name = winrt::box_value(winrt::to_hstring(keyText));
    // The value Apple Music had comes back: a key the kit added on top of the theme dictionaries is removed, so the
    // app's light and dark values work again, instead of the one read under the theme staying for good.
    if (auto added = m_addedKeys.find(keyText); added != m_addedKeys.end() && added->second == value) {
        m_addedKeys.erase(added);
        if (OwnsKey(resources, name)) resources.Remove(name);
        m_resourcesDirty = true;
        return true;
    }
    if (!OwnsKey(resources, name) && !m_addedKeys.count(keyText)) {
        if (std::optional<theme::Value> before = ReadResource(key)) m_addedKeys.emplace(keyText, *before);
    }

    std::optional<IInspectable> object;
    if (value.kind == theme::ValueKind::Color) {
        std::optional<ColorValue> color = ColorFromText(value.text);
        if (!color) return false;
        object = media::SolidColorBrush(*color);
    } else if (value.kind == theme::ValueKind::Opaque) {
        object = Restore(value);
    }
    if (!object) return false;

    // Fluent names a Color without the Brush suffix (SolidBackgroundFillColorBase) and the brush with it: a brush in
    // place of a Color breaks every brush built on it, so the value keeps the kind already there.
    if (value.kind == theme::ValueKind::Color && resources.HasKey(name)) {
        if (std::optional<ColorValue> color = ColorFromText(value.text);
            color && resources.Lookup(name).try_as<winrt::Windows::Foundation::IReference<ColorValue>>()) {
            object = winrt::box_value(*color);
        }
    }
    resources.Insert(name, *object);
    m_resourcesDirty = true;
    return true;
}

bool XamlTreeBackend::SetDictionary(std::string_view xaml) {
    xaml::ResourceDictionary resources = xaml::Application::Current().Resources();
    auto merged = resources.MergedDictionaries();

    if (m_merged) {
        uint32_t index = 0;
        if (merged.IndexOf(m_merged, index)) merged.RemoveAt(index);
        m_merged = nullptr;
    }
    m_resourcesDirty = true;
    if (xaml.empty()) return true;

    xaml::ResourceDictionary dictionary = xaml::Markup::XamlReader::Load(winrt::to_hstring(std::string(xaml))).try_as<xaml::ResourceDictionary>();
    if (!dictionary) return false;
    merged.Append(dictionary);
    m_merged = dictionary;
    return true;
}

bool XamlTreeBackend::EnsureSettingsSection(const std::vector<std::string>& themeNames, const std::string& activeName,
                                            std::function<void(const std::string&)> onChoice) {
    // Find the settings page, then its first panel. The panel is where the section is inserted, at the top.
    std::vector<theme::NodeInfo> nodes = Snapshot();
    int page = -1;
    for (int index = 0; index < static_cast<int>(nodes.size()); index++) {
        if (nodes[index].type == "GeneralSettingsPage") page = index;
    }
    if (page < 0) return false;

    for (const theme::NodeInfo& node : nodes) {
        if (node.name == "AmkThemeSection") return false;  // already there
    }

    controls::Panel panel{nullptr};
    for (int index = page + 1; index < static_cast<int>(nodes.size()); index++) {
        if (nodes[index].depth <= nodes[page].depth) break;  // left the page
        xaml::DependencyObject candidate = Resolve(nodes[index].id);
        if (!panel && candidate) panel = candidate.try_as<controls::Panel>();
    }
    if (!panel) return false;

    controls::StackPanel section;
    section.Name(L"AmkThemeSection");

    controls::TextBlock title;
    title.Text(L"Thème (Apple Music Mod Kit)");
    section.Children().Append(title);

    controls::ComboBox choice;
    choice.Width(240);
    for (const std::string& name : themeNames) {
        controls::ComboBoxItem item;
        item.Content(winrt::box_value(winrt::to_hstring(name.empty() ? std::string("Aucun") : name)));
        item.Tag(winrt::box_value(winrt::to_hstring(name)));
        choice.Items().Append(item);
        if (name == activeName) choice.SelectedItem(item);
    }
    // Runs inside Apple Music's XAML: an exception that escaped would come back to XAML as a failure, which raises
    // Application.UnhandledException and ends the app.
    choice.SelectionChanged([onChoice](const IInspectable& sender, const controls::SelectionChangedEventArgs&) noexcept {
        try {
            auto selected = sender.as<controls::ComboBox>().SelectedItem().try_as<controls::ComboBoxItem>();
            if (selected) onChoice(Utf8(winrt::unbox_value_or<winrt::hstring>(selected.Tag(), L"")));
        } catch (...) {
        }
    });
    section.Children().Append(choice);

    panel.Children().InsertAt(0, section);
    return true;
}

}  // namespace amk::win

#endif  // AMK_WITH_XAML
