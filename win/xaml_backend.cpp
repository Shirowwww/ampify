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

#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Text.h>

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

double ParseLength(const std::string& text) {
    if (text == "NaN") return std::numeric_limits<double>::quiet_NaN();
    return std::stod(text);
}

std::string Lowered(std::string_view text) {
    std::string result(text);
    for (char& c : result) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return result;
}

// `file:///C:/...` for a path with '/' separators.
winrt::hstring FileUri(const std::string& path) {
    return winrt::to_hstring("file:///" + path);
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

}  // namespace

bool XamlTreeBackend::Attach() {
    xaml::Window window = AppWindow(L"MainWindow");
    if (!window) return false;
    m_window = winrt::make_weak(window);
    return true;
}

xaml::DependencyObject XamlTreeBackend::Resolve(theme::NodeId id) const {
    auto it = m_elements.find(id);
    if (it == m_elements.end()) return nullptr;
    return it->second.get();  // null when the element has been destroyed since the snapshot
}

theme::Value XamlTreeBackend::Keep(IInspectable object) {
    std::uint64_t token = m_nextToken++;
    m_kept[token] = object;
    return {theme::ValueKind::Opaque, "handle:" + std::to_string(token)};
}

IInspectable XamlTreeBackend::Restore(const theme::Value& token) const {
    std::uint64_t number = std::stoull(token.text.substr(std::string("handle:").size()));
    auto it = m_kept.find(number);
    return it == m_kept.end() ? IInspectable{nullptr} : it->second;
}

std::vector<theme::NodeInfo> XamlTreeBackend::Snapshot() {
    std::vector<theme::NodeInfo> nodes;
    m_elements.clear();

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
    return nodes;
}

std::optional<theme::Value> XamlTreeBackend::Read(theme::NodeId id, std::string_view property) {
    xaml::DependencyObject object = Resolve(id);
    if (!object) return std::nullopt;

    auto brush = [this](const media::Brush& value) -> theme::Value {
        if (!value) return Keep(nullptr);
        if (auto solid = value.try_as<media::SolidColorBrush>()) return {theme::ValueKind::Color, ColorText(solid.Color())};
        return Keep(value);
    };

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
    } else if (property == "FontSize") {
        if (auto control = object.try_as<controls::Control>()) return theme::Value{theme::ValueKind::Length, th::FormatNumber(control.FontSize())};
        if (auto text = object.try_as<controls::TextBlock>()) return theme::Value{theme::ValueKind::Length, th::FormatNumber(text.FontSize())};
    } else if (property == "FontFamily") {
        if (auto text = object.try_as<controls::TextBlock>()) return theme::Value{theme::ValueKind::Text, Utf8(text.FontFamily().Source())};
    } else if (property == "Visibility") {
        if (auto element = object.try_as<xaml::UIElement>()) {
            return theme::Value{theme::ValueKind::Visibility, element.Visibility() == xaml::Visibility::Collapsed ? "Collapsed" : "Visible"};
        }
    } else if (property == "Opacity") {
        if (auto element = object.try_as<xaml::UIElement>()) return theme::Value{theme::ValueKind::Number, th::FormatNumber(element.Opacity())};
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

bool XamlTreeBackend::Write(theme::NodeId id, std::string_view property, const theme::Value& value) {
    xaml::DependencyObject object = Resolve(id);
    if (!object) return false;

    if (property == "Background" || property == "Foreground" || property == "BorderBrush") {
        std::optional<media::Brush> target;
        if (value.kind == theme::ValueKind::Color) {
            std::optional<ColorValue> color = ColorFromText(value.text);
            if (!color) return false;
            target = media::SolidColorBrush(*color);
        } else if (value.kind == theme::ValueKind::Image) {
            media::ImageBrush image;
            image.ImageSource(media::Imaging::BitmapImage(winrt::Windows::Foundation::Uri(FileUri(value.text))));
            target = image;
        } else if (value.kind == theme::ValueKind::Opaque) {
            target = Restore(value).try_as<media::Brush>();
        }
        if (!target) return false;

        if (property == "Background") {
            if (auto control = object.try_as<controls::Control>()) { control.Background(*target); return true; }
            if (auto panel = object.try_as<controls::Panel>()) { panel.Background(*target); return true; }
            if (auto border = object.try_as<controls::Border>()) { border.Background(*target); return true; }
        } else if (property == "Foreground") {
            if (auto control = object.try_as<controls::Control>()) { control.Foreground(*target); return true; }
            if (auto text = object.try_as<controls::TextBlock>()) { text.Foreground(*target); return true; }
        } else {
            if (auto control = object.try_as<controls::Control>()) { control.BorderBrush(*target); return true; }
            if (auto border = object.try_as<controls::Border>()) { border.BorderBrush(*target); return true; }
        }
        return false;
    }

    if (property == "FontSize" && value.kind == theme::ValueKind::Length) {
        double size = ParseLength(value.text);
        if (auto control = object.try_as<controls::Control>()) { control.FontSize(size); return true; }
        if (auto text = object.try_as<controls::TextBlock>()) { text.FontSize(size); return true; }
        return false;
    }

    if (property == "FontFamily" && value.kind == theme::ValueKind::Text) {
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
        } else {
            if (auto control = object.try_as<controls::Control>()) { control.BorderThickness(thickness); return true; }
            if (auto border = object.try_as<controls::Border>()) { border.BorderThickness(thickness); return true; }
        }
        return false;
    }

    if (property == "CornerRadius" && value.kind == theme::ValueKind::Thickness) {
        // One radius for the four corners: the first number of the value.
        double radius = Numbers(value.text, ',').front();
        if (auto control = object.try_as<controls::Control>()) { control.CornerRadius(xaml::CornerRadius{radius, radius, radius, radius}); return true; }
        if (auto border = object.try_as<controls::Border>()) { border.CornerRadius(xaml::CornerRadius{radius, radius, radius, radius}); return true; }
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

std::vector<std::string> XamlTreeBackend::ResourceKeys() {
    std::vector<std::string> keys;
    xaml::ResourceDictionary resources = xaml::Application::Current().Resources();
    for (const auto& pair : resources) {
        keys.push_back(Utf8(winrt::unbox_value_or<winrt::hstring>(pair.Key(), L"")));
    }
    return keys;
}

std::optional<theme::Value> XamlTreeBackend::ReadResource(std::string_view key) {
    xaml::ResourceDictionary resources = xaml::Application::Current().Resources();
    winrt::hstring name = winrt::to_hstring(std::string(key));
    if (!resources.HasKey(winrt::box_value(name))) return std::nullopt;

    IInspectable value = resources.Lookup(winrt::box_value(name));
    if (auto solid = value.try_as<media::SolidColorBrush>()) return theme::Value{theme::ValueKind::Color, ColorText(solid.Color())};
    return Keep(value);
}

bool XamlTreeBackend::WriteResource(std::string_view key, const theme::Value& value) {
    std::optional<IInspectable> object;
    if (value.kind == theme::ValueKind::Color) {
        std::optional<ColorValue> color = ColorFromText(value.text);
        if (!color) return false;
        object = media::SolidColorBrush(*color);
    } else if (value.kind == theme::ValueKind::Opaque) {
        object = Restore(value);
    }
    if (!object) return false;

    xaml::ResourceDictionary resources = xaml::Application::Current().Resources();
    resources.Insert(winrt::box_value(winrt::to_hstring(std::string(key))), *object);
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
    choice.SelectionChanged([onChoice](const IInspectable& sender, const controls::SelectionChangedEventArgs&) {
        auto selected = sender.as<controls::ComboBox>().SelectedItem().try_as<controls::ComboBoxItem>();
        if (selected) onChoice(Utf8(winrt::unbox_value_or<winrt::hstring>(selected.Tag(), L"")));
    });
    section.Children().Append(choice);

    panel.Children().InsertAt(0, section);
    return true;
}

}  // namespace amk::win

#endif  // AMK_WITH_XAML
