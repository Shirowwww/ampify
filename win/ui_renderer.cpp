// NOT RUN YET IN APPLE MUSIC: see ui_renderer.hpp. Written against the C++/WinRT projection of Windows App SDK 1.8.
// The style copying follows AMExclusive (MIT, https://github.com/AntiOblivionis/AMExclusive, mod/src/UiAdapter.cpp,
// AttachOrRefreshToggle, at 91f589d): native Style, margins and padding first, WinUI card resources otherwise.

#if defined(AMPIFY_WITH_XAML)

#include "ui_renderer.hpp"

#include <cmath>
#include <utility>


#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

#include "../core/json.hpp"
#include "../core/ui_render/probe.hpp"
#include "../core/ui_render/svg_icon.hpp"

namespace ampify::win {

namespace {

namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = xaml::Controls;
namespace primitives = xaml::Controls::Primitives;
namespace media = xaml::Media;
namespace automation = xaml::Automation;
using winrt::Windows::Foundation::IInspectable;
using uirender::Patch;
using uirender::PatchOp;
using uirender::RenderNode;

constexpr wchar_t kXmlns[] = L"xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation'";

winrt::hstring Wide(const std::string& text) {
    return winrt::to_hstring(text);
}

std::string Utf8(const winrt::hstring& text) {
    return winrt::to_string(text);
}

// Loads a small piece of XAML written by Ampify (never text of an extension, except path data checked by
// ParseSvgIcon). Null when the reader refuses it, for example when a theme resource is missing.
template <typename T>
T LoadXaml(const std::wstring& text) noexcept {
    try {
        return xaml::Markup::XamlReader::Load(winrt::hstring(text)).try_as<T>();
    } catch (...) {
        return nullptr;
    }
}

xaml::Style StyleResource(const wchar_t* key) noexcept {
    return ThemeResource(key).try_as<xaml::Style>();
}

media::Brush BrushResource(const wchar_t* key) noexcept {
    return ThemeResource(key).try_as<media::Brush>();
}

void SetText(const controls::TextBlock& block, const winrt::hstring& text) {
    if (block.Text() != text) block.Text(text);
}

void SetVisible(const xaml::UIElement& element, bool visible) {
    xaml::Visibility wanted = visible ? xaml::Visibility::Visible : xaml::Visibility::Collapsed;
    if (element.Visibility() != wanted) element.Visibility(wanted);
}

const wchar_t* TextStyleKey(const std::string& variant) {
    if (variant == "title") return L"TitleTextBlockStyle";
    if (variant == "subtitle") return L"SubtitleTextBlockStyle";
    if (variant == "caption") return L"CaptionTextBlockStyle";
    if (variant == "bodyStrong") return L"BodyStrongTextBlockStyle";
    return L"BodyTextBlockStyle";
}

// An icon drawn as XAML shapes: the SVG paths in a Viewbox, filled with a theme brush, so the icon follows the light
// and dark modes. A glyph gives a FontIcon. Null when the icon cannot be drawn.
xaml::FrameworkElement MakeIcon(const std::string& icon, double size, const wchar_t* brushKey) {
    if (icon.empty()) return nullptr;
    if (uirender::IsGlyphIcon(icon)) {
        controls::FontIcon glyph;
        glyph.Glyph(Wide(icon));
        glyph.FontSize(size);
        return glyph;
    }
    uirender::SvgIcon svg = uirender::ParseSvgIcon(icon);
    if (!svg.Usable()) return nullptr;
    std::wstring text = L"<Viewbox " + std::wstring(kXmlns) + L" Width='" + std::to_wstring(size) + L"' Height='" +
                        std::to_wstring(size) + L"'><Canvas Width='" + std::to_wstring(svg.width) + L"' Height='" +
                        std::to_wstring(svg.height) + L"'>";
    for (const std::string& data : svg.paths) {
        text += L"<Path Data='" + std::wstring(Wide(data)) + L"' Fill='{ThemeResource " + brushKey + L"}'/>";
    }
    text += L"</Canvas></Viewbox>";
    return LoadXaml<xaml::FrameworkElement>(text);
}

// An image address: https, http, ms-appx and ms-appdata as they are; anything else is a file path.
winrt::Windows::Foundation::Uri ImageUri(const std::string& source) {
    for (const char* scheme : {"https://", "http://", "ms-appx:", "ms-appdata:", "file:"}) {
        if (source.rfind(scheme, 0) == 0) return winrt::Windows::Foundation::Uri(Wide(source));
    }
    std::string path = source;
    for (char& c : path) {
        if (c == '\\') c = '/';
    }
    return winrt::Windows::Foundation::Uri(Wide("file:///" + path));
}

primitives::PlacementMode TooltipPlacement(const std::string& placement) {
    if (placement == "bottom") return primitives::PlacementMode::Bottom;
    if (placement == "left") return primitives::PlacementMode::Left;
    if (placement == "right") return primitives::PlacementMode::Right;
    return primitives::PlacementMode::Top;
}

// The name read by screen readers: the label, the title, the text, or the tooltip.
std::string AccessibleName(const RenderNode& node) {
    for (const char* key : {"label", "title", "text", "tooltip"}) {
        std::string value = node.Text(key);
        if (!value.empty()) return value;
    }
    return {};
}

struct UpdateGuard {
    explicit UpdateGuard(int& counter) : m_counter(counter) { ++m_counter; }
    ~UpdateGuard() { --m_counter; }
    UpdateGuard(const UpdateGuard&) = delete;
    UpdateGuard& operator=(const UpdateGuard&) = delete;

private:
    int& m_counter;
};

bool NeedsGrid(const RenderNode& node) {
    return node.type == "row" && node.children.size() == 2;
}

}  // namespace

// What an event handler needs, shared with the handlers so they never hold the renderer itself.
struct XamlRenderer::Callbacks {
    std::string elementId;
    int onClick = 0;
    int onChange = 0;
    UiEventSink sink;
    std::weak_ptr<int> updating;
    // The value the renderer last set itself, as JSON. WinUI may report such a change after the renderer has returned
    // (Toggled and TextChanged can arrive late): that echo is dropped once, so the extension only hears the user.
    mutable std::string echo;

    void Fire(bool click, const std::string& valueJson) const noexcept {
        try {
            std::shared_ptr<int> busy = updating.lock();
            if (!busy) return;  // the renderer is gone
            // The renderer's own value, reported now (Toggled, ValueChanged) or late (TextChanged): dropped, and the
            // echo is spent either way, so a later change by the user to that same value still counts.
            if (!click && !echo.empty() && valueJson == echo) {
                echo.clear();
                return;
            }
            if (*busy > 0) return;  // the renderer itself is setting values
            if (!click) echo.clear();
            int id = click ? onClick : onChange;
            if (id <= 0 || !sink) return;
            sink(UiEvent{elementId, id, click, valueJson});
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    }
};

struct XamlRenderer::Mounted {
    std::string type;
    bool grid = false;  // a row drawn as a Grid of 2 columns
    // All strong: in Apple Music a weak reference to a XAML object Ampify made never resolves, even while the object
    // is on screen (seen on 1.1792: the slot of a panel was lost right after it was made). The renderer is released
    // on the interface thread with its surface, and no handler holds these objects, so there is no cycle.
    xaml::FrameworkElement outer{nullptr};    // what the parent holds
    xaml::FrameworkElement control{nullptr};  // what takes the attributes, the name and the tooltip
    controls::Panel host{nullptr};            // where the children go
    controls::TextBlock label{nullptr};       // the label of a toggle, the title of a card
    controls::TextBlock subtitle{nullptr};    // the subtitle of a card
    std::shared_ptr<Callbacks> callbacks;
    std::vector<Mounted> children;
};

int StyleProbe::Found() const {
    int count = 0;
    for (const xaml::Style& style : {button, toggle, comboBox, slider, textBox, titleText, captionText}) {
        if (style) count++;
    }
    if (cardBackground) count++;
    if (cardStroke) count++;
    if (cardPadding) count++;
    if (cardCorner) count++;
    return count;
}

IInspectable ThemeResource(const wchar_t* key) noexcept {
    try {
        xaml::Application application = xaml::Application::Current();
        if (!application) return nullptr;
        return application.Resources().TryLookup(winrt::box_value(winrt::hstring(key)));
    } catch (...) {
        return nullptr;
    }
}

void CaptureStyleProbe(const xaml::DependencyObject& root, StyleProbe& probe, const std::string& origin) {
    if (!root) return;
    int found = 0;
    std::vector<xaml::DependencyObject> stack{root};
    std::size_t visited = 0;
    controls::ToggleSwitch nativeToggle{nullptr};
    while (!stack.empty() && visited < 4000) {
        xaml::DependencyObject node = stack.back();
        stack.pop_back();
        visited++;
        try {
            auto element = node.try_as<xaml::FrameworkElement>();
            if (element) {
                std::string name = Utf8(element.Name());
                if (uirender::IsOwnName(name) || uirender::IsForeignModName(name)) continue;
            }
            if (node.try_as<controls::ItemsControl>() || node.try_as<controls::ItemsRepeater>()) continue;

            if (auto toggle = node.try_as<controls::ToggleSwitch>()) {
                if (toggle.Style()) probe.toggle = toggle.Style();
                nativeToggle = toggle;  // the last one: its row gives the card values below
                found++;
                continue;
            }
            if (auto combo = node.try_as<controls::ComboBox>()) {
                if (combo.Style() && !probe.comboBox) probe.comboBox = combo.Style();
                found++;
                continue;
            }
            if (auto slider = node.try_as<controls::Slider>()) {
                if (slider.Style() && !probe.slider) probe.slider = slider.Style();
                found++;
                continue;
            }
            if (auto box = node.try_as<controls::TextBox>()) {
                if (box.Style() && !probe.textBox) probe.textBox = box.Style();
                found++;
                continue;
            }
            if (auto button = node.try_as<controls::Button>()) {
                // Only a button that shows a text: an icon button of the title bar would give a wrong look.
                bool textual = !winrt::unbox_value_or<winrt::hstring>(button.Content(), L"").empty();
                if (textual && button.Style() && !probe.button) probe.button = button.Style();
                if (textual) found++;
                continue;
            }
            int count = media::VisualTreeHelper::GetChildrenCount(node);
            for (int index = count - 1; index >= 0; index--) stack.push_back(media::VisualTreeHelper::GetChild(node, index));
        } catch (...) {
        }
    }

    // The card of a native settings row: the widest Border with a background between the toggle and the root, and the
    // two first TextBlocks of that row for the title and the description.
    if (nativeToggle) {
        try {
            controls::Border best{nullptr};
            double bestArea = 0;
            for (xaml::DependencyObject current = nativeToggle; current && current != root;
                 current = media::VisualTreeHelper::GetParent(current)) {
                auto border = current.try_as<controls::Border>();
                if (!border || !border.Background()) continue;
                double area = border.ActualWidth() * border.ActualHeight();
                if (area > bestArea && border.ActualWidth() > nativeToggle.ActualWidth() * 3.0) {
                    best = border;
                    bestArea = area;
                }
            }
            if (best) {
                probe.cardBackground = best.Background();
                probe.cardStroke = best.BorderBrush() ? best.BorderBrush() : BrushResource(L"CardStrokeColorDefaultBrush");
                probe.cardPadding = best.Padding();
                probe.cardCorner = best.CornerRadius();
                probe.cardMargin = best.Margin();
                std::vector<controls::TextBlock> texts;
                std::vector<xaml::DependencyObject> pending{best};
                while (!pending.empty() && texts.size() < 2) {
                    xaml::DependencyObject node = pending.back();
                    pending.pop_back();
                    if (node.try_as<controls::ToggleSwitch>()) continue;  // its On/Off text is not a row title
                    if (auto text = node.try_as<controls::TextBlock>(); text && !text.Text().empty()) {
                        texts.push_back(text);
                        continue;
                    }
                    int count = media::VisualTreeHelper::GetChildrenCount(node);
                    for (int index = count - 1; index >= 0; index--) pending.push_back(media::VisualTreeHelper::GetChild(node, index));
                }
                if (!texts.empty() && texts[0].Style()) probe.titleText = texts[0].Style();
                if (texts.size() > 1 && texts[1].Style()) probe.captionText = texts[1].Style();
            }
        } catch (...) {
        }
    }
    if (found > 0 || probe.Found() > 0) probe.origin = origin;
}

controls::Border MakeCardBorder(const StyleProbe& probe) {
    // {ThemeResource} keeps the brushes in step with the light and dark modes; the literal values are WinUI's card.
    controls::Border card = LoadXaml<controls::Border>(
        L"<Border " + std::wstring(kXmlns) +
        L" Background='{ThemeResource CardBackgroundFillColorDefaultBrush}'"
        L" BorderBrush='{ThemeResource CardStrokeColorDefaultBrush}' BorderThickness='1' CornerRadius='8'"
        L" Padding='16,12,16,12'/>");
    if (!card) {
        card = controls::Border();
        card.Background(BrushResource(L"CardBackgroundFillColorDefaultBrush"));
        card.BorderBrush(BrushResource(L"CardStrokeColorDefaultBrush"));
        card.BorderThickness(xaml::Thickness{1, 1, 1, 1});
        card.CornerRadius(xaml::CornerRadius{8, 8, 8, 8});
        card.Padding(xaml::Thickness{16, 12, 16, 12});
    }
    if (probe.cardBackground) card.Background(probe.cardBackground);
    if (probe.cardStroke) card.BorderBrush(probe.cardStroke);
    if (probe.cardCorner) card.CornerRadius(*probe.cardCorner);
    if (probe.cardPadding) card.Padding(*probe.cardPadding);
    return card;
}

controls::TextBlock MakeText(const std::wstring& text, const std::string& variant, bool secondary, const StyleProbe* probe) {
    controls::TextBlock block{nullptr};
    if (secondary) {
        block = LoadXaml<controls::TextBlock>(L"<TextBlock " + std::wstring(kXmlns) +
                                              L" Foreground='{ThemeResource TextFillColorSecondaryBrush}'/>");
    }
    if (!block) block = controls::TextBlock();
    xaml::Style style{nullptr};
    if (probe && variant == "bodyStrong" && probe->titleText) style = probe->titleText;
    if (probe && variant == "caption" && probe->captionText) style = probe->captionText;
    if (!style) style = StyleResource(TextStyleKey(variant));
    if (style) block.Style(style);
    block.TextWrapping(xaml::TextWrapping::Wrap);
    block.Text(winrt::hstring(text));
    return block;
}

const wchar_t* KitXmlns() noexcept {
    return kXmlns;
}

IInspectable LoadKitXaml(const std::wstring& text) noexcept {
    try {
        return xaml::Markup::XamlReader::Load(winrt::hstring(text));
    } catch (...) {
        return nullptr;
    }
}

xaml::FrameworkElement MakeIconElement(const std::string& icon, double size, const wchar_t* brushKey) {
    return MakeIcon(icon, size, brushKey);
}

xaml::FrameworkElement MakeDivider() {
    xaml::FrameworkElement line = LoadXaml<xaml::FrameworkElement>(
        L"<Border " + std::wstring(kXmlns) + L" Height='1' Background='{ThemeResource DividerStrokeColorDefaultBrush}'/>");
    if (line) return line;
    controls::Border fallback;
    fallback.Height(1);
    fallback.Background(BrushResource(L"DividerStrokeColorDefaultBrush"));
    return fallback;
}

XamlRenderer::XamlRenderer(std::string elementId, UiEventSink sink)
    : m_elementId(std::move(elementId)), m_sink(std::move(sink)), m_updating(std::make_shared<int>(0)) {}

XamlRenderer::~XamlRenderer() = default;

xaml::FrameworkElement XamlRenderer::Root() const {
    return m_root ? m_root->outer : nullptr;
}

xaml::FrameworkElement XamlRenderer::Render(RenderNode tree, const StyleProbe& probe, uirender::PatchStats& stats,
                                            bool& rootReplaced) {
    auto next = std::make_unique<RenderNode>(std::move(tree));
    rootReplaced = false;
    UpdateGuard guard(*m_updating);

    bool rebuild = !m_root || !m_root->outer || (m_tree && NeedsGrid(*next) != m_root->grid && next->type == "row");
    Patch patch = uirender::Reconcile(rebuild ? nullptr : m_tree.get(), *next);
    stats = uirender::CountPatch(patch);
    if (rebuild || patch.op == PatchOp::Create || patch.op == PatchOp::Replace) {
        m_root = std::make_unique<Mounted>(Build(*next, probe));
        rootReplaced = true;
    } else {
        Apply(*m_root, patch, probe);
    }
    m_tree = std::move(next);
    return Root();
}

XamlRenderer::Mounted XamlRenderer::Build(const RenderNode& node, const StyleProbe& probe) {
    Mounted mounted;
    mounted.type = node.type;
    mounted.callbacks = std::make_shared<Callbacks>();
    mounted.callbacks->elementId = m_elementId;
    mounted.callbacks->sink = m_sink;
    mounted.callbacks->updating = m_updating;
    std::shared_ptr<Callbacks> callbacks = mounted.callbacks;

    xaml::FrameworkElement outer{nullptr};
    xaml::FrameworkElement control{nullptr};

    if (node.type == "stack" || node.type == "list" || (node.type == "row" && !NeedsGrid(node))) {
        controls::StackPanel panel;
        if (node.type == "row") {
            panel.Orientation(controls::Orientation::Horizontal);
            panel.VerticalAlignment(xaml::VerticalAlignment::Center);
        }
        outer = control = panel;
        mounted.host = panel;
    } else if (node.type == "row") {
        controls::Grid grid;
        controls::ColumnDefinition first;
        first.Width(xaml::GridLength{1.0, xaml::GridUnitType::Star});
        controls::ColumnDefinition second;
        second.Width(xaml::GridLength{1.0, xaml::GridUnitType::Auto});
        grid.ColumnDefinitions().Append(first);
        grid.ColumnDefinitions().Append(second);
        outer = control = grid;
        mounted.host = grid;
        mounted.grid = true;
    } else if (node.type == "card") {
        controls::Border card = MakeCardBorder(probe);
        controls::StackPanel inner;
        inner.Spacing(8);
        controls::StackPanel header;
        header.Spacing(2);
        controls::TextBlock title = MakeText(L"", "bodyStrong", false, &probe);
        controls::TextBlock subtitle = MakeText(L"", "caption", true, &probe);
        header.Children().Append(title);
        header.Children().Append(subtitle);
        controls::StackPanel host;
        host.Spacing(8);
        inner.Children().Append(header);
        inner.Children().Append(host);
        card.Child(inner);
        card.Tapped([callbacks](IInspectable const&, xaml::Input::TappedRoutedEventArgs const&) noexcept {
            callbacks->Fire(true, {});
        });
        outer = control = card;
        mounted.host = host;
        mounted.label = title;
        mounted.subtitle = subtitle;
    } else if (node.type == "text") {
        controls::TextBlock block = MakeText(L"", node.Text("variant", "body"), node.Text("variant") == "caption", &probe);
        outer = control = block;
    } else if (node.type == "button") {
        controls::Button button;
        button.Click([callbacks](IInspectable const&, xaml::RoutedEventArgs const&) noexcept { callbacks->Fire(true, {}); });
        outer = control = button;
    } else if (node.type == "toggle") {
        controls::Grid grid;
        grid.ColumnSpacing(12);
        controls::ColumnDefinition first;
        first.Width(xaml::GridLength{1.0, xaml::GridUnitType::Star});
        controls::ColumnDefinition second;
        second.Width(xaml::GridLength{1.0, xaml::GridUnitType::Auto});
        grid.ColumnDefinitions().Append(first);
        grid.ColumnDefinitions().Append(second);
        controls::TextBlock label = MakeText(L"", "body", false, &probe);
        label.VerticalAlignment(xaml::VerticalAlignment::Center);
        controls::ToggleSwitch toggle;
        if (probe.toggle) toggle.Style(probe.toggle);
        toggle.VerticalAlignment(xaml::VerticalAlignment::Center);
        toggle.HorizontalAlignment(xaml::HorizontalAlignment::Right);
        toggle.MinWidth(0);
        controls::Grid::SetColumn(toggle, 1);
        // Ampify speaks English: without this, Windows names the states in its own language.
        toggle.OnContent(winrt::box_value(L"On"));
        toggle.OffContent(winrt::box_value(L"Off"));
        grid.Children().Append(label);
        grid.Children().Append(toggle);
        toggle.Toggled([callbacks](IInspectable const& sender, xaml::RoutedEventArgs const&) noexcept {
            try {
                bool on = sender.as<controls::ToggleSwitch>().IsOn();
                callbacks->Fire(false, on ? "true" : "false");
            } catch (...) {
            }
        });
        outer = grid;
        control = toggle;
        mounted.label = label;
    } else if (node.type == "slider") {
        controls::Slider slider;
        if (probe.slider) slider.Style(probe.slider);
        slider.ValueChanged([callbacks](IInspectable const&, primitives::RangeBaseValueChangedEventArgs const& args) noexcept {
            try {
                callbacks->Fire(false, Json(args.NewValue()).Dump());
            } catch (...) {
            }
        });
        outer = control = slider;
    } else if (node.type == "textfield") {
        controls::TextBox box;
        if (probe.textBox) box.Style(probe.textBox);
        box.TextChanged([callbacks](IInspectable const& sender, controls::TextChangedEventArgs const&) noexcept {
            try {
                callbacks->Fire(false, Json(Utf8(sender.as<controls::TextBox>().Text())).Dump());
            } catch (...) {
            }
        });
        outer = control = box;
    } else if (node.type == "dropdown") {
        controls::ComboBox combo;
        if (probe.comboBox) combo.Style(probe.comboBox);
        combo.MinWidth(200);
        combo.SelectionChanged([callbacks](IInspectable const& sender, controls::SelectionChangedEventArgs const&) noexcept {
            try {
                auto item = sender.as<controls::ComboBox>().SelectedItem().try_as<controls::ComboBoxItem>();
                if (!item) return;
                callbacks->Fire(false, Json(Utf8(winrt::unbox_value_or<winrt::hstring>(item.Tag(), L""))).Dump());
            } catch (...) {
            }
        });
        outer = control = combo;
    } else if (node.type == "image") {
        controls::Image image;
        image.Stretch(media::Stretch::Uniform);
        image.MaxHeight(240);
        image.HorizontalAlignment(xaml::HorizontalAlignment::Left);
        image.Tapped([callbacks](IInspectable const&, xaml::Input::TappedRoutedEventArgs const&) noexcept {
            callbacks->Fire(true, {});
        });
        outer = control = image;
    } else {  // icon, and option outside a dropdown (refused by ValidateTree, drawn empty here)
        controls::Border frame;  // holds the drawn icon, which is rebuilt when the SVG changes
        frame.HorizontalAlignment(xaml::HorizontalAlignment::Left);
        frame.Background(media::SolidColorBrush(winrt::Windows::UI::Color{0, 0, 0, 0}));  // so a tap anywhere counts
        frame.Tapped([callbacks](IInspectable const&, xaml::Input::TappedRoutedEventArgs const&) noexcept {
            callbacks->Fire(true, {});
        });
        outer = control = frame;
    }

    mounted.outer = outer;
    mounted.control = control;
    std::string name = uirender::ElementName(m_elementId, node.id);
    control.Name(Wide(name));
    automation::AutomationProperties::SetAutomationId(control, Wide(name));
    if (outer != control) outer.Name(Wide(name + "_frame"));

    SetAttributes(mounted, node, probe);
    if (node.type == "dropdown") {
        FillOptions(mounted, node);
    } else if (mounted.host) {
        for (const RenderNode& child : node.children) mounted.children.push_back(Build(child, probe));
        Rewrite(mounted);
    }
    return mounted;
}

void XamlRenderer::SetAttributes(Mounted& mounted, const RenderNode& node, const StyleProbe& probe) {
    xaml::FrameworkElement control = mounted.control;
    if (!control) return;
    mounted.callbacks->onClick = static_cast<int>(node.Number("onClick", 0));
    mounted.callbacks->onChange = static_cast<int>(node.Number("onChange", 0));
    bool disabled = node.Bool("disabled", false);
    if (auto asControl = control.try_as<controls::Control>()) {
        if (asControl.IsEnabled() == disabled) asControl.IsEnabled(!disabled);
    }

    const std::string& type = node.type;
    if (type == "stack" || type == "list" || type == "row") {
        double gap = node.Number("gap", type == "list" ? 4 : (type == "row" ? 12 : 8));
        if (auto panel = control.try_as<controls::StackPanel>()) panel.Spacing(gap);
        if (auto grid = control.try_as<controls::Grid>()) grid.ColumnSpacing(gap);
    } else if (type == "card") {
        if (auto title = mounted.label) {
            SetText(title, Wide(node.Text("title")));
            SetVisible(title, node.Has("title"));
        }
        if (auto subtitle = mounted.subtitle) {
            SetText(subtitle, Wide(node.Text("subtitle")));
            SetVisible(subtitle, node.Has("subtitle"));
        }
    } else if (type == "text") {
        auto block = control.as<controls::TextBlock>();
        SetText(block, Wide(node.Text("text")));
        xaml::Style style = StyleResource(TextStyleKey(node.Text("variant", "body")));
        if (style && block.Style() != style) block.Style(style);
    } else if (type == "button") {
        auto button = control.as<controls::Button>();
        std::string variant = node.Text("variant", "secondary");
        // Apple Music's own button style for every variant: its AccentButtonStyle draws text in a symbol font (the
        // label came out as icons on 1.1792). A primary button takes the accent colours on top instead.
        if (probe.button && button.Style() != probe.button) button.Style(probe.button);
        if (variant == "primary") {
            if (auto accent = ThemeResource(L"AccentFillColorDefaultBrush").try_as<media::Brush>()) button.Background(accent);
            if (auto onAccent = ThemeResource(L"TextOnAccentFillColorPrimaryBrush").try_as<media::Brush>()) {
                button.Foreground(onAccent);
            }
        }
        if (variant == "tertiary") {
            button.Background(media::SolidColorBrush(winrt::Windows::UI::Color{0, 0, 0, 0}));
            button.BorderThickness(xaml::Thickness{0, 0, 0, 0});
        }
        std::string icon = node.Text("icon");
        winrt::hstring label = Wide(node.Text("label"));
        xaml::FrameworkElement drawn =
            MakeIcon(icon, 16, variant == "primary" ? L"TextOnAccentFillColorPrimaryBrush" : L"TextFillColorPrimaryBrush");
        if (drawn) {
            controls::StackPanel content;
            content.Orientation(controls::Orientation::Horizontal);
            content.Spacing(8);
            content.Children().Append(drawn);
            if (!label.empty()) {
                controls::TextBlock text;
                text.Text(label);
                text.VerticalAlignment(xaml::VerticalAlignment::Center);
                content.Children().Append(text);
            }
            button.Content(content);
        } else if (winrt::unbox_value_or<winrt::hstring>(button.Content(), L"") != label || !button.Content()) {
            button.Content(winrt::box_value(label));
        }
    } else if (type == "toggle") {
        auto toggle = control.as<controls::ToggleSwitch>();
        bool checked = node.Bool("checked", node.Bool("value", false));
        if (toggle.IsOn() != checked) {
            mounted.callbacks->echo = checked ? "true" : "false";
            toggle.IsOn(checked);
        }
        if (auto label = mounted.label) {
            SetText(label, Wide(node.Text("label")));
            SetVisible(label, !node.Text("label").empty());
        }
    } else if (type == "slider") {
        auto slider = control.as<controls::Slider>();
        double minimum = node.Number("min", 0);
        double maximum = node.Number("max", 100);
        if (maximum < minimum) maximum = minimum;
        if (slider.Minimum() != minimum) slider.Minimum(minimum);
        if (slider.Maximum() != maximum) slider.Maximum(maximum);
        double step = node.Number("step", 1);
        if (step > 0 && slider.StepFrequency() != step) slider.StepFrequency(step);
        double value = node.Number("value", minimum);
        if (slider.Value() != value) {
            mounted.callbacks->echo = Json(value).Dump();
            slider.Value(value);
        }
        std::string label = node.Text("label");
        slider.Header(label.empty() ? nullptr : winrt::box_value(Wide(label)));
    } else if (type == "textfield") {
        auto box = control.as<controls::TextBox>();
        winrt::hstring value = Wide(node.Text("value"));
        if (node.Has("value") && box.Text() != value) {
            mounted.callbacks->echo = Json(Utf8(value)).Dump();
            box.Text(value);
        }
        box.PlaceholderText(Wide(node.Text("placeholder")));
        std::string label = node.Text("label");
        box.Header(label.empty() ? nullptr : winrt::box_value(Wide(label)));
    } else if (type == "dropdown") {
        auto combo = control.as<controls::ComboBox>();
        std::string label = node.Text("label");
        combo.Header(label.empty() ? nullptr : winrt::box_value(Wide(label)));
    } else if (type == "image") {
        auto image = control.as<controls::Image>();
        std::string source = node.Text("src");
        if (!source.empty()) {
            try {
                image.Source(media::Imaging::BitmapImage(ImageUri(source)));
            } catch (...) {
                image.Source(nullptr);
            }
        }
        if (node.Has("width")) image.Width(node.Number("width", 0));
        if (node.Has("height")) image.Height(node.Number("height", 0));
    } else if (type == "icon") {
        auto frame = control.as<controls::Border>();
        frame.Child(MakeIcon(node.Text("icon"), node.Number("size", 20), L"TextFillColorPrimaryBrush"));
    }

    std::string accessible = AccessibleName(node);
    if (type != "text") automation::AutomationProperties::SetName(control, Wide(accessible));
    std::string tooltip = node.Text("tooltip");
    if (tooltip.empty()) {
        controls::ToolTipService::SetToolTip(control, nullptr);
    } else {
        controls::ToolTipService::SetToolTip(control, winrt::box_value(Wide(tooltip)));
        controls::ToolTipService::SetPlacement(control, TooltipPlacement(node.Text("tooltipPlacement", "top")));
        automation::AutomationProperties::SetHelpText(control, Wide(tooltip));
    }
    // tooltipDelay has no per-control setting in WinUI 3: the system delay applies.
}

void XamlRenderer::FillOptions(Mounted& mounted, const RenderNode& node) {
    auto combo = mounted.control ? mounted.control.try_as<controls::ComboBox>() : nullptr;
    if (!combo) return;
    UpdateGuard guard(*m_updating);
    std::string selected = node.Text("value");
    auto items = combo.Items();
    items.Clear();
    controls::ComboBoxItem chosen{nullptr};
    for (const RenderNode& option : node.children) {
        std::string value = option.Text("value", option.Text("label"));
        std::string label = option.Text("label", value);
        controls::ComboBoxItem item;
        item.Content(winrt::box_value(Wide(label)));
        item.Tag(winrt::box_value(Wide(value)));
        std::string name = uirender::ElementName(m_elementId, option.id);
        item.Name(Wide(name));
        automation::AutomationProperties::SetAutomationId(item, Wide(name));
        automation::AutomationProperties::SetName(item, Wide(label));
        items.Append(item);
        if (value == selected) chosen = item;
    }
    if (chosen && combo.SelectedItem() != chosen) {
        mounted.callbacks->echo = Json(selected).Dump();
        combo.SelectedItem(chosen);
    }
}

void XamlRenderer::Apply(Mounted& mounted, const Patch& patch, const StyleProbe& probe) {
    const RenderNode& node = *patch.node;
    if (patch.op == PatchOp::Update) SetAttributes(mounted, node, probe);

    if (node.type == "dropdown") {
        bool optionsChanged = patch.reordered;
        for (const Patch& child : patch.children) optionsChanged = optionsChanged || child.op != PatchOp::Keep;
        if (optionsChanged || patch.op == PatchOp::Update) FillOptions(mounted, node);
        return;
    }
    if (!mounted.host) return;

    bool rewrite = patch.reordered;
    std::vector<Mounted> next;
    next.reserve(patch.children.size());
    for (const Patch& child : patch.children) {
        bool reuse = child.from >= 0 && child.op != PatchOp::Create && child.op != PatchOp::Replace &&
                     child.from < static_cast<int>(mounted.children.size());
        if (reuse) {
            Mounted& old = mounted.children[child.from];
            // A row that changes between 2 children and another count changes its native panel: rebuilt.
            bool layoutChanged = child.node->type == "row" && NeedsGrid(*child.node) != old.grid;
            if (old.outer && !layoutChanged) {
                Apply(old, child, probe);
                next.push_back(std::move(old));
                continue;
            }
        }
        next.push_back(Build(*child.node, probe));
        rewrite = true;
    }
    mounted.children = std::move(next);
    if (rewrite) Rewrite(mounted);
}

void XamlRenderer::Rewrite(Mounted& mounted) {
    controls::Panel host = mounted.host;
    if (!host) return;
    auto children = host.Children();
    children.Clear();
    int column = 0;
    for (const Mounted& child : mounted.children) {
        xaml::FrameworkElement element = child.outer;
        if (!element) continue;
        children.Append(element);
        if (mounted.grid) {
            controls::Grid::SetColumn(element, column);
            if (column == 1) element.HorizontalAlignment(xaml::HorizontalAlignment::Right);
            element.VerticalAlignment(xaml::VerticalAlignment::Center);
        }
        column++;
    }
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
