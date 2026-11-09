#pragma once

// The native renderer: turns the interface trees of core/ui_model (Ampify.ui, Ampify.React) into WinUI controls, and
// updates them in place when the extension registers its tree again. Compiled only with AMPIFY_WITH_XAML. Every call
// happens on the interface thread.
//
// | primitive | control                                                                       |
// |-----------|-------------------------------------------------------------------------------|
// | stack     | StackPanel (vertical, Spacing = gap, 8 by default); Grid of rows with a growing child |
// | row       | Grid of 2 columns (Star, Auto) with 2 children, Grid of Auto/Star columns with |
// |           | a growing child, else horizontal StackPanel                                   |
// | list      | StackPanel (vertical, Spacing = gap, 4 by default)                            |
// | layer     | Grid of one cell: the children on top of each other, stretched by default     |
// | card      | Border (card brushes of the theme or of a native card) + title, subtitle      |
// | text      | TextBlock, style Title/Subtitle/Body/Caption TextBlockStyle                    |
// | button    | Button (accent template for primary, transparent for tertiary), icon + label  |
// | toggle    | ToggleSwitch on the right of its label (Grid of 2 columns)                    |
// | slider    | Slider with Header                                                            |
// | textfield | TextBox with Header and PlaceholderText                                       |
// | dropdown  | ComboBox with Header, one ComboBoxItem per option (Tag = value)               |
// | image     | Border painted by an ImageBrush (BitmapImage from an https or file address),  |
// |           | sized by an invisible Image of the same bitmap, so cornerRadius rounds it     |
// | icon      | Path shapes from the SVG (theme text brush), FontIcon for a glyph             |
// | grid      | Grid of `columns` equal Star columns, the children filled row by row, Auto rows |
// | tabs      | horizontal StackPanel of tab buttons: the one that is `selected` is primary    |
// | tab       | Button (primary while selected, tertiary otherwise), clicks like a button     |
// | menu      | Button (secondary) with a MenuFlyout: one MenuFlyoutItem per item node        |
// | item      | not a control of its own: a MenuFlyoutItem of its menu, clicks and `disabled` |
//
// The style attributes (src/core/ui_render/style.hpp): background (SolidColorBrush or in-app AcrylicBrush), padding,
// cornerRadius and the sizes on the panel or Border; align and valign as the element's own alignment, set by its
// parent; opacity, scale (UIElement.Scale around the centre) and transition (OpacityTransition, ScaleTransition,
// TranslationTransition), which the compositor animates.
//
// Every node gets x:Name and AutomationId "AMPIFY_<elementId>_<nodeId>" (uirender::ElementName), an AutomationProperties
// name, and its tooltip through ToolTipService. Clicks and changes go to the sink, which hands them to the
// JavaScript thread (ExtensionLoader::DeliverClickTo / DeliverEventTo).

#if defined(AMPIFY_WITH_XAML)

#include <windows.h>
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <winrt/Windows.Foundation.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

#include "../core/ui_render/render_tree.hpp"

namespace ampify::win {

// A click (onClick) or a new value (onChange) on a node, for the element that drew it. With `entry` set, another
// event of the element's dispatch ("fullscreenClose"), whose payload is `valueJson` as it is.
struct UiEvent {
    std::string elementId;
    int callbackId = 0;
    bool click = true;
    std::string valueJson;  // for a change: true, 42, "texte"
    std::string entry;
};
using UiEventSink = std::function<void(const UiEvent&)>;

// The look of the page, copied from native controls (the style probe): each new control takes the Style of a native
// control of the same kind, and a card takes the brushes, corner and padding of a native card. Everything left null
// falls back to the theme resources of WinUI (CardBackgroundFillColorDefaultBrush, CardStrokeColorDefaultBrush, the
// TextBlock styles), which follow the light and dark modes.
struct StyleProbe {
    winrt::Microsoft::UI::Xaml::Style button{nullptr};
    winrt::Microsoft::UI::Xaml::Style toggle{nullptr};
    winrt::Microsoft::UI::Xaml::Style comboBox{nullptr};
    winrt::Microsoft::UI::Xaml::Style slider{nullptr};
    winrt::Microsoft::UI::Xaml::Style textBox{nullptr};
    winrt::Microsoft::UI::Xaml::Style titleText{nullptr};    // first TextBlock of a native settings row
    winrt::Microsoft::UI::Xaml::Style captionText{nullptr};  // second TextBlock of that row
    double captionOpacity = 1;  // Apple dims that second TextBlock itself (0.6) rather than through its brush
    winrt::Microsoft::UI::Xaml::Media::Brush cardBackground{nullptr};
    winrt::Microsoft::UI::Xaml::Media::Brush cardStroke{nullptr};
    std::optional<winrt::Microsoft::UI::Xaml::Thickness> cardPadding;
    std::optional<winrt::Microsoft::UI::Xaml::CornerRadius> cardCorner;
    std::optional<winrt::Microsoft::UI::Xaml::Thickness> cardMargin;
    std::string origin;  // where the styles come from, for the log ("" when nothing was found)

    int Found() const;  // number of styles and card values found
};

// Fills `probe` from the native controls under `root` (bounded walk, 4000 nodes, outside item lists and outside the
// Ampify's and AMExclusive's elements). Keeps what `probe` already has when nothing better is found.
void CaptureStyleProbe(const winrt::Microsoft::UI::Xaml::DependencyObject& root, StyleProbe& probe,
                       const std::string& origin);

// A theme resource of the application (Application.Resources and its merged and theme dictionaries), or null.
winrt::Windows::Foundation::IInspectable ThemeResource(const wchar_t* key) noexcept;

// A card Border with the theme's card brushes ({ThemeResource}, so they follow the light and dark modes), or the
// probe's native card values when it has them.
winrt::Microsoft::UI::Xaml::Controls::Border MakeCardBorder(const StyleProbe& probe);

// A TextBlock in one of the type ramp styles ("title", "subtitle", "body", "caption", "bodyStrong"), secondary text
// brush for "caption" when `secondary` is set.
winrt::Microsoft::UI::Xaml::Controls::TextBlock MakeText(const std::wstring& text, const std::string& variant,
                                                         bool secondary, const StyleProbe* probe = nullptr);

// The XAML presentation namespace attribute, for LoadKitXaml texts.
const wchar_t* KitXmlns() noexcept;

// Loads XAML written by Ampify (never text of an extension), or null when the reader refuses it.
winrt::Windows::Foundation::IInspectable LoadKitXaml(const std::wstring& text) noexcept;

// An icon from SVG text (Path shapes filled with the theme brush `brushKey`) or a glyph (FontIcon), or null.
winrt::Microsoft::UI::Xaml::FrameworkElement MakeIconElement(const std::string& icon, double size, const wchar_t* brushKey);

// A one-pixel line with the theme's divider brush, between the rows of a card.
winrt::Microsoft::UI::Xaml::FrameworkElement MakeDivider();

// The template of a primary button: the accent brushes in every visual state. Apple Music's AccentButtonStyle draws
// its text in a symbol font (WebDings), so a primary button takes the plain button style with this template.
winrt::Microsoft::UI::Xaml::Controls::ControlTemplate PrimaryButtonTemplate();

// One surface (panel, page or popup) of one element. It holds its controls: destroy it on the interface thread.
class XamlRenderer {
public:
    XamlRenderer(std::string elementId, UiEventSink sink);
    ~XamlRenderer();

    XamlRenderer(const XamlRenderer&) = delete;
    XamlRenderer& operator=(const XamlRenderer&) = delete;

    // Renders `tree` (already checked by ValidateTree). The first call builds everything; the next ones update the
    // controls in place, by node id, and rebuild only a node whose type changed. Returns the root control, which is a
    // new object when the root itself was rebuilt (`rootReplaced`): the caller then puts it in place of the old one.
    winrt::Microsoft::UI::Xaml::FrameworkElement Render(uirender::RenderNode tree, const StyleProbe& probe,
                                                        uirender::PatchStats& stats, bool& rootReplaced);

    winrt::Microsoft::UI::Xaml::FrameworkElement Root() const;

    // The largest corner radius the tree's boxes and images take, from the next render on: a panel's content keeps
    // within the corners of its frame, square ones included (Corners snippet). No limit by default.
    void SetCornerLimit(double limit) { m_cornerLimit = limit; }

private:
    struct Mounted;
    struct Callbacks;
    struct Makers;   // the element of each node type (ui_renderer_build.cpp)
    struct Setters;  // the values of each node type (ui_renderer_attributes.cpp)

    Mounted Build(const uirender::RenderNode& node, const StyleProbe& probe);
    void Apply(Mounted& mounted, const uirender::Patch& patch, const StyleProbe& probe);
    void SetAttributes(Mounted& mounted, const uirender::RenderNode& node, const StyleProbe& probe, bool building);
    void SetBox(Mounted& mounted, const uirender::RenderNode& node);
    void SetMotion(Mounted& mounted, const uirender::RenderNode& node, bool building);
    void SetImage(Mounted& mounted, const uirender::RenderNode& node);
    void FillOptions(Mounted& mounted, const uirender::RenderNode& node);
    void FillMenu(Mounted& mounted, const uirender::RenderNode& node);
    void Rewrite(Mounted& mounted);
    void PlaceChildren(Mounted& mounted, const uirender::RenderNode& node);
    void SetGridCells(Mounted& mounted, const uirender::RenderNode& node);

    std::string m_elementId;
    UiEventSink m_sink;
    std::shared_ptr<int> m_updating;  // > 0 while the renderer sets values itself: the change events are ignored
    std::unique_ptr<uirender::RenderNode> m_tree;
    std::unique_ptr<Mounted> m_root;
    winrt::Microsoft::UI::Xaml::Controls::ControlTemplate m_primaryTemplate{nullptr};  // shared by the primary buttons
    double m_cornerLimit = std::numeric_limits<double>::infinity();
};

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
