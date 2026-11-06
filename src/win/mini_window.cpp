// The mini player's window, written against the C++/WinRT projection of Windows App SDK 1.8. It lives on Apple Music's
// interface thread: WinUI documents secondary windows on the thread of the app's own, and Apple's resources belong to
// that thread. Everything XAML is created, read and released there (docs/development/xaml-lessons.md).

#if defined(AMPIFY_WITH_XAML)

#include "mini_window.hpp"

// The WinRT headers come before windows.h, as in apple_player.cpp (the GetClassName macro).
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.UI.h>
#include <winrt/Microsoft.UI.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

#include "../core/i18n.hpp"
#include "../core/mini_player.hpp"
#include "../core/settings.hpp"
#include "ui_thread.hpp"
#include "xaml_host.hpp"

namespace ampify::win::miniwin {

namespace {

namespace core = ampify::mini;
namespace controls = winrt::Microsoft::UI::Xaml::Controls;
namespace dispatching = winrt::Microsoft::UI::Dispatching;
namespace media = winrt::Microsoft::UI::Xaml::Media;
namespace windowing = winrt::Microsoft::UI::Windowing;
namespace xaml = winrt::Microsoft::UI::Xaml;

constexpr wchar_t kIconFont[] = L"Segoe MDL2 Assets";
constexpr wchar_t kGlyphPrevious[] = L"";
constexpr wchar_t kGlyphNext[] = L"";
constexpr wchar_t kGlyphPlay[] = L"";
constexpr wchar_t kGlyphPause[] = L"";
constexpr wchar_t kGlyphBack[] = L"";  // Segoe MDL2 "BackToWindow"
constexpr double kIconSize = 16;
constexpr double kCornerRadius = 4;  // small corners only: never a pill
constexpr auto kTick = std::chrono::milliseconds(250);
constexpr auto kSaveGap = std::chrono::milliseconds(500);

// The XAML objects of the window. Touched on the interface thread only.
struct Parts {
    xaml::Window window{nullptr};
    controls::Canvas root{nullptr};
    controls::TextBlock title{nullptr};
    controls::TextBlock artist{nullptr};
    controls::Border coverFrame{nullptr};
    controls::Image cover{nullptr};
    controls::ProgressBar progress{nullptr};
    controls::Button previous{nullptr};
    controls::Button playPause{nullptr};
    controls::Button next{nullptr};
    controls::Button back{nullptr};
    controls::FontIcon playIcon{nullptr};
    dispatching::DispatcherQueueTimer timer{nullptr};
    bool alive = false;  // a window exists and is not closing
    bool followsMain = false;  // closes with Apple Music's main window
    bool shown = false;  // the texts and the state of the buttons match `info`
    core::Info info;
    bool coloured = false;
    core::Colours colours;
    std::string coverTrack;  // the track whose cover is shown
    int coverSlot = 0;       // two file names alternate, so the file on screen is never rewritten
    std::chrono::steady_clock::time_point lastSave{};
};

struct State {
    Journal* journal = nullptr;
    std::filesystem::path kit;
    std::string placement;  // mini.position, read at Install and written when the window moves
    Parts parts;
};

// Never destroyed: the kernel's threads may reach it while the process exits.
State& GetState() {
    static State* state = new State();
    return *state;
}

void Log(const std::string& line) {
    if (GetState().journal) GetState().journal->Write("Mini player: " + line);
}

// "#AARRGGBB" from the core, as a colour. The core only gives valid colours.
winrt::Windows::UI::Color Colour(const std::string& hex) {
    auto channel = [&hex](size_t at) { return static_cast<std::uint8_t>(std::stoul(hex.substr(at, 2), nullptr, 16)); };
    return winrt::Windows::UI::Color{channel(1), channel(3), channel(5), channel(7)};
}

media::SolidColorBrush Brush(const std::string& hex) {
    return media::SolidColorBrush(Colour(hex));
}

void Place(const xaml::UIElement& element, const core::Rect& rect) {
    controls::Canvas::SetLeft(element, rect.x);
    controls::Canvas::SetTop(element, rect.y);
}

template <typename Element>
void Size(const Element& element, const core::Rect& rect) {
    element.Width(rect.width);
    element.Height(rect.height);
}

controls::Button GlyphButton(const wchar_t* glyph, const std::string& tip) {
    controls::FontIcon icon;
    icon.FontFamily(media::FontFamily(kIconFont));
    icon.FontSize(kIconSize);
    icon.Glyph(glyph);
    controls::Button button;
    button.Content(icon);
    button.Background(media::SolidColorBrush(winrt::Windows::UI::Color{0, 0, 0, 0}));
    button.BorderThickness(xaml::Thickness{0, 0, 0, 0});
    button.Padding(xaml::Thickness{0, 0, 0, 0});
    button.CornerRadius(xaml::CornerRadius{kCornerRadius, kCornerRadius, kCornerRadius, kCornerRadius});
    controls::ToolTipService::SetToolTip(button, winrt::box_value(winrt::to_hstring(tip)));
    return button;
}

controls::TextBlock Line(double size) {
    controls::TextBlock text;
    text.FontSize(size);
    text.TextWrapping(xaml::TextWrapping::NoWrap);
    text.TextTrimming(xaml::TextTrimming::CharacterEllipsis);
    return text;
}

// The controls, placed by LayoutParts. Their colours come from the core and are applied by ApplyColours.
void BuildContent(Parts& parts) {
    parts.root = controls::Canvas();
    parts.title = Line(16);
    parts.artist = Line(12);
    parts.coverFrame = controls::Border();
    parts.coverFrame.CornerRadius(xaml::CornerRadius{kCornerRadius, kCornerRadius, kCornerRadius, kCornerRadius});
    parts.cover = controls::Image();
    parts.cover.Stretch(media::Stretch::UniformToFill);
    parts.coverFrame.Child(parts.cover);
    parts.progress = controls::ProgressBar();
    parts.progress.Maximum(100);
    parts.previous = GlyphButton(kGlyphPrevious, i18n::Text("mini.previous"));
    parts.playIcon = controls::FontIcon();
    parts.playIcon.FontFamily(media::FontFamily(kIconFont));
    parts.playIcon.FontSize(kIconSize);
    parts.playIcon.Glyph(kGlyphPlay);
    parts.playPause = controls::Button();
    parts.playPause.Content(parts.playIcon);
    parts.playPause.Background(media::SolidColorBrush(winrt::Windows::UI::Color{0, 0, 0, 0}));
    parts.playPause.BorderThickness(xaml::Thickness{0, 0, 0, 0});
    parts.playPause.Padding(xaml::Thickness{0, 0, 0, 0});
    parts.playPause.CornerRadius(xaml::CornerRadius{kCornerRadius, kCornerRadius, kCornerRadius, kCornerRadius});
    controls::ToolTipService::SetToolTip(parts.playPause, winrt::box_value(winrt::to_hstring(i18n::Text("mini.playPause"))));
    parts.next = GlyphButton(kGlyphNext, i18n::Text("mini.next"));
    parts.back = GlyphButton(kGlyphBack, i18n::Text("mini.back"));

    for (const xaml::UIElement& element : {xaml::UIElement(parts.title), xaml::UIElement(parts.artist),
                                           xaml::UIElement(parts.coverFrame), xaml::UIElement(parts.progress),
                                           xaml::UIElement(parts.previous), xaml::UIElement(parts.playPause),
                                           xaml::UIElement(parts.next), xaml::UIElement(parts.back)}) {
        parts.root.Children().Append(element);
    }
}

// Places each part for a window of `width` x `height` device-independent pixels.
void LayoutParts(Parts& parts, double width, double height) {
    const core::Layout layout = core::LayoutFor(static_cast<int>(width), static_cast<int>(height));
    Place(parts.coverFrame, layout.artwork);
    Size(parts.coverFrame, layout.artwork);
    parts.cover.Width(layout.artwork.width);
    parts.cover.Height(layout.artwork.height);
    parts.title.FontSize(layout.titleSize);
    parts.artist.FontSize(layout.artistSize);
    Place(parts.title, layout.title);
    Size(parts.title, layout.title);
    Place(parts.artist, layout.artist);
    Size(parts.artist, layout.artist);
    Place(parts.progress, layout.progress);
    Size(parts.progress, layout.progress);
    Place(parts.previous, layout.previous);
    Size(parts.previous, layout.previous);
    Place(parts.playPause, layout.playPause);
    Size(parts.playPause, layout.playPause);
    Place(parts.next, layout.next);
    Size(parts.next, layout.next);
    Place(parts.back, layout.back);
    Size(parts.back, layout.back);
}

// The system draws the title bar and its close button: they follow the theme through the title bar's colours.
void ApplyCaption(Parts& parts, const core::Colours& colours) {
    using ColourRef = winrt::Windows::Foundation::IReference<winrt::Windows::UI::Color>;
    if (!windowing::AppWindowTitleBar::IsCustomizationSupported()) return;
    const core::Caption caption = core::CaptionFrom(colours);
    const ColourRef background(Colour(caption.background));
    const ColourRef foreground(Colour(caption.foreground));
    const ColourRef inactive(Colour(caption.inactiveForeground));
    windowing::AppWindowTitleBar bar = parts.window.AppWindow().TitleBar();
    bar.BackgroundColor(background);
    bar.InactiveBackgroundColor(background);
    bar.ForegroundColor(foreground);
    bar.InactiveForegroundColor(inactive);
    bar.ButtonBackgroundColor(background);
    bar.ButtonInactiveBackgroundColor(background);
    bar.ButtonForegroundColor(foreground);
    bar.ButtonInactiveForegroundColor(inactive);
    bar.ButtonHoverBackgroundColor(ColourRef(Colour(caption.hoverBackground)));
    bar.ButtonHoverForegroundColor(foreground);
    bar.ButtonPressedBackgroundColor(ColourRef(Colour(caption.pressedBackground)));
    bar.ButtonPressedForegroundColor(foreground);
}

void ApplyColours(Parts& parts, const core::Colours& colours) {
    ApplyCaption(parts, colours);
    parts.root.Background(Brush(colours.background));
    parts.title.Foreground(Brush(colours.foreground));
    parts.artist.Foreground(Brush(colours.muted));
    parts.coverFrame.Background(Brush(colours.muted));
    parts.progress.Foreground(Brush(colours.accent));
    parts.progress.Background(Brush(colours.muted));
    for (controls::Button button : {parts.previous, parts.playPause, parts.next, parts.back}) {
        button.Foreground(Brush(colours.foreground));
    }
}

// The texts and the play state. The artist line carries the time, "1:05 / 3:42".
void ShowInfo(Parts& parts, const core::Info& info) {
    if (parts.shown && parts.info == info) return;
    if (!parts.shown || parts.info.title != info.title || parts.info.hasTrack != info.hasTrack) {
        std::string title = info.hasTrack ? info.title : i18n::Text("mini.nothing");
        parts.title.Text(winrt::to_hstring(title));
    }
    std::string artist = info.hasTrack ? info.artist : std::string();
    if (!info.elapsed.empty() && info.hasTrack) {
        artist += artist.empty() ? "" : "   ";
        artist += info.elapsed + (info.total.empty() ? "" : " / " + info.total);
    }
    parts.artist.Text(winrt::to_hstring(artist));
    parts.progress.Value(info.progress * 100);
    if (!parts.shown || parts.info.playing != info.playing) {
        parts.playIcon.Glyph(info.playing ? kGlyphPause : kGlyphPlay);
    }
    parts.info = info;
    parts.shown = true;
}

// The cover of the track on screen, written to a file (the image control reads files), two names alternating. The
// image cache is skipped: a name comes back every other song, and the cache would show the older song's cover.
void ShowCover(Parts& parts, const core::Info& info) {
    std::optional<core::Artwork> artwork = core::CurrentArtwork();
    if (!info.hasTrack || !artwork || artwork->trackId != info.trackId) return;  // keep the last cover until the new one
    if (artwork->trackId == parts.coverTrack) return;
    const std::string extension = artwork->contentType == "image/png" ? "png" : "jpg";
    parts.coverSlot ^= 1;
    std::filesystem::path folder = GetState().kit / "cache";
    std::filesystem::path file = folder / ("mini-cover-" + std::to_string(parts.coverSlot) + "." + extension);
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) return;
    out.write(artwork->bytes.data(), static_cast<std::streamsize>(artwork->bytes.size()));
    out.close();
    std::wstring uri = L"file:///" + file.generic_wstring();
    for (size_t at = uri.find(L' '); at != std::wstring::npos; at = uri.find(L' ', at + 3)) uri.replace(at, 1, L"%20");
    media::Imaging::BitmapImage image;
    image.CreateOptions(media::Imaging::BitmapCreateOptions::IgnoreImageCache);
    image.UriSource(winrt::Windows::Foundation::Uri(uri));
    parts.cover.Source(image);
    parts.coverTrack = artwork->trackId;
}

void Refresh(Parts& parts) {
    if (!parts.alive) return;
    const core::Info info = core::CurrentInfo();
    const core::Colours colours = core::CurrentColours();
    if (!parts.coloured || !(parts.colours == colours)) {
        ApplyColours(parts, colours);
        parts.colours = colours;
        parts.coloured = true;
    }
    ShowInfo(parts, info);
    ShowCover(parts, info);
}

// Writes the window's place to mini.position, at most every half second while it is dragged.
void SavePlacement(Parts& parts, bool force) {
    if (!parts.alive || !parts.window) return;
    const auto now = std::chrono::steady_clock::now();
    if (!force && now - parts.lastSave < kSaveGap) return;
    parts.lastSave = now;
    windowing::AppWindow app = parts.window.AppWindow();
    const auto position = app.Position();
    const auto size = app.ClientSize();
    const core::Placement placement{position.X, position.Y, size.Width, size.Height};
    const std::string text = core::FormatPlacement(placement);
    if (text == GetState().placement) return;
    std::string error;
    if (!Settings::UpdateFile((GetState().kit / "settings.txt").string(), core::kPlacementKey, text, error)) {
        Log("the place could not be saved: " + error);
        return;
    }
    GetState().placement = text;
}

// Brings Apple Music's window back in front.
void ShowMainWindow() {
    xaml::Window main = AppWindow(L"MainWindow");
    if (!main) return;
    HWND hwnd = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(main.AppWindow().Id().Value));
    ShowWindow(hwnd, SW_RESTORE);
    SetForegroundWindow(hwnd);
}

// The transport buttons send their command to the kernel's thread, which owns the player. Back runs after the click.
void WireButtons(Parts& parts) {
    parts.previous.Click([](auto&&, auto&&) { core::RequestCommand(PlayerCommand::Previous); });
    parts.next.Click([](auto&&, auto&&) { core::RequestCommand(PlayerCommand::Next); });
    parts.playPause.Click([](auto&&, auto&&) {
        core::RequestCommand(GetState().parts.info.playing ? PlayerCommand::Pause : PlayerCommand::Play);
    });
    parts.back.Click([](auto&&, auto&&) { PostToUiThread([] { ShowMainWindow(); }); });
}

// The screen's scale for the window. Not XamlRoot's: there is none before Activate, and 1 made the window too small.
double ScaleOf(const windowing::AppWindow& app) {
    const UINT dpi = GetDpiForWindow(reinterpret_cast<HWND>(static_cast<std::uintptr_t>(app.Id().Value)));
    return dpi ? dpi / static_cast<double>(USER_DEFAULT_SCREEN_DPI) : 1.0;
}

// The title bar and borders around the content, which differ with the screen's scale.
core::Frame FrameOf(const windowing::AppWindow& app) {
    const auto outer = app.Size();
    const auto client = app.ClientSize();
    return core::Frame{std::max(0, outer.Width - client.Width), std::max(0, outer.Height - client.Height)};
}

// The window's place: the saved one, kept on a screen, or the default corner of the work area of the screen it is on.
// The size is the content's (the title bar comes on top), so the layout gets the room it was made for at every scale.
std::string PlaceWindow(Parts& parts) {
    windowing::AppWindow app = parts.window.AppWindow();
    const std::optional<core::Placement> saved = core::ParsePlacement(GetState().placement);
    if (saved) app.Move(winrt::Windows::Graphics::PointInt32{saved->x, saved->y});  // scale and frame of that screen
    const double scale = ScaleOf(app);
    const core::Frame frame = FrameOf(app);
    const auto work = windowing::DisplayArea::GetFromWindowId(app.Id(), windowing::DisplayAreaFallback::Nearest).WorkArea();
    const core::Rect area{work.X, work.Y, work.Width, work.Height};
    core::Placement placement = saved.value_or(core::DefaultPlacement(area, scale, frame));
    placement = core::FitInto(placement, area, scale, frame);
    app.Move(winrt::Windows::Graphics::PointInt32{placement.x, placement.y});
    app.ResizeClient(winrt::Windows::Graphics::SizeInt32{placement.width, placement.height});
    LayoutParts(parts, placement.width / scale, placement.height / scale);
    return core::FormatPlacement(placement);
}

void OnClosed(Parts& parts) {
    SavePlacement(parts, true);
    parts.alive = false;
    if (parts.timer) parts.timer.Stop();
    parts.timer = nullptr;
    core::SetWindowOpen(false);
    Log("window closed");
    // The window is released on the next pass of the queue: it is still inside its own Closed event here.
    xaml::Window keep = parts.window;
    PostToUiThread([keep]() mutable { keep = nullptr; });
}

// A second window keeps a WinUI app running: the mini player closes with Apple Music's main window. Subscribed once.
void FollowMainWindow(Parts& parts) {
    if (parts.followsMain) return;
    xaml::Window main = AppWindow(L"MainWindow");
    if (!main) return;
    main.Closed([](auto&&, auto&&) noexcept {
        try {
            Parts& live = GetState().parts;
            if (live.alive && live.window) live.window.Close();
        } catch (const winrt::hresult_error& error) {
            Log("the window could not close with Apple Music: " + HresultText(error.code()));
        }
    });
    parts.followsMain = true;
}

// Creates the window on the interface thread, with the controls, the placement, the presenter and the events. Gives
// the placement, for the one log line of the opening.
std::string MakeWindow(Parts& parts) {
    BuildContent(parts);
    WireButtons(parts);
    parts.window = xaml::Window();
    parts.window.Title(L"Ampify");
    parts.window.Content(parts.root);

    windowing::AppWindow app = parts.window.AppWindow();
    windowing::OverlappedPresenter presenter = windowing::OverlappedPresenter::Create();
    presenter.IsAlwaysOnTop(true);
    presenter.IsMaximizable(false);
    presenter.IsMinimizable(false);
    app.SetPresenter(presenter);

    parts.root.SizeChanged([](auto&&, xaml::SizeChangedEventArgs const& args) {
        LayoutParts(GetState().parts, args.NewSize().Width, args.NewSize().Height);
    });
    app.Changed([](windowing::AppWindow const&, windowing::AppWindowChangedEventArgs const& args) {
        if (args.DidPositionChange() || args.DidSizeChange()) SavePlacement(GetState().parts, false);
    });
    parts.window.Closed([](auto&&, auto&&) { OnClosed(GetState().parts); });

    parts.timer = parts.window.DispatcherQueue().CreateTimer();
    parts.timer.Interval(kTick);
    parts.timer.Tick([](auto&&, auto&&) { Refresh(GetState().parts); });
    std::string placement = PlaceWindow(parts);
    FollowMainWindow(parts);
    parts.alive = true;
    parts.shown = false;
    parts.coloured = false;
    parts.coverTrack.clear();
    return placement;
}

// Shows the window, made first when it is not open, and starts the refresh. An open window is only brought forward,
// without a log line: the log has one line per opening and one per closing.
void OpenWindow(Parts& parts, bool open) {
    try {
        std::string placement;
        if (!open) placement = MakeWindow(parts);
        Refresh(parts);  // before Activate: the title bar shows in the theme's colours from the first frame
        parts.window.Activate();
        parts.timer.Start();
        core::SetWindowOpen(true);
        if (!open) Log("window opened at " + placement);
    } catch (const winrt::hresult_error& error) {
        Log("the window could not be opened: " + HresultText(error.code()));
        parts.alive = false;
        if (parts.timer) parts.timer.Stop();
        if (!open && parts.window) parts.window.Close();  // a half-made window never shows: let it go
    }
}

// On the interface thread: opens, closes or toggles the window.
void Perform(core::WindowAction action) {
    Parts& parts = GetState().parts;
    const bool open = parts.alive && parts.window;
    if (action == core::WindowAction::Toggle) action = open ? core::WindowAction::Close : core::WindowAction::Open;
    if (action == core::WindowAction::Open) return OpenWindow(parts, open);
    if (!open) return;
    try {
        parts.window.Close();
    } catch (const winrt::hresult_error& error) {
        Log("the window could not be closed: " + HresultText(error.code()));
    }
}

// The shortcut and the extensions come here, on any thread.
void Dispatch(core::WindowAction action) {
    if (!PostToUiThread([action] { Perform(action); })) Log("Apple Music's interface thread is not reachable yet");
}

}  // namespace

void Install(Journal& journal, std::filesystem::path kit, std::string savedPlacement) {
    State& state = GetState();
    state.journal = &journal;
    state.kit = std::move(kit);
    state.placement = std::move(savedPlacement);
    core::InstallWindow([](core::WindowAction action) { Dispatch(action); });
}

}  // namespace ampify::win::miniwin

#endif
