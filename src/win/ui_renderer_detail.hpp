#pragma once
// The pieces of XamlRenderer that its files share (ui_renderer.cpp, ui_renderer_build.cpp,
// ui_renderer_attributes.cpp, ui_renderer_style.cpp): small helpers, and what a mounted element holds.

#if defined(AMPIFY_WITH_XAML)

#include "ui_renderer.hpp"
#include "ui_renderer_playing.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwchar>
#include <limits>
#include <utility>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Documents.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include "../core/i18n.hpp"
#include "../core/json.hpp"
#include "../core/player_queue.hpp"
#include "../core/ui_render/probe.hpp"
#include "../core/ui_render/style.hpp"
#include "../core/ui_vocab.hpp"
#include "../core/ui_render/svg_icon.hpp"
#include "xaml_host.hpp"

namespace ampify::win {

namespace renderer_detail {

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

inline winrt::hstring Wide(const std::string& text) {
    return winrt::to_hstring(text);
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

inline xaml::Style StyleResource(const wchar_t* key) noexcept {
    return ThemeResource(key).try_as<xaml::Style>();
}

inline media::Brush BrushResource(const wchar_t* key) noexcept {
    return ThemeResource(key).try_as<media::Brush>();
}

inline void SetText(const controls::TextBlock& block, const winrt::hstring& text) {
    if (block.Text() != text) block.Text(text);
}

inline void SetVisible(const xaml::UIElement& element, bool visible) {
    xaml::Visibility wanted = visible ? xaml::Visibility::Visible : xaml::Visibility::Collapsed;
    if (element.Visibility() != wanted) element.Visibility(wanted);
}

// A slider's header: its label (kept in Tag) and the value it is on, which WinUI shows only while dragging.
inline void ShowSliderValue(const controls::Slider& slider) {
    std::wstring label(winrt::unbox_value_or<winrt::hstring>(slider.Tag(), L""));
    double value = std::round(slider.Value() * 100) / 100;
    wchar_t number[32];
    std::swprintf(number, 32, L"%g", value);
    std::wstring text = label.empty() ? number : label + L" · " + number;
    slider.Header(winrt::box_value(winrt::hstring(text)));
}

inline const wchar_t* TextStyleKey(const std::string& variant) {
    if (variant == "title") return L"TitleTextBlockStyle";
    if (variant == "subtitle") return L"SubtitleTextBlockStyle";
    if (variant == "caption") return L"CaptionTextBlockStyle";
    if (variant == "bodyStrong") return L"BodyStrongTextBlockStyle";
    return L"BodyTextBlockStyle";
}

// An icon drawn as XAML shapes: the SVG paths in a Viewbox, filled with a theme brush, so the icon follows the light
// and dark modes. A glyph gives a FontIcon. Null when the icon cannot be drawn.
inline xaml::FrameworkElement MakeIcon(const std::string& icon, double size, const wchar_t* brushKey) {
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
inline winrt::Windows::Foundation::Uri ImageUri(const std::string& source) {
    for (const char* scheme : {"https://", "http://", "ms-appx:", "ms-appdata:", "file:"}) {
        if (source.rfind(scheme, 0) == 0) return winrt::Windows::Foundation::Uri(Wide(source));
    }
    std::string path = source;
    for (char& c : path) {
        if (c == '\\') c = '/';
    }
    return winrt::Windows::Foundation::Uri(Wide("file:///" + path));
}

inline primitives::PlacementMode TooltipPlacement(const std::string& placement) {
    if (placement == "bottom") return primitives::PlacementMode::Bottom;
    if (placement == "left") return primitives::PlacementMode::Left;
    if (placement == "right") return primitives::PlacementMode::Right;
    return primitives::PlacementMode::Top;
}

// The name read by screen readers: the label, the title, the text, or the tooltip.
inline std::string AccessibleName(const RenderNode& node) {
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

using uirender::Layout;

constexpr double kImageMaxHeight = 240;  // an image without fit or height
constexpr double kInfinity = std::numeric_limits<double>::infinity();
constexpr double kAuto = std::numeric_limits<double>::quiet_NaN();  // XAML's Width and Height when unset
constexpr winrt::Windows::UI::Color kThemedAcrylicTint{0xFF, 0x20, 0x20, 0x20};

inline winrt::Windows::UI::Color ToColor(const uirender::Rgba& color) {
    return winrt::Windows::UI::Color{color.a, color.r, color.g, color.b};
}

inline bool SameLength(double a, double b) {
    return (std::isnan(a) && std::isnan(b)) || a == b;
}

// A background attribute as a brush: solid, or an in-app AcrylicBrush that blurs what is under it in the window.
inline media::Brush MakeBackground(const std::string& text) {
    std::optional<uirender::Background> parsed = uirender::ParseBackground(text);
    if (!parsed) return nullptr;
    if (parsed->gradient) {
        media::LinearGradientBrush gradient;
        gradient.StartPoint({0, 0});
        gradient.EndPoint({0, 1});
        media::GradientStop top;
        top.Color(ToColor(parsed->color));
        top.Offset(0);
        media::GradientStop bottom;
        bottom.Color(ToColor(parsed->end));
        bottom.Offset(1);
        gradient.GradientStops().Append(top);
        gradient.GradientStops().Append(bottom);
        return gradient;
    }
    if (!parsed->acrylic) return media::SolidColorBrush(ToColor(parsed->color));
    if (parsed->themed) {
        if (auto themed = BrushResource(L"AcrylicInAppFillColorDefaultBrush")) return themed;
    }
    winrt::Windows::UI::Color tint = parsed->themed ? kThemedAcrylicTint : ToColor(parsed->color);
    media::AcrylicBrush acrylic;
    acrylic.TintColor(tint);
    acrylic.TintOpacity(parsed->tintOpacity);
    acrylic.FallbackColor(tint);
    return acrylic;
}

inline xaml::HorizontalAlignment HorizontalOf(const std::string& value, xaml::HorizontalAlignment fallback) {
    if (value == "start") return xaml::HorizontalAlignment::Left;
    if (value == "center") return xaml::HorizontalAlignment::Center;
    if (value == "end") return xaml::HorizontalAlignment::Right;
    if (value == "stretch") return xaml::HorizontalAlignment::Stretch;
    return fallback;
}

inline xaml::VerticalAlignment VerticalOf(const std::string& value, xaml::VerticalAlignment fallback) {
    if (value == "start") return xaml::VerticalAlignment::Top;
    if (value == "center") return xaml::VerticalAlignment::Center;
    if (value == "end") return xaml::VerticalAlignment::Bottom;
    if (value == "stretch") return xaml::VerticalAlignment::Stretch;
    return fallback;
}

struct Placement {
    xaml::HorizontalAlignment horizontal = xaml::HorizontalAlignment::Stretch;
    xaml::VerticalAlignment vertical = xaml::VerticalAlignment::Stretch;
};

// Where a node sits in its parent when it says nothing: images, icons, buttons and dropdowns on the left (the last two
// as their WinUI styles have it), rows centred vertically.
inline Placement TypePlacement(const RenderNode& node) {
    Placement placement;
    if (node.type == "image" || node.type == "icon" || node.type == "button" || node.type == "dropdown") {
        placement.horizontal = xaml::HorizontalAlignment::Left;
    }
    if (node.type == "row") placement.vertical = xaml::VerticalAlignment::Center;
    return placement;
}

// The node's own align and valign over the parent's defaults. On a text, align is the text alignment instead. The
// values are always set locally: a control outside the tree has no style yet, so comparing with what it reads would
// skip the write, and its style would decide until the next update wrote the value (buttons stretched after a click).
inline void Place(const xaml::FrameworkElement& element, const RenderNode& node, Placement placement) {
    if (node.type != "text") placement.horizontal = HorizontalOf(node.Text("align"), placement.horizontal);
    placement.vertical = VerticalOf(node.Text("valign"), placement.vertical);
    auto unset = xaml::DependencyProperty::UnsetValue();
    if (element.ReadLocalValue(xaml::FrameworkElement::HorizontalAlignmentProperty()) == unset ||
        element.HorizontalAlignment() != placement.horizontal) {
        element.HorizontalAlignment(placement.horizontal);
    }
    if (element.ReadLocalValue(xaml::FrameworkElement::VerticalAlignmentProperty()) == unset ||
        element.VerticalAlignment() != placement.vertical) {
        element.VerticalAlignment(placement.vertical);
    }
}

inline void SetPanelBackground(const xaml::FrameworkElement& element, const media::Brush& brush) {
    if (auto panel = element.try_as<controls::Panel>()) panel.Background(brush);
    else if (auto border = element.try_as<controls::Border>()) border.Background(brush);
}

inline void SetPanelPadding(const xaml::FrameworkElement& element, const xaml::Thickness& padding) {
    if (auto stack = element.try_as<controls::StackPanel>()) stack.Padding(padding);
    else if (auto grid = element.try_as<controls::Grid>()) grid.Padding(padding);
    else if (auto border = element.try_as<controls::Border>()) border.Padding(padding);
}

inline void SetPanelCorner(const xaml::FrameworkElement& element, const xaml::CornerRadius& corner) {
    if (auto stack = element.try_as<controls::StackPanel>()) stack.CornerRadius(corner);
    else if (auto grid = element.try_as<controls::Grid>()) grid.CornerRadius(corner);
    else if (auto border = element.try_as<controls::Border>()) border.CornerRadius(corner);
}

inline void ClearLocal(const xaml::DependencyObject& object, const xaml::DependencyProperty& property) {
    if (object.ReadLocalValue(property) != xaml::DependencyProperty::UnsetValue()) object.ClearValue(property);
}

inline winrt::Windows::Foundation::TimeSpan Milliseconds(double ms) {
    return std::chrono::milliseconds(static_cast<std::int64_t>(ms));
}

inline media::Stretch StretchOf(const std::string& fit) {
    if (fit == "cover") return media::Stretch::UniformToFill;
    if (fit == "fill") return media::Stretch::Fill;
    return media::Stretch::Uniform;
}

}  // namespace renderer_detail

using namespace renderer_detail;
using namespace playing_views;

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
    Layout layout = Layout::None;  // how the native container lays out the children (uirender::LayoutOf)
    std::vector<bool> cells;       // Layout::Grid: the growing children the cell definitions were made for
    int columns = 0;               // Layout::Columns (grid): the columns and rows the cell definitions were made for
    int rows = 0;
    // What the style attributes last set, so an update only touches what changed and a removed one is undone.
    std::string background;
    std::string transition;
    std::string source;            // image address
    bool colored = false;          // a text whose color the renderer set
    bool centered = false;         // the scale centre follows the size
    double baseOpacity = 1;        // a dimmed secondary text: the opacity attribute scales it
    media::Brush baseBackground{nullptr};  // a card's own look, put back when the attribute goes
    xaml::Thickness basePadding{};
    xaml::CornerRadius baseCorner{};
    // All strong: in Apple Music a weak reference to a XAML object Ampify made never resolves, even while the object
    // is on screen (seen on 1.1792: the slot of a panel was lost right after it was made). The renderer is released
    // on the interface thread with its surface, and no handler holds these objects, so there is no cycle.
    xaml::FrameworkElement outer{nullptr};    // what the parent holds
    xaml::FrameworkElement control{nullptr};  // what takes the attributes, the name and the tooltip
    controls::Panel host{nullptr};            // where the children go
    controls::TextBlock label{nullptr};       // the label of a toggle, the title of a card
    controls::TextBlock subtitle{nullptr};    // the subtitle of a card
    media::ImageBrush brush{nullptr};         // an image: drawn by its frame's brush, so the corners can be rounded
    controls::Image sizer{nullptr};           // an image: invisible, it gives the frame the picture's own size
    std::shared_ptr<ArtworkFollow> artwork;   // an image showing the song playing
    std::shared_ptr<LyricsView> lyrics;       // a lyrics node
    std::shared_ptr<Callbacks> callbacks;
    std::vector<Mounted> children;
};

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
