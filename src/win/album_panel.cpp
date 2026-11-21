#if defined(AMPIFY_WITH_XAML)

#include "album_panel.hpp"

#include <winrt/Microsoft.UI.Text.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.h>

#include <algorithm>
#include <memory>

#include "../core/i18n.hpp"

namespace ampify::win {

namespace {

namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = xaml::Controls;
namespace media = xaml::Media;

constexpr double kArtwork = 200;
constexpr double kGutter = 28;
constexpr double kPadding = 24;
constexpr double kRowHeight = 34;
constexpr double kNumberWidth = 36;
constexpr double kTwoColumnsFrom = 520;  // the list's width from which a long album splits it in two
constexpr size_t kTwoColumnsAfter = 8;   // tracks
constexpr wchar_t kIconFont[] = L"Segoe Fluent Icons, Segoe MDL2 Assets";
constexpr wchar_t kPlayGlyph[] = L"";
constexpr wchar_t kShuffleGlyph[] = L"";
constexpr wchar_t kCloseGlyph[] = L"";
constexpr wchar_t kOpenGlyph[] = L"";
constexpr wchar_t kSpeakerGlyph[] = L"\xE767";  // Volume: the song that plays
// The text colour's alpha for the panel's background, a row under the pointer and the row picked.
constexpr uint8_t kPanelAlpha = 0x10;
constexpr uint8_t kHoverAlpha = 0x14;
constexpr uint8_t kPickedAlpha = 0x26;
// In the album's colours, the header's buttons: their fill at rest and under the pointer.
constexpr uint8_t kButtonAlpha = 0x1A;
constexpr uint8_t kButtonHoverAlpha = 0x2C;
constexpr double kDim = 0.6;

std::wstring Text(const char* key) { return std::wstring(winrt::to_hstring(i18n::Text(key))); }

media::SolidColorBrush Tint(const media::Brush& text, uint8_t alpha) {
    winrt::Windows::UI::Color color{0xFF, 0xFF, 0xFF, 0xFF};
    if (auto solid = text.try_as<media::SolidColorBrush>()) color = solid.Color();
    color.A = alpha;
    return media::SolidColorBrush(color);
}

media::SolidColorBrush Solid(const Rgb& color) {
    return media::SolidColorBrush(winrt::Windows::UI::Color{0xFF, color.r, color.g, color.b});
}

// The panel's buttons in the album's colours. Theirs come from the app's theme, which need not read on the album's
// background; a button looks these keys up from itself to the panel before the theme (lightweight styling).
void PaintButtons(const xaml::ResourceDictionary& resources, const media::Brush& text) {
    auto put = [&resources](const wchar_t* key, const media::Brush& brush) {
        resources.Insert(winrt::box_value(winrt::hstring(key)), brush);
    };
    for (const wchar_t* key : {L"ButtonForeground", L"ButtonForegroundPointerOver", L"ButtonForegroundPressed"}) {
        put(key, text);
    }
    put(L"ButtonBackground", Tint(text, kButtonAlpha));
    put(L"ButtonBackgroundPointerOver", Tint(text, kButtonHoverAlpha));
    put(L"ButtonBackgroundPressed", Tint(text, kHoverAlpha));
    for (const wchar_t* key : {L"ButtonBorderBrush", L"ButtonBorderBrushPointerOver", L"ButtonBorderBrushPressed"}) {
        put(key, Tint(text, 0));
    }
}

controls::TextBlock Label(const std::wstring& text, double size, double opacity = 1.0) {
    controls::TextBlock block;
    block.Text(text);
    block.FontSize(size);
    block.Opacity(opacity);
    block.TextTrimming(xaml::TextTrimming::CharacterEllipsis);
    block.TextWrapping(xaml::TextWrapping::NoWrap);
    block.VerticalAlignment(xaml::VerticalAlignment::Center);
    return block;
}

controls::FontIcon Glyph(const wchar_t* glyph, double size) {
    controls::FontIcon icon;
    icon.Glyph(glyph);
    icon.FontFamily(media::FontFamily(kIconFont));
    icon.FontSize(size);
    return icon;
}

// A button of the header: a glyph and, unless empty, a text.
controls::Button HeaderButton(const wchar_t* glyph, const std::wstring& text, std::function<void()> click) {
    controls::StackPanel content;
    content.Orientation(controls::Orientation::Horizontal);
    content.Spacing(8);
    content.Children().Append(Glyph(glyph, 13));
    if (!text.empty()) content.Children().Append(Label(text, 14));
    controls::Button button;
    button.Content(content);
    button.Padding(xaml::ThicknessHelper::FromLengths(14, 6, 14, 6));
    xaml::Automation::AutomationProperties::SetName(button, winrt::hstring(text));
    button.Click([click = std::move(click)](auto&&, auto&&) {
        if (click) click();
    });
    return button;
}

controls::Button GlyphButton(const wchar_t* glyph, const std::wstring& name, std::function<void()> click) {
    controls::Button button;
    button.Content(Glyph(glyph, 12));
    button.Padding(xaml::ThicknessHelper::FromUniformLength(6));
    button.Background(media::SolidColorBrush(winrt::Windows::UI::Color{}));
    button.BorderThickness(xaml::ThicknessHelper::FromUniformLength(0));
    xaml::Automation::AutomationProperties::SetName(button, winrt::hstring(name));
    button.Click([click = std::move(click)](auto&&, auto&&) {
        if (click) click();
    });
    return button;
}

// A track's right-click menu, Apple's entries for a song of an album (issue #7).
controls::MenuFlyout TrackMenu(int index, const AlbumPanelActions& actions, const std::function<void()>& play) {
    controls::MenuFlyout menu;
    auto add = [&menu](const std::wstring& text, std::function<void()> run) {
        controls::MenuFlyoutItem entry;
        entry.Text(winrt::hstring(text));
        entry.Click([run = std::move(run)](auto&&, auto&&) {
            if (run) run();
        });
        menu.Items().Append(entry);
    };
    add(Text("album.play"), play);
    if (actions.queue) {
        add(Text("album.playNext"), [actions, index] { actions.queue(index, true); });
        add(Text("album.playLater"), [actions, index] { actions.queue(index, false); });
    }
    if (actions.open) {
        menu.Items().Append(controls::MenuFlyoutSeparator());
        add(Text("album.open"), actions.open);
    }
    return menu;
}

// Which row is picked, shared by the rows of one panel.
struct Picked {
    controls::Grid row{nullptr};
};

controls::Grid TrackRow(const AlbumPanelTrack& track, int index, const AlbumPanelActions& actions,
                        const media::Brush& text, const std::shared_ptr<Picked>& picked) {
    controls::Grid row;
    row.Height(kRowHeight);
    row.CornerRadius(xaml::CornerRadiusHelper::FromUniformRadius(6));
    row.Padding(xaml::ThicknessHelper::FromLengths(4, 0, 10, 0));
    media::SolidColorBrush clear(winrt::Windows::UI::Color{});
    media::SolidColorBrush hover = Tint(text, kHoverAlpha);
    media::SolidColorBrush pick = Tint(text, kPickedAlpha);
    row.Background(clear);  // hit-testable everywhere, not only on its text
    for (xaml::GridLength width : {xaml::GridLengthHelper::FromPixels(kNumberWidth),
                                   xaml::GridLengthHelper::FromValueAndType(1, xaml::GridUnitType::Star),
                                   xaml::GridLengthHelper::Auto()}) {
        controls::ColumnDefinition column;
        column.Width(width);
        row.ColumnDefinitions().Append(column);
    }

    // The track's number, or a speaker for the song that plays (issue #7).
    const double markerOpacity = track.playing ? 1.0 : kDim;
    xaml::FrameworkElement number = track.playing ? xaml::FrameworkElement(Glyph(kSpeakerGlyph, 13))
                                                  : xaml::FrameworkElement(Label(track.number, 13, kDim));
    number.HorizontalAlignment(xaml::HorizontalAlignment::Center);
    number.VerticalAlignment(xaml::VerticalAlignment::Center);
    auto play = [actions, index] {
        if (actions.play) actions.play(index, false);
    };
    controls::Button start = GlyphButton(kPlayGlyph, Text("album.play") + L": " + track.title, play);
    start.HorizontalAlignment(xaml::HorizontalAlignment::Center);
    start.Opacity(0);  // shown under the pointer or with the keyboard's focus, in the number's place
    row.Children().Append(number);
    row.Children().Append(start);

    controls::StackPanel names;
    names.Orientation(controls::Orientation::Horizontal);
    names.Spacing(10);
    names.VerticalAlignment(xaml::VerticalAlignment::Center);
    controls::TextBlock name = Label(track.title, 14);
    if (track.playing) name.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
    names.Children().Append(name);
    if (!track.artist.empty()) names.Children().Append(Label(track.artist, 14, kDim));
    controls::Grid::SetColumn(names, 1);
    row.Children().Append(names);

    controls::TextBlock duration = Label(track.duration, 13, kDim);
    duration.Margin(xaml::ThicknessHelper::FromLengths(12, 0, 0, 0));
    controls::Grid::SetColumn(duration, 2);
    row.Children().Append(duration);

    auto show = [number, start, markerOpacity](bool under) {
        number.Opacity(under ? 0 : markerOpacity);
        start.Opacity(under ? 1 : 0);
    };
    auto background = [row, clear, hover, pick, picked](bool under) {
        row.Background(picked->row == row ? pick : under ? hover : clear);
    };
    row.PointerEntered([show, background](auto&&, auto&&) {
        show(true);
        background(true);
    });
    row.PointerExited([show, background](auto&&, auto&&) {
        show(false);
        background(false);
    });
    start.GotFocus([show](auto&&, auto&&) { show(true); });
    start.LostFocus([show](auto&&, auto&&) { show(false); });
    row.Tapped([row, clear, pick, picked](auto&&, auto&&) {
        if (picked->row && picked->row != row) picked->row.Background(clear);
        picked->row = row;
        row.Background(pick);
    });
    row.DoubleTapped([play](auto&&, xaml::Input::DoubleTappedRoutedEventArgs const& args) {
        args.Handled(true);
        play();
    });
    row.ContextFlyout(TrackMenu(index, actions, play));
    xaml::Automation::AutomationProperties::SetName(row, winrt::hstring(track.title));
    return row;
}

controls::Grid TrackList(const AlbumPanelModel& model, const AlbumPanelActions& actions, const media::Brush& text,
                         double width) {
    controls::Grid list;
    const size_t count = model.tracks.size();
    const size_t columns = count > kTwoColumnsAfter && width >= kTwoColumnsFrom ? 2 : 1;
    const size_t perColumn = (count + columns - 1) / columns;
    auto picked = std::make_shared<Picked>();
    list.ColumnSpacing(24);
    for (size_t c = 0; c < columns; c++) {
        controls::ColumnDefinition column;
        column.Width(xaml::GridLengthHelper::FromValueAndType(1, xaml::GridUnitType::Star));
        list.ColumnDefinitions().Append(column);
        controls::StackPanel rows;
        for (size_t i = c * perColumn; i < std::min(count, (c + 1) * perColumn); i++) {
            if (!model.tracks[i].section.empty()) {
                controls::TextBlock heading = Label(model.tracks[i].section, 13, kDim);
                heading.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
                heading.Margin(xaml::ThicknessHelper::FromLengths(4, i == c * perColumn ? 0 : 10, 0, 4));
                rows.Children().Append(heading);
            }
            rows.Children().Append(TrackRow(model.tracks[i], static_cast<int>(i), actions, text, picked));
        }
        controls::Grid::SetColumn(rows, static_cast<int32_t>(c));
        list.Children().Append(rows);
    }
    return list;
}

xaml::FrameworkElement Cover(const media::ImageSource& artwork) {
    controls::Border frame;
    frame.Width(kArtwork);
    frame.Height(kArtwork);
    frame.CornerRadius(xaml::CornerRadiusHelper::FromUniformRadius(6));
    frame.VerticalAlignment(xaml::VerticalAlignment::Top);
    if (artwork) {
        controls::Image image;
        image.Source(artwork);
        image.Stretch(media::Stretch::UniformToFill);
        frame.Child(image);
    }
    return frame;
}

}  // namespace

xaml::FrameworkElement MakeAlbumPanel(const AlbumPanelModel& model, const AlbumPanelActions& actions,
                                      const media::Brush& gridText, double width) {
    // In the album's colours, its text colour stands for the grid's everywhere: rows under the pointer, the row picked.
    const media::Brush text = model.colors ? media::Brush(Solid(model.colors->text)) : gridText;
    controls::Grid layout;
    for (double column : {kArtwork, kGutter, -1.0}) {
        controls::ColumnDefinition definition;
        definition.Width(column > 0 ? xaml::GridLengthHelper::FromPixels(column)
                                    : xaml::GridLengthHelper::FromValueAndType(1, xaml::GridUnitType::Star));
        layout.ColumnDefinitions().Append(definition);
    }
    layout.Children().Append(Cover(model.artwork));

    controls::StackPanel side;
    controls::Grid::SetColumn(side, 2);
    controls::Grid heading;
    controls::TextBlock title = Label(model.title, 26);
    title.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
    title.Margin(xaml::ThicknessHelper::FromLengths(0, 0, 40, 0));
    heading.Children().Append(title);
    controls::Button close = GlyphButton(kCloseGlyph, Text("album.close"), actions.close);
    close.HorizontalAlignment(xaml::HorizontalAlignment::Right);
    close.VerticalAlignment(xaml::VerticalAlignment::Top);
    heading.Children().Append(close);
    side.Children().Append(heading);
    side.Children().Append(Label(model.subtitle, 15, kDim));

    controls::StackPanel buttons;
    buttons.Orientation(controls::Orientation::Horizontal);
    buttons.Spacing(8);
    buttons.Margin(xaml::ThicknessHelper::FromLengths(0, 14, 0, 14));
    if (!model.tracks.empty()) {
        buttons.Children().Append(HeaderButton(kPlayGlyph, Text("album.play"), [actions] {
            if (actions.play) actions.play(-1, false);
        }));
        buttons.Children().Append(HeaderButton(kShuffleGlyph, Text("album.shuffle"), [actions] {
            if (actions.play) actions.play(-1, true);
        }));
    }
    buttons.Children().Append(HeaderButton(kOpenGlyph, Text("album.open"), actions.open));
    side.Children().Append(buttons);

    const double listWidth = width - 2 * kPadding - kArtwork - kGutter;
    if (!model.tracks.empty()) side.Children().Append(TrackList(model, actions, text, listWidth));
    controls::TextBlock footer = Label(model.footer, 13, kDim);
    footer.TextWrapping(xaml::TextWrapping::Wrap);
    footer.Margin(xaml::ThicknessHelper::FromLengths(model.tracks.empty() ? 0 : 4, 12, 0, 0));
    side.Children().Append(footer);
    layout.Children().Append(side);

    controls::Border panel;
    panel.Width(width);
    panel.Padding(xaml::ThicknessHelper::FromUniformLength(kPadding));
    panel.CornerRadius(xaml::CornerRadiusHelper::FromUniformRadius(10));
    if (model.colors) {
        panel.Background(Solid(model.colors->background));
        // Text and glyphs take their colour from the nearest presenter up the tree; buttons, from the keys painted.
        controls::ContentPresenter painted;
        painted.Foreground(text);
        painted.Content(layout);
        panel.Child(painted);
        PaintButtons(panel.Resources(), text);
    } else {
        panel.Background(Tint(text, kPanelAlpha));
        panel.Child(layout);
    }
    xaml::Automation::AutomationProperties::SetName(panel, winrt::hstring(model.title));
    return panel;
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
