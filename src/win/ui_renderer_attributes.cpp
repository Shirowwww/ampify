// Written against the C++/WinRT projection of Windows App SDK 1.8.
// A node's attributes on its element: text, look, motion, picture and options.

#if defined(AMPIFY_WITH_XAML)

#include "ui_renderer_detail.hpp"

namespace ampify::win {

// The Fluent type ramp's size and weight for a title and a subtitle. Apple Music's resources leave these two styles
// at body size, so a title came out as plain text: the values go on the text itself.
namespace {

struct RampStep {
    double size;
    std::uint16_t weight;
};

std::optional<RampStep> RampOf(const std::string& variant) {
    if (variant == "title") return RampStep{28, 600};
    if (variant == "subtitle") return RampStep{20, 600};
    return std::nullopt;
}

}  // namespace

// size, weight, color, align, wrap and maxLines of a text. A removed attribute gives the text style's value back.
static void SetTextStyle(const controls::TextBlock& block, const RenderNode& node, bool& colored) {
    std::optional<RampStep> ramp = RampOf(node.Text("variant"));
    if (node.Has("size")) block.FontSize(node.Number("size", 14));
    else if (ramp) block.FontSize(ramp->size);
    else ClearLocal(block, controls::TextBlock::FontSizeProperty());
    if (node.Has("weight")) {
        block.FontWeight(winrt::Windows::UI::Text::FontWeight{static_cast<std::uint16_t>(node.Number("weight", 400))});
    } else if (ramp) {
        block.FontWeight(winrt::Windows::UI::Text::FontWeight{ramp->weight});
    } else {
        ClearLocal(block, controls::TextBlock::FontWeightProperty());
    }
    if (std::optional<uirender::Rgba> color = uirender::ParseColor(node.Text("color"))) {
        block.Foreground(media::SolidColorBrush(ToColor(*color)));
        colored = true;
    } else if (colored) {
        // A caption was made with the secondary brush: it gets it back, the other texts their style's brush.
        if (node.Text("variant") == "caption") block.Foreground(BrushResource(L"TextFillColorSecondaryBrush"));
        else ClearLocal(block, controls::TextBlock::ForegroundProperty());
        colored = false;
    }
    std::string align = node.Text("align");
    if (align == "center") block.TextAlignment(xaml::TextAlignment::Center);
    else if (align == "right") block.TextAlignment(xaml::TextAlignment::Right);
    else if (align == "left") block.TextAlignment(xaml::TextAlignment::Left);
    else ClearLocal(block, controls::TextBlock::TextAlignmentProperty());
    bool wrap = node.Bool("wrap", true);
    xaml::TextWrapping wrapping = wrap ? xaml::TextWrapping::Wrap : xaml::TextWrapping::NoWrap;
    if (block.TextWrapping() != wrapping) block.TextWrapping(wrapping);
    int lines = static_cast<int>(node.Number("maxLines", 0));
    if (block.MaxLines() != lines) block.MaxLines(lines);
    xaml::TextTrimming trimming = lines > 0 || !wrap ? xaml::TextTrimming::CharacterEllipsis : xaml::TextTrimming::None;
    if (block.TextTrimming() != trimming) block.TextTrimming(trimming);
}

// inert: neither the pointer nor Tab reaches the node (BuildRenderTree marks its whole subtree). An element at opacity 0
// still takes clicks in XAML, so a faded control needs this to let them through. Cards, images and icons set their
// own tab stop from it (SetAttributes).
static bool SetInert(const xaml::FrameworkElement& outer, const xaml::FrameworkElement& control, const RenderNode& node) {
    bool inert = node.Bool("inert", false);
    for (const xaml::FrameworkElement& element : {outer, control}) {
        if (element && element.IsHitTestVisible() == inert) element.IsHitTestVisible(!inert);
    }
    const bool ownTabStop = node.type == "card" || node.type == "image" || node.type == "icon";
    if (control && !ownTabStop) {
        if (inert) control.IsTabStop(false);
        else ClearLocal(control, xaml::UIElement::IsTabStopProperty());
    }
    return inert;
}

// opacity, scale and transition, on any node. While building, the values go first so that nothing animates in; on
// an update, the transitions go first so that the new values animate on the compositor thread.
void XamlRenderer::SetMotion(Mounted& mounted, const RenderNode& node, bool building) {
    xaml::FrameworkElement element = mounted.outer;
    auto values = [&] {
        double opacity = node.Number("opacity", 1) * mounted.baseOpacity;
        if (element.Opacity() != opacity) element.Opacity(opacity);
        float scale = static_cast<float>(node.Number("scale", 1));
        winrt::Windows::Foundation::Numerics::float3 wanted{scale, scale, 1};
        if (element.Scale() != wanted) element.Scale(wanted);
        if (scale != 1 && !mounted.centered) {
            // Around the centre: the centre follows the size, which is not known before the first layout.
            mounted.centered = true;
            element.SizeChanged([](IInspectable const& sender, xaml::SizeChangedEventArgs const& args) noexcept {
                try {
                    auto size = args.NewSize();
                    sender.as<xaml::UIElement>().CenterPoint({size.Width / 2, size.Height / 2, 0});
                } catch (...) {
                    // A handler runs inside Apple Music's XAML frame: nothing may escape it.
                }
            });
            element.CenterPoint({static_cast<float>(element.ActualWidth() / 2), static_cast<float>(element.ActualHeight() / 2), 0});
        }
    };
    auto transitions = [&] {
        std::string text = node.Text("transition");
        if (text == mounted.transition) return;
        mounted.transition = text;
        uirender::Transitions parsed = uirender::ParseTransition(text).value_or(uirender::Transitions{});
        xaml::ScalarTransition opacity{nullptr};
        if (parsed.opacityMs) {
            opacity = xaml::ScalarTransition();
            opacity.Duration(Milliseconds(*parsed.opacityMs));
        }
        element.OpacityTransition(opacity);
        xaml::Vector3Transition scale{nullptr};
        if (parsed.scaleMs) {
            scale = xaml::Vector3Transition();
            scale.Duration(Milliseconds(*parsed.scaleMs));
        }
        element.ScaleTransition(scale);
        xaml::Vector3Transition translation{nullptr};
        if (parsed.translationMs) {
            translation = xaml::Vector3Transition();
            translation.Duration(Milliseconds(*parsed.translationMs));
        }
        element.TranslationTransition(translation);
    };
    if (building) {
        values();
        transitions();
    } else {
        transitions();
        values();
    }
}

// The values of each node type on its element, found in one table as the makers are (ui_renderer_build.cpp).
struct XamlRenderer::Setters {
    struct Context {
        const StyleProbe& probe;
        bool building;
        controls::ControlTemplate& primaryTemplate;  // the renderer's, shared by the primary buttons
    };
    using Setter = void (*)(Mounted& mounted, const RenderNode& node, const Context& context);

    static Setter For(const std::string& type);
    static void Gap(Mounted& mounted, const RenderNode& node, const Context& context);
    static void Card(Mounted& mounted, const RenderNode& node, const Context& context);
    static void Lyrics(Mounted& mounted, const RenderNode& node, const Context& context);
    static void Text(Mounted& mounted, const RenderNode& node, const Context& context);
    static void ButtonLook(const controls::Button& button, const std::string& variant, const Context& context);
    static void Button(Mounted& mounted, const RenderNode& node, const Context& context);
    static void Toggle(Mounted& mounted, const RenderNode& node, const Context& context);
    static void Slider(Mounted& mounted, const RenderNode& node, const Context& context);
    static void TextField(Mounted& mounted, const RenderNode& node, const Context& context);
    static void Dropdown(Mounted& mounted, const RenderNode& node, const Context& context);
    static void Icon(Mounted& mounted, const RenderNode& node, const Context& context);
    static void Progress(Mounted& mounted, const RenderNode& node, const Context& context);
    static void Checkbox(Mounted& mounted, const RenderNode& node, const Context& context);
    static void Spinner(Mounted& mounted, const RenderNode& node, const Context& context);
    static void Accessibility(const xaml::FrameworkElement& control, const RenderNode& node);
};

// A type with no row (layer, grid, image: SetImage) has no values of its own here.
XamlRenderer::Setters::Setter XamlRenderer::Setters::For(const std::string& type) {
    struct Row {
        std::string_view type;
        Setter setter;
    };
    static constexpr Row kRows[] = {
        {"stack", Gap},       {"list", Gap},         {"row", Gap},          {"tabs", Gap},
        {"card", Card},       {"lyrics", Lyrics},    {"text", Text},        {"button", Button},
        {"tab", Button},      {"menu", Button},      {"toggle", Toggle},    {"slider", Slider},
        {"textfield", TextField}, {"dropdown", Dropdown}, {"icon", Icon},
        {"progress", Progress},   {"checkbox", Checkbox}, {"spinner", Spinner},
    };
    for (const Row& row : kRows) {
        if (row.type == type) return row.setter;
    }
    return nullptr;
}

void XamlRenderer::Setters::Gap(Mounted& mounted, const RenderNode& node, const Context&) {
    const std::string& type = node.type;
    double gap = node.Number("gap", uivocab::DefaultGap(type, 8));
    if (auto panel = mounted.control.try_as<controls::StackPanel>()) {
        if (panel.Spacing() != gap) panel.Spacing(gap);
    }
    if (auto grid = mounted.control.try_as<controls::Grid>()) {
        if (type == "row" && grid.ColumnSpacing() != gap) grid.ColumnSpacing(gap);
        if (type != "row" && grid.RowSpacing() != gap) grid.RowSpacing(gap);
    }
}

void XamlRenderer::Setters::Card(Mounted& mounted, const RenderNode& node, const Context&) {
    if (auto title = mounted.label) {
        SetText(title, Wide(node.Text("title")));
        SetVisible(title, node.Has("title"));
    }
    if (auto subtitle = mounted.subtitle) {
        SetText(subtitle, Wide(node.Text("subtitle")));
        SetVisible(subtitle, node.Has("subtitle"));
    }
}

void XamlRenderer::Setters::Lyrics(Mounted& mounted, const RenderNode& node, const Context& context) {
    if (!mounted.lyrics) return;
    LyricsView& view = *mounted.lyrics;
    double size = node.Number("size", kLyricsSize);
    winrt::Windows::UI::Color color{255, 255, 255, 255};
    if (std::optional<uirender::Rgba> parsed = uirender::ParseColor(node.Text("color"))) color = ToColor(*parsed);
    if (size != view.size || color != view.color) {
        view.size = size;
        view.color = color;
        view.lines.Spacing(size * 0.6);
        view.backIcon.Foreground(media::SolidColorBrush(color));
        view.stale = true;
    }
    SetText(view.placeholder, Wide(node.Text("placeholder")));
    if (context.building) PaintLyrics(view);
}

void XamlRenderer::Setters::Text(Mounted& mounted, const RenderNode& node, const Context&) {
    auto block = mounted.control.as<controls::TextBlock>();
    SetText(block, Wide(node.Text("text")));
    xaml::Style style = StyleResource(TextStyleKey(node.Text("variant", "body")));
    if (style && block.Style() != style) block.Style(style);
    SetTextStyle(block, node, mounted.colored);
}

// Apple Music's own button style for every variant: its AccentButtonStyle draws text in a symbol font (the label came
// out as icons on 1.1792). A primary button takes the accent colours on top instead.
void XamlRenderer::Setters::ButtonLook(const controls::Button& button, const std::string& variant, const Context& context) {
    if (context.probe.button && button.Style() != context.probe.button) button.Style(context.probe.button);
    if (variant == "primary" && !context.primaryTemplate) context.primaryTemplate = PrimaryButtonTemplate();
    if (variant == "primary" && context.primaryTemplate) {
        if (button.Template() != context.primaryTemplate) button.Template(context.primaryTemplate);
    } else {
        button.ClearValue(controls::Control::TemplateProperty());  // a button that was primary before
    }
    if (variant == "tertiary") {
        button.Background(media::SolidColorBrush(winrt::Windows::UI::Color{0, 0, 0, 0}));
        button.BorderThickness(xaml::Thickness{0, 0, 0, 0});
    } else {
        button.ClearValue(controls::Control::BackgroundProperty());  // a button that was tertiary before
        button.ClearValue(controls::Control::BorderThicknessProperty());
    }
}

void XamlRenderer::Setters::Button(Mounted& mounted, const RenderNode& node, const Context& context) {
    auto button = mounted.control.as<controls::Button>();
    std::string variant = uirender::ButtonVariantOf(node);
    ButtonLook(button, variant, context);
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
}

void XamlRenderer::Setters::Toggle(Mounted& mounted, const RenderNode& node, const Context&) {
    auto toggle = mounted.control.as<controls::ToggleSwitch>();
    bool checked = node.Bool("checked", node.Bool("value", false));
    if (toggle.IsOn() != checked) {
        mounted.callbacks->echo = checked ? "true" : "false";
        toggle.IsOn(checked);
    }
    if (auto label = mounted.label) {
        SetText(label, Wide(node.Text("label")));
        SetVisible(label, !node.Text("label").empty());
    }
}

void XamlRenderer::Setters::Slider(Mounted& mounted, const RenderNode& node, const Context&) {
    auto slider = mounted.control.as<controls::Slider>();
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
    slider.Tag(winrt::box_value(Wide(node.Text("label"))));
    ShowSliderValue(slider);
}

void XamlRenderer::Setters::TextField(Mounted& mounted, const RenderNode& node, const Context&) {
    auto box = mounted.control.as<controls::TextBox>();
    winrt::hstring value = Wide(node.Text("value"));
    if (node.Has("value") && box.Text() != value) {
        mounted.callbacks->echo = Json(Utf8(value)).Dump();
        box.Text(value);
    }
    box.PlaceholderText(Wide(node.Text("placeholder")));
    std::string label = node.Text("label");
    box.Header(label.empty() ? nullptr : winrt::box_value(Wide(label)));
}

void XamlRenderer::Setters::Dropdown(Mounted& mounted, const RenderNode& node, const Context&) {
    auto combo = mounted.control.as<controls::ComboBox>();
    std::string label = node.Text("label");
    combo.Header(label.empty() ? nullptr : winrt::box_value(Wide(label)));
}

void XamlRenderer::Setters::Icon(Mounted& mounted, const RenderNode& node, const Context&) {
    auto frame = mounted.control.as<controls::Border>();
    frame.Child(MakeIcon(node.Text("icon"), node.Number("size", 20), L"TextFillColorPrimaryBrush"));
}

// Without a value the bar moves on its own; with one it fills to value / max, max 100 by default.
void XamlRenderer::Setters::Progress(Mounted& mounted, const RenderNode& node, const Context&) {
    auto bar = mounted.control.as<controls::ProgressBar>();
    const bool known = !std::isnan(node.Number("value", std::numeric_limits<double>::quiet_NaN()));
    if (bar.IsIndeterminate() == known) bar.IsIndeterminate(!known);
    double maximum = node.Number("max", 100);
    if (maximum <= 0) maximum = 100;
    double value = std::clamp(node.Number("value", 0), 0.0, maximum);
    if (bar.Maximum() != maximum) bar.Maximum(maximum);
    if (bar.Value() != value) bar.Value(value);
}

void XamlRenderer::Setters::Checkbox(Mounted& mounted, const RenderNode& node, const Context&) {
    auto box = mounted.control.as<controls::CheckBox>();
    bool checked = node.Bool("checked", node.Bool("value", false));
    auto now = box.IsChecked();
    if (!now || now.Value() != checked) {
        mounted.callbacks->echo = checked ? "true" : "false";
        box.IsChecked(checked);
    }
    winrt::hstring label = Wide(node.Text("label"));
    if (winrt::unbox_value_or<winrt::hstring>(box.Content(), L"") != label || !box.Content()) {
        box.Content(winrt::box_value(label));
    }
}

void XamlRenderer::Setters::Spinner(Mounted& mounted, const RenderNode& node, const Context&) {
    auto ring = mounted.control.as<controls::ProgressRing>();
    double size = std::clamp(node.Number("size", 32), 16.0, 128.0);
    if (ring.Width() != size) ring.Width(size);
    if (ring.Height() != size) ring.Height(size);
}

// The name screen readers say and the tooltip. tooltipDelay has no per-control setting in WinUI 3: the system delay
// applies.
void XamlRenderer::Setters::Accessibility(const xaml::FrameworkElement& control, const RenderNode& node) {
    std::string accessible = AccessibleName(node);
    if (node.type != "text") automation::AutomationProperties::SetName(control, Wide(accessible));
    std::string tooltip = node.Text("tooltip");
    if (tooltip.empty()) {
        controls::ToolTipService::SetToolTip(control, nullptr);
    } else {
        controls::ToolTipService::SetToolTip(control, winrt::box_value(Wide(tooltip)));
        controls::ToolTipService::SetPlacement(control, TooltipPlacement(node.Text("tooltipPlacement", "top")));
        automation::AutomationProperties::SetHelpText(control, Wide(tooltip));
    }
}

void XamlRenderer::SetAttributes(Mounted& mounted, const RenderNode& node, const StyleProbe& probe, bool building) {
    xaml::FrameworkElement control = mounted.control;
    if (!control) return;
    mounted.callbacks->onClick = static_cast<int>(node.Number("onClick", 0));
    mounted.callbacks->onChange = static_cast<int>(node.Number("onChange", 0));
    bool disabled = node.Bool("disabled", false);
    if (auto asControl = control.try_as<controls::Control>()) {
        if (asControl.IsEnabled() == disabled) asControl.IsEnabled(!disabled);
    }
    if (mounted.outer) SetMotion(mounted, node, building);
    bool inert = SetInert(mounted.outer, control, node);

    const std::string& type = node.type;
    if (type == "card" || type == "image" || type == "icon") {
        bool clickable = mounted.callbacks->onClick > 0 && !disabled && !inert;
        if (control.IsTabStop() != clickable) control.IsTabStop(clickable);
    }
    if (type == "stack" || type == "row" || type == "layer" || type == "card") SetBox(mounted, node);
    if (type == "image") SetImage(mounted, node);
    if (Setters::Setter setter = Setters::For(type)) setter(mounted, node, {probe, building, m_primaryTemplate});
    Setters::Accessibility(control, node);
}

// src, fit, cornerRadius, width and height. The bitmap is loaded again only when the address changes, so a fade of
// the image does not reload it. `src: "artwork"` shows the song playing (FollowPlayingArtwork).
void XamlRenderer::SetImage(Mounted& mounted, const RenderNode& node) {
    auto frame = mounted.control.as<controls::Border>();
    std::string source = node.Text("src");
    if (source != mounted.source && mounted.artwork) {
        DropFollow(frame, *mounted.artwork);
        mounted.artwork.reset();
    }
    std::optional<ampify::AppleArtwork> apple = ampify::ParseAppleArtworkSource(source);
    if (source != mounted.source && mounted.brush && mounted.sizer && apple && !apple->queueItem) {
        mounted.source = source;
        mounted.artwork = FollowPlayingArtwork(frame, mounted.brush, mounted.sizer);
    } else if (source != mounted.source && mounted.brush && mounted.sizer && apple) {
        mounted.source = source;
        mounted.brush.ClearValue(media::ImageBrush::ImageSourceProperty());
        mounted.sizer.ClearValue(controls::Image::SourceProperty());
        mounted.artwork = ShowQueueArtwork(mounted.brush, mounted.sizer, *apple->queueItem);
    } else if (source != mounted.source && mounted.brush && mounted.sizer) {
        mounted.source = source;
        mounted.brush.ClearValue(media::ImageBrush::ImageSourceProperty());
        mounted.sizer.ClearValue(controls::Image::SourceProperty());
        media::Imaging::BitmapImage bitmap{nullptr};
        try {
            if (!source.empty()) bitmap = media::Imaging::BitmapImage(ImageUri(source));
        } catch (...) {
            bitmap = nullptr;  // an address that is not a URI: nothing drawn
        }
        mounted.brush.ImageSource(bitmap);
        mounted.sizer.Source(bitmap);
    }
    std::string fit = node.Text("fit");
    media::Stretch stretch = StretchOf(fit);
    if (mounted.brush && mounted.brush.Stretch() != stretch) mounted.brush.Stretch(stretch);
    if (mounted.sizer && mounted.sizer.Stretch() != stretch) mounted.sizer.Stretch(stretch);
    double radius = std::min(node.Number("cornerRadius", 0), m_cornerLimit);
    xaml::CornerRadius corner{radius, radius, radius, radius};
    if (frame.CornerRadius() != corner) frame.CornerRadius(corner);
    if (mounted.artwork) {
        mounted.artwork->corner = corner;
        if (ArtworkFade& fade = mounted.artwork->fade; fade.brush && fade.brush.Stretch() != stretch) fade.brush.Stretch(stretch);
    }
    double width = node.Number("width", kAuto);
    double height = node.Number("height", kAuto);
    double maxHeight = node.Has("height") || !fit.empty() ? kInfinity : kImageMaxHeight;
    if (!SameLength(frame.Width(), width)) frame.Width(width);
    if (!SameLength(frame.Height(), height)) frame.Height(height);
    if (frame.MaxHeight() != maxHeight) frame.MaxHeight(maxHeight);
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

// The entries of a menu: one MenuFlyoutItem per item node, each with its own click handler. The flyout opens from the
// menu's button (a Button with a Flyout), and is built again whenever the items change, as the options of a dropdown.
void XamlRenderer::FillMenu(Mounted& mounted, const RenderNode& node) {
    auto button = mounted.control ? mounted.control.try_as<controls::Button>() : nullptr;
    if (!button) return;
    controls::MenuFlyout flyout;
    // A flyout opens above its button by default (Top), over what sits there, such as the tabs above a menu.
    flyout.Placement(primitives::FlyoutPlacementMode::BottomEdgeAlignedLeft);
    for (const RenderNode& entry : node.children) {
        std::string label = entry.Text("label");
        controls::MenuFlyoutItem item;
        item.Text(Wide(label));
        item.IsEnabled(!entry.Bool("disabled", false));
        std::string name = uirender::ElementName(m_elementId, entry.id);
        item.Name(Wide(name));
        automation::AutomationProperties::SetAutomationId(item, Wide(name));
        automation::AutomationProperties::SetName(item, Wide(label));
        auto callbacks = std::make_shared<Callbacks>();
        callbacks->elementId = m_elementId;
        callbacks->sink = m_sink;
        callbacks->updating = m_updating;
        callbacks->onClick = static_cast<int>(entry.Number("onClick", 0));
        item.Click([callbacks](IInspectable const&, xaml::RoutedEventArgs const&) noexcept { callbacks->Fire(true, {}); });
        flyout.Items().Append(item);
    }
    button.Flyout(flyout);
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
