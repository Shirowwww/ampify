// Written against the C++/WinRT projection of Windows App SDK 1.8.
// The style probe: Apple Music's own look, copied onto Ampify's elements. The style copying follows AMExclusive (MIT,
// https://github.com/AntiOblivionis/AMExclusive, mod/src/UiAdapter.cpp, AttachOrRefreshToggle, at 91f589d): native
// Style, margins and padding first, WinUI card resources otherwise.

#if defined(AMPIFY_WITH_XAML)

#include "ui_renderer_detail.hpp"

namespace ampify::win {

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

namespace {

// The style of one native control, kept when it is the first of its kind (the last toggle: its row gives the card).
// True when the node is such a control, which the walk then does not enter; `found` counts what it gave.
bool TakeControlStyle(const xaml::DependencyObject& node, StyleProbe& probe, controls::ToggleSwitch& nativeToggle,
                      int& found) {
    if (auto toggle = node.try_as<controls::ToggleSwitch>()) {
        if (toggle.Style()) probe.toggle = toggle.Style();
        nativeToggle = toggle;  // the last one: its row gives the card values below
        found++;
        return true;
    }
    if (auto combo = node.try_as<controls::ComboBox>()) {
        if (combo.Style() && !probe.comboBox) probe.comboBox = combo.Style();
        found++;
        return true;
    }
    if (auto slider = node.try_as<controls::Slider>()) {
        if (slider.Style() && !probe.slider) probe.slider = slider.Style();
        found++;
        return true;
    }
    if (auto box = node.try_as<controls::TextBox>()) {
        if (box.Style() && !probe.textBox) probe.textBox = box.Style();
        found++;
        return true;
    }
    if (auto button = node.try_as<controls::Button>()) {
        // Only a button that shows a text: an icon button of the title bar would give a wrong look.
        bool textual = !winrt::unbox_value_or<winrt::hstring>(button.Content(), L"").empty();
        if (textual && button.Style() && !probe.button) probe.button = button.Style();
        if (textual) found++;
        return true;
    }
    return false;
}

// The two first texts of a settings row, a toggle's own On/Off text left out.
std::vector<controls::TextBlock> RowTexts(const controls::Border& row) {
    std::vector<controls::TextBlock> texts;
    std::vector<xaml::DependencyObject> pending{row};
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
    return texts;
}

// The card of a native settings row: the widest Border with a background between the toggle and the root, and the
// two first TextBlocks of that row for the title and the description.
void TakeRowCard(const xaml::DependencyObject& root, const controls::ToggleSwitch& nativeToggle, StyleProbe& probe) {
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
    if (!best) return;
    probe.cardBackground = best.Background();
    probe.cardStroke = best.BorderBrush() ? best.BorderBrush() : BrushResource(L"CardStrokeColorDefaultBrush");
    probe.cardPadding = best.Padding();
    probe.cardCorner = best.CornerRadius();
    probe.cardMargin = best.Margin();
    std::vector<controls::TextBlock> texts = RowTexts(best);
    if (!texts.empty() && texts[0].Style()) probe.titleText = texts[0].Style();
    if (texts.size() > 1 && texts[1].Style()) {
        probe.captionText = texts[1].Style();
        probe.captionOpacity = texts[1].Opacity();
    }
}

}  // namespace

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
            if (element && uirender::IsAmpifyOrModName(Utf8(element.Name()))) continue;
            if (node.try_as<controls::ItemsControl>() || node.try_as<controls::ItemsRepeater>()) continue;
            if (TakeControlStyle(node, probe, nativeToggle, found)) continue;
            int count = media::VisualTreeHelper::GetChildrenCount(node);
            for (int index = count - 1; index >= 0; index--) stack.push_back(media::VisualTreeHelper::GetChild(node, index));
        } catch (...) {
            // An element that refuses a read is left out of the probe.
        }
    }
    if (nativeToggle) {
        try {
            TakeRowCard(root, nativeToggle, probe);
        } catch (...) {
            // An element that refuses a read is left out of the probe.
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

// A primary button: the accent brushes in every visual state. Under Apple Music's button style the pointer brought back
// the plain button's brushes, and a brush set directly stayed the one read when the button was drawn (a dark accent in
// a light window after a theme change). {ThemeResource} follows the light and dark modes and the theme's colours.
controls::ControlTemplate PrimaryButtonTemplate() {
    return LoadXaml<controls::ControlTemplate>(
        L"<ControlTemplate " + std::wstring(kXmlns) +
        L" xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml' TargetType='Button'>"
        L"<Grid x:Name='Root' Background='{ThemeResource AccentFillColorDefaultBrush}'"
        L" BorderBrush='{ThemeResource AccentControlElevationBorderBrush}' BorderThickness='{TemplateBinding BorderThickness}'"
        L" CornerRadius='{TemplateBinding CornerRadius}' Padding='{TemplateBinding Padding}'>"
        L"<VisualStateManager.VisualStateGroups><VisualStateGroup x:Name='CommonStates'>"
        L"<VisualState x:Name='Normal'/>"
        L"<VisualState x:Name='PointerOver'><VisualState.Setters>"
        L"<Setter Target='Root.Background' Value='{ThemeResource AccentFillColorSecondaryBrush}'/>"
        L"</VisualState.Setters></VisualState>"
        L"<VisualState x:Name='Pressed'><VisualState.Setters>"
        L"<Setter Target='Root.Background' Value='{ThemeResource AccentFillColorTertiaryBrush}'/>"
        L"<Setter Target='Label.Foreground' Value='{ThemeResource TextOnAccentFillColorSecondaryBrush}'/>"
        L"</VisualState.Setters></VisualState>"
        L"<VisualState x:Name='Disabled'><VisualState.Setters>"
        L"<Setter Target='Root.Background' Value='{ThemeResource AccentFillColorDisabledBrush}'/>"
        L"<Setter Target='Root.BorderBrush' Value='{ThemeResource ControlFillColorTransparentBrush}'/>"
        L"<Setter Target='Label.Foreground' Value='{ThemeResource TextOnAccentFillColorDisabledBrush}'/>"
        L"</VisualState.Setters></VisualState>"
        L"</VisualStateGroup></VisualStateManager.VisualStateGroups>"
        L"<ContentPresenter x:Name='Label' Content='{TemplateBinding Content}'"
        L" Foreground='{ThemeResource TextOnAccentFillColorPrimaryBrush}' FontFamily='{TemplateBinding FontFamily}'"
        L" FontSize='{TemplateBinding FontSize}' FontWeight='{TemplateBinding FontWeight}'"
        L" HorizontalAlignment='{TemplateBinding HorizontalContentAlignment}'"
        L" VerticalAlignment='{TemplateBinding VerticalContentAlignment}'/>"
        L"</Grid></ControlTemplate>");
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
    // Dimmed as Apple's own descriptions: as the native row the probe found, else as those of its Settings (0.6).
    constexpr double kAppleCaptionOpacity = 0.6;
    if (probe && variant == "caption" && probe->captionText) {
        style = probe->captionText;
        block.Opacity(probe->captionOpacity);
    } else if (secondary) {
        block.Opacity(kAppleCaptionOpacity);
    }
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

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
