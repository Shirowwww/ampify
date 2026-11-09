// Written against the C++/WinRT projection of Windows App SDK 1.8.
// The element of each node type, built once; SetAttributes then gives it its values. One maker per type, found in
// one table: a new component of Ampify.ui is a row there and its maker.

#if defined(AMPIFY_WITH_XAML)

#include "ui_renderer_detail.hpp"

#include "xaml_chrome.hpp"

namespace ampify::win {

// The makers reach the renderer's private Mounted and Callbacks: a nested struct of XamlRenderer.
struct XamlRenderer::Makers {
    // What a maker gives back: the element its parent holds, and the one that takes the node's values.
    struct Made {
        xaml::FrameworkElement outer{nullptr};
        xaml::FrameworkElement control{nullptr};
    };
    using Callback = std::shared_ptr<Callbacks>;
    using Maker = Made (*)(Mounted& mounted, const RenderNode& node, const StyleProbe& probe, const Callback& callbacks);

    static Maker For(const std::string& type);
    static void ClickOnKeys(const Callback& callbacks, const xaml::UIElement& element);
    static void LightUp(const Callback& callbacks, const controls::Border& card);
    static Made Container(Mounted& mounted, const RenderNode& node, const StyleProbe& probe, const Callback& callbacks);
    static Made Card(Mounted& mounted, const RenderNode& node, const StyleProbe& probe, const Callback& callbacks);
    static Made Text(Mounted& mounted, const RenderNode& node, const StyleProbe& probe, const Callback& callbacks);
    static Made Button(Mounted& mounted, const RenderNode& node, const StyleProbe& probe, const Callback& callbacks);
    static Made Toggle(Mounted& mounted, const RenderNode& node, const StyleProbe& probe, const Callback& callbacks);
    static Made Slider(Mounted& mounted, const RenderNode& node, const StyleProbe& probe, const Callback& callbacks);
    static Made TextField(Mounted& mounted, const RenderNode& node, const StyleProbe& probe, const Callback& callbacks);
    static Made Dropdown(Mounted& mounted, const RenderNode& node, const StyleProbe& probe, const Callback& callbacks);
    static Made Image(Mounted& mounted, const RenderNode& node, const StyleProbe& probe, const Callback& callbacks);
    static Made Lyrics(Mounted& mounted, const RenderNode& node, const StyleProbe& probe, const Callback& callbacks);
    static Made Icon(Mounted& mounted, const RenderNode& node, const StyleProbe& probe, const Callback& callbacks);
    static Made Progress(Mounted& mounted, const RenderNode& node, const StyleProbe& probe, const Callback& callbacks);
    static Made Checkbox(Mounted& mounted, const RenderNode& node, const StyleProbe& probe, const Callback& callbacks);
    static Made Divider(Mounted& mounted, const RenderNode& node, const StyleProbe& probe, const Callback& callbacks);
    static Made Spinner(Mounted& mounted, const RenderNode& node, const StyleProbe& probe, const Callback& callbacks);
};

namespace {

// Two columns: the first takes the room, the second its own width.
controls::Grid MakePairGrid() {
    controls::Grid grid;
    controls::ColumnDefinition first;
    first.Width(xaml::GridLength{1.0, xaml::GridUnitType::Star});
    controls::ColumnDefinition second;
    second.Width(xaml::GridLength{1.0, xaml::GridUnitType::Auto});
    grid.ColumnDefinitions().Append(first);
    grid.ColumnDefinitions().Append(second);
    return grid;
}

controls::Panel MakeContainer(const RenderNode& node, Layout layout) {
    if (layout == Layout::Panel) {
        controls::StackPanel panel;
        if (node.type == "row" || node.type == "tabs") panel.Orientation(controls::Orientation::Horizontal);
        return panel;
    }
    if (layout == Layout::Pair) return MakePairGrid();
    return controls::Grid();
}

}  // namespace

// Every node type of Ampify.ui and its maker. An option outside a dropdown is refused by ValidateTree and drawn empty,
// as an icon frame, like any type this table does not know.
XamlRenderer::Makers::Maker XamlRenderer::Makers::For(const std::string& type) {
    struct Row {
        std::string_view type;
        Maker maker;
    };
    static constexpr Row kRows[] = {
        {"stack", Container}, {"list", Container},    {"row", Container},       {"layer", Container},
        {"grid", Container},  {"tabs", Container},    {"card", Card},           {"text", Text},
        {"button", Button},   {"tab", Button},        {"menu", Button},         {"toggle", Toggle},
        {"slider", Slider},   {"textfield", TextField}, {"dropdown", Dropdown}, {"image", Image},
        {"lyrics", Lyrics},   {"icon", Icon},         {"progress", Progress},   {"checkbox", Checkbox},
        {"divider", Divider}, {"spinner", Spinner},
    };
    for (const Row& row : kRows) {
        if (row.type == type) return row.maker;
    }
    return Icon;
}

// A card, picture or icon that answers a click answers Enter and Space too, once Tab reaches it (SetAttributes).
void XamlRenderer::Makers::ClickOnKeys(const Callback& callbacks, const xaml::UIElement& element) {
    element.KeyDown([callbacks](IInspectable const& sender, xaml::Input::KeyRoutedEventArgs const& args) noexcept {
        try {
            auto key = args.Key();
            if (key != winrt::Windows::System::VirtualKey::Enter && key != winrt::Windows::System::VirtualKey::Space) return;
            // Keys bubble up from a text field or a button inside a card: only the element's own focus clicks it.
            if (args.OriginalSource() != sender || callbacks->onClick <= 0) return;
            args.Handled(true);
            callbacks->Fire(true, {});
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });
}

// A card that answers a click lights up under the pointer, as the rows of Apple's lists do, and while it has the
// keyboard's focus, which a Border shows no other way.
void XamlRenderer::Makers::LightUp(const Callback& callbacks, const controls::Border& card) {
    // The light comes and goes in a short fade, as every change of Ampify's interface does (none with Windows'
    // animation effects off). A brush other than a plain colour still changes at once.
    constexpr auto kLightFade = std::chrono::milliseconds(150);
    if (MotionAllowed()) {
        xaml::BrushTransition fade;
        fade.Duration(kLightFade);
        card.BackgroundTransition(fade);
    }
    auto resting = std::make_shared<xaml::Media::Brush>(nullptr);
    auto light = [callbacks, resting](IInspectable const& sender) noexcept {
        try {
            auto border = sender.as<controls::Border>();
            auto hover = ThemeResource(L"SubtleFillColorSecondaryBrush").try_as<xaml::Media::Brush>();
            if (callbacks->onClick <= 0 || !hover || *resting) return;
            *resting = border.Background();
            border.Background(hover);
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    };
    auto unlight = [resting](IInspectable const& sender) noexcept {
        try {
            if (!*resting) return;
            sender.as<controls::Border>().Background(*resting);
            *resting = nullptr;
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    };
    card.PointerEntered([light](IInspectable const& sender, xaml::Input::PointerRoutedEventArgs const&) noexcept { light(sender); });
    card.PointerExited([unlight](IInspectable const& sender, xaml::Input::PointerRoutedEventArgs const&) noexcept { unlight(sender); });
    // Focus bubbles up from a switch or a button inside the card: only the card's own focus lights it.
    card.GotFocus([light](IInspectable const& sender, xaml::RoutedEventArgs const& args) noexcept {
        if (args.OriginalSource() == sender) light(sender);
    });
    card.LostFocus([unlight](IInspectable const& sender, xaml::RoutedEventArgs const& args) noexcept {
        if (args.OriginalSource() == sender) unlight(sender);
    });
}

XamlRenderer::Makers::Made XamlRenderer::Makers::Container(Mounted& mounted, const RenderNode& node, const StyleProbe&,
                                                            const Callback&) {
    controls::Panel panel = MakeContainer(node, mounted.layout);
    mounted.host = panel;
    return {panel, panel};
}

XamlRenderer::Makers::Made XamlRenderer::Makers::Card(Mounted& mounted, const RenderNode&, const StyleProbe& probe,
                                                       const Callback& callbacks) {
    controls::Border card = MakeCardBorder(probe);
    mounted.baseBackground = card.Background();
    mounted.basePadding = card.Padding();
    mounted.baseCorner = card.CornerRadius();
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
    // A tap answered here goes no further: a clickable picture inside a clickable card (the Marketplace's) fired both.
    card.Tapped([callbacks](IInspectable const&, xaml::Input::TappedRoutedEventArgs const& args) noexcept {
        if (callbacks->onClick <= 0) return;
        args.Handled(true);
        callbacks->Fire(true, {});
    });
    ClickOnKeys(callbacks, card);
    LightUp(callbacks, card);
    mounted.host = host;
    mounted.label = title;
    mounted.subtitle = subtitle;
    return {card, card};
}

XamlRenderer::Makers::Made XamlRenderer::Makers::Text(Mounted& mounted, const RenderNode& node, const StyleProbe& probe,
                                                       const Callback&) {
    controls::TextBlock block = MakeText(L"", node.Text("variant", "body"), node.Text("variant") == "caption", &probe);
    mounted.baseOpacity = block.Opacity();
    return {block, block};
}

XamlRenderer::Makers::Made XamlRenderer::Makers::Button(Mounted&, const RenderNode&, const StyleProbe&,
                                                         const Callback& callbacks) {
    controls::Button button;
    button.Click([callbacks](IInspectable const&, xaml::RoutedEventArgs const&) noexcept { callbacks->Fire(true, {}); });
    return {button, button};
}

XamlRenderer::Makers::Made XamlRenderer::Makers::Toggle(Mounted& mounted, const RenderNode&, const StyleProbe& probe,
                                                         const Callback& callbacks) {
    controls::Grid grid = MakePairGrid();
    grid.ColumnSpacing(12);
    controls::TextBlock label = MakeText(L"", "body", false, &probe);
    label.VerticalAlignment(xaml::VerticalAlignment::Center);
    controls::ToggleSwitch toggle;
    if (probe.toggle) toggle.Style(probe.toggle);
    toggle.VerticalAlignment(xaml::VerticalAlignment::Center);
    toggle.HorizontalAlignment(xaml::HorizontalAlignment::Right);
    toggle.MinWidth(0);
    controls::Grid::SetColumn(toggle, 1);
    // The states in Ampify's language (Apple Music's own words in French), not in whatever Windows would pick.
    toggle.OnContent(winrt::box_value(winrt::to_hstring(i18n::Text("ui.on"))));
    toggle.OffContent(winrt::box_value(winrt::to_hstring(i18n::Text("ui.off"))));
    grid.Children().Append(label);
    grid.Children().Append(toggle);
    toggle.Toggled([callbacks](IInspectable const& sender, xaml::RoutedEventArgs const&) noexcept {
        try {
            bool on = sender.as<controls::ToggleSwitch>().IsOn();
            callbacks->Fire(false, on ? "true" : "false");
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });
    mounted.label = label;
    return {grid, toggle};
}

XamlRenderer::Makers::Made XamlRenderer::Makers::Slider(Mounted&, const RenderNode&, const StyleProbe& probe,
                                                         const Callback& callbacks) {
    controls::Slider slider;
    if (probe.slider) slider.Style(probe.slider);
    slider.ValueChanged([callbacks](IInspectable const& sender, primitives::RangeBaseValueChangedEventArgs const& args) noexcept {
        try {
            ShowSliderValue(sender.as<controls::Slider>());
            callbacks->Fire(false, Json(args.NewValue()).Dump());
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });
    return {slider, slider};
}

XamlRenderer::Makers::Made XamlRenderer::Makers::TextField(Mounted&, const RenderNode&, const StyleProbe& probe,
                                                            const Callback& callbacks) {
    controls::TextBox box;
    if (probe.textBox) box.Style(probe.textBox);
    box.TextChanged([callbacks](IInspectable const& sender, controls::TextChangedEventArgs const&) noexcept {
        try {
            callbacks->Fire(false, Json(Utf8(sender.as<controls::TextBox>().Text())).Dump());
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });
    return {box, box};
}

XamlRenderer::Makers::Made XamlRenderer::Makers::Dropdown(Mounted&, const RenderNode&, const StyleProbe& probe,
                                                           const Callback& callbacks) {
    controls::ComboBox combo;
    if (probe.comboBox) combo.Style(probe.comboBox);
    combo.MinWidth(200);
    combo.SelectionChanged([callbacks](IInspectable const& sender, controls::SelectionChangedEventArgs const&) noexcept {
        try {
            auto item = sender.as<controls::ComboBox>().SelectedItem().try_as<controls::ComboBoxItem>();
            if (!item) return;
            callbacks->Fire(false, Json(Utf8(winrt::unbox_value_or<winrt::hstring>(item.Tag(), L""))).Dump());
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });
    return {combo, combo};
}

// The picture is the frame's ImageBrush, so cornerRadius rounds it (a Border does not clip an Image child). The
// invisible Image shares the same bitmap and gives the frame the size the picture would take. The grid around it
// takes the layer that fades the previous cover out (src "artwork").
XamlRenderer::Makers::Made XamlRenderer::Makers::Image(Mounted& mounted, const RenderNode&, const StyleProbe&,
                                                        const Callback& callbacks) {
    controls::Border frame;
    media::ImageBrush brush;
    controls::Image sizer;
    sizer.Opacity(0);
    controls::Grid layers;
    layers.Children().Append(sizer);
    frame.Child(layers);
    frame.Background(brush);
    frame.Tapped([callbacks](IInspectable const&, xaml::Input::TappedRoutedEventArgs const& args) noexcept {
        if (callbacks->onClick <= 0) return;  // a picture that answers no click lets the tap reach its card
        args.Handled(true);
        callbacks->Fire(true, {});
    });
    ClickOnKeys(callbacks, frame);
    mounted.brush = brush;
    mounted.sizer = sizer;
    return {frame, frame};
}

XamlRenderer::Makers::Made XamlRenderer::Makers::Lyrics(Mounted& mounted, const RenderNode&, const StyleProbe&,
                                                         const Callback& callbacks) {
    mounted.lyrics = std::make_shared<LyricsView>();
    mounted.lyrics->pick = [callbacks](int64_t startMs) { callbacks->Fire(false, Json(static_cast<double>(startMs)).Dump()); };
    xaml::FrameworkElement view = MakeLyricsView(mounted.lyrics);
    return {view, view};
}

// Holds the drawn icon, which is rebuilt when the SVG changes.
XamlRenderer::Makers::Made XamlRenderer::Makers::Icon(Mounted&, const RenderNode&, const StyleProbe&,
                                                       const Callback& callbacks) {
    controls::Border frame;
    frame.Background(media::SolidColorBrush(winrt::Windows::UI::Color{0, 0, 0, 0}));  // so a tap anywhere counts
    frame.Tapped([callbacks](IInspectable const&, xaml::Input::TappedRoutedEventArgs const& args) noexcept {
        if (callbacks->onClick <= 0) return;
        args.Handled(true);
        callbacks->Fire(true, {});
    });
    ClickOnKeys(callbacks, frame);
    return {frame, frame};
}

// WinUI's own bar, in the accent colour as Apple's are.
XamlRenderer::Makers::Made XamlRenderer::Makers::Progress(Mounted&, const RenderNode&, const StyleProbe&, const Callback&) {
    controls::ProgressBar bar;
    bar.MinWidth(120);
    return {bar, bar};
}

XamlRenderer::Makers::Made XamlRenderer::Makers::Checkbox(Mounted&, const RenderNode&, const StyleProbe&,
                                                           const Callback& callbacks) {
    controls::CheckBox box;
    auto report = [callbacks](IInspectable const& sender, xaml::RoutedEventArgs const&) noexcept {
        try {
            auto checked = sender.as<controls::CheckBox>().IsChecked();
            callbacks->Fire(false, checked && checked.Value() ? "true" : "false");
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    };
    box.Checked(report);
    box.Unchecked(report);
    return {box, box};
}

XamlRenderer::Makers::Made XamlRenderer::Makers::Divider(Mounted&, const RenderNode&, const StyleProbe&, const Callback&) {
    xaml::FrameworkElement line = MakeDivider();
    return {line, line};
}

XamlRenderer::Makers::Made XamlRenderer::Makers::Spinner(Mounted&, const RenderNode&, const StyleProbe&, const Callback&) {
    controls::ProgressRing ring;
    ring.IsActive(true);
    return {ring, ring};
}

XamlRenderer::Mounted XamlRenderer::Build(const RenderNode& node, const StyleProbe& probe) {
    Mounted mounted;
    mounted.type = node.type;
    mounted.layout = uirender::LayoutOf(node);
    mounted.callbacks = std::make_shared<Callbacks>();
    mounted.callbacks->elementId = m_elementId;
    mounted.callbacks->sink = m_sink;
    mounted.callbacks->updating = m_updating;

    Makers::Made made = Makers::For(node.type)(mounted, node, probe, mounted.callbacks);
    mounted.outer = made.outer;
    mounted.control = made.control;
    std::string name = uirender::ElementName(m_elementId, node.id);
    made.control.Name(Wide(name));
    automation::AutomationProperties::SetAutomationId(made.control, Wide(name));
    if (made.outer != made.control) made.outer.Name(Wide(name + "_frame"));

    SetAttributes(mounted, node, probe, true);
    if (node.type == "dropdown") {
        FillOptions(mounted, node);
    } else if (node.type == "menu") {
        FillMenu(mounted, node);
    } else if (mounted.host) {
        for (const RenderNode& child : node.children) mounted.children.push_back(Build(child, probe));
        Rewrite(mounted);
        PlaceChildren(mounted, node);
    }
    return mounted;
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
