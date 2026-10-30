#pragma once

// The native renderer: turns the interface trees of core/ui_model (AMK.ui, AMK.React) into WinUI controls, and
// updates them in place when the extension registers its tree again. Compiled only with AMK_WITH_XAML. Every call
// happens on the interface thread. Not run yet in Apple Music: see docs/tests/M8-interface.md.
//
// | primitive | control                                                                       |
// |-----------|-------------------------------------------------------------------------------|
// | stack     | StackPanel (vertical, Spacing = gap, 8 by default)                            |
// | row       | Grid of 2 columns (Star, Auto) with 2 children, else horizontal StackPanel    |
// | list      | StackPanel (vertical, Spacing = gap, 4 by default)                            |
// | card      | Border (card brushes of the theme or of a native card) + title, subtitle      |
// | text      | TextBlock, style Title/Subtitle/Body/Caption TextBlockStyle                    |
// | button    | Button (AccentButtonStyle for primary, transparent for tertiary), icon + label|
// | toggle    | ToggleSwitch on the right of its label (Grid of 2 columns)                    |
// | slider    | Slider with Header                                                            |
// | textfield | TextBox with Header and PlaceholderText                                       |
// | dropdown  | ComboBox with Header, one ComboBoxItem per option (Tag = value)               |
// | image     | Image (BitmapImage from an https or file address)                             |
// | icon      | Path shapes from the SVG (theme text brush), FontIcon for a glyph             |
//
// Every node gets x:Name and AutomationId "AMK_<elementId>_<nodeId>" (uirender::ElementName), an AutomationProperties
// name, and its tooltip through ToolTipService. Clicks and changes go to the sink, which hands them to the
// JavaScript thread (ExtensionLoader::DeliverClickTo / DeliverEventTo).

#if defined(AMK_WITH_XAML)

#include <windows.h>
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include <functional>
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

namespace amk::win {

// A click (onClick) or a new value (onChange) on a node, for the element that drew it.
struct UiEvent {
    std::string elementId;
    int callbackId = 0;
    bool click = true;
    std::string valueJson;  // for a change: true, 42, "texte"
};
using UiEventSink = std::function<void(const UiEvent&)>;

// The look of the page, copied from native controls (the style probe): each new control takes the Style of a native
// control of the same kind, and a card takes the brushes, corner and padding of a native card. Everything left null
// falls back to the theme resources of WinUI (CardBackgroundFillColorDefaultBrush, CardStrokeColorDefaultBrush,
// AccentButtonStyle, the TextBlock styles), which follow the light and dark modes.
struct StyleProbe {
    winrt::Microsoft::UI::Xaml::Style button{nullptr};
    winrt::Microsoft::UI::Xaml::Style toggle{nullptr};
    winrt::Microsoft::UI::Xaml::Style comboBox{nullptr};
    winrt::Microsoft::UI::Xaml::Style slider{nullptr};
    winrt::Microsoft::UI::Xaml::Style textBox{nullptr};
    winrt::Microsoft::UI::Xaml::Style titleText{nullptr};    // first TextBlock of a native settings row
    winrt::Microsoft::UI::Xaml::Style captionText{nullptr};  // second TextBlock of that row
    winrt::Microsoft::UI::Xaml::Media::Brush cardBackground{nullptr};
    winrt::Microsoft::UI::Xaml::Media::Brush cardStroke{nullptr};
    std::optional<winrt::Microsoft::UI::Xaml::Thickness> cardPadding;
    std::optional<winrt::Microsoft::UI::Xaml::CornerRadius> cardCorner;
    std::optional<winrt::Microsoft::UI::Xaml::Thickness> cardMargin;
    std::string origin;  // where the styles come from, for the journal ("" when nothing was found)

    int Found() const;  // number of styles and card values found
};

// Fills `probe` from the native controls under `root` (bounded walk, 4000 nodes, outside item lists and outside the
// kit's and AMExclusive's elements). Keeps what `probe` already has when nothing better is found.
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

// Loads XAML written by the kit (never text of an extension), or null when the reader refuses it.
winrt::Windows::Foundation::IInspectable LoadKitXaml(const std::wstring& text) noexcept;

// An icon from SVG text (Path shapes filled with the theme brush `brushKey`) or a glyph (FontIcon), or null.
winrt::Microsoft::UI::Xaml::FrameworkElement MakeIconElement(const std::string& icon, double size, const wchar_t* brushKey);

// A one-pixel line with the theme's divider brush, between the rows of a card.
winrt::Microsoft::UI::Xaml::FrameworkElement MakeDivider();

// One surface (panel, page or popup) of one element.
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

private:
    struct Mounted;
    struct Callbacks;

    Mounted Build(const uirender::RenderNode& node, const StyleProbe& probe);
    void Apply(Mounted& mounted, const uirender::Patch& patch, const StyleProbe& probe);
    void SetAttributes(Mounted& mounted, const uirender::RenderNode& node, const StyleProbe& probe);
    void FillOptions(Mounted& mounted, const uirender::RenderNode& node);
    void Rewrite(Mounted& mounted);

    std::string m_elementId;
    UiEventSink m_sink;
    std::shared_ptr<int> m_updating;  // > 0 while the renderer sets values itself: the change events are ignored
    std::unique_ptr<uirender::RenderNode> m_tree;
    std::unique_ptr<Mounted> m_root;
};

}  // namespace amk::win

#endif  // AMK_WITH_XAML
