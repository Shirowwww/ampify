// Written against the C++/WinRT projection of Windows App SDK 1.8.

#if defined(AMPIFY_WITH_XAML)

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
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

#include "../core/i18n.hpp"
#include "../core/json.hpp"
#include "../core/now_playing.hpp"
#include "../core/player_queue.hpp"
#include "../core/ui_render/probe.hpp"
#include "../core/ui_render/style.hpp"
#include "../core/ui_vocab.hpp"
#include "../core/ui_render/svg_icon.hpp"
#include "artwork_fade.hpp"
#include "player_controls.hpp"
#include "xaml_host.hpp"

namespace ampify::win::playing_views {

namespace automation = xaml::Automation;
namespace primitives = xaml::Controls::Primitives;
using winrt::Windows::Foundation::IInspectable;

constexpr double kArtworkPixels = 1200;  // Apple's library covers are 600 to 1200 px wide, its catalog ones larger
constexpr int kArtworkSearchLimit = 20000;  // elements looked at in the window, a few thousand on the busiest pages

// Apple Music's image of the song playing: the first Image#StaticArtwork under NowPlayingControl#LCD, the one its play
// bar shows (the same that `background: artwork` uses, src/win/xaml_backend.cpp). Null when the bar has none.
controls::Image FindPlayingArtwork(const xaml::XamlRoot& root) {
    if (!root || !root.Content()) return nullptr;
    std::vector<std::pair<xaml::DependencyObject, bool>> pending{{root.Content(), false}};
    for (int looked = 0; !pending.empty() && looked < kArtworkSearchLimit; looked++) {
        auto [object, inDisplay] = pending.back();
        pending.pop_back();
        auto element = object.try_as<xaml::FrameworkElement>();
        std::wstring_view name = element ? std::wstring_view(element.Name()) : std::wstring_view();
        if (element && name == L"LCD" && object.try_as<controls::Control>()) inDisplay = true;
        if (inDisplay && name == L"StaticArtwork") {
            if (auto image = object.try_as<controls::Image>()) return image;
        }
        int count = media::VisualTreeHelper::GetChildrenCount(object);
        for (int i = count - 1; i >= 0; i--) pending.emplace_back(media::VisualTreeHelper::GetChild(object, i), inDisplay);
    }
    return nullptr;
}

// Apple empties its image between two songs: the previous cover stays until the next one, so the image keeps its
// size and a scrolled column does not jump.
void ShowArtwork(ArtworkFollow& follow, const media::ImageSource& source) {
    if (!source) return;
    media::ImageSource previous = follow.brush.ImageSource();
    if (previous != source) FadeFromPrevious(follow.fade, previous, follow.corner);
    follow.brush.ImageSource(source);
    follow.sizer.Source(source);
}

winrt::Windows::Foundation::Uri AddressOf(const media::ImageSource& source) {
    auto bitmap = source ? source.try_as<media::Imaging::BitmapImage>() : nullptr;
    return bitmap ? bitmap.UriSource() : nullptr;
}

// The image already shows this file decoded large. Apple sets its play-bar image again with the same file (two songs of
// one album, a redraw of its bar); a new bitmap of an address WinUI holds decoded may never raise ImageOpened, which
// left the thumbnail upscaled in place of the cover.
bool ShowsLarge(const ArtworkFollow& follow, const winrt::Windows::Foundation::Uri& address) {
    if (!address) return false;
    auto shown = follow.brush.ImageSource().try_as<media::Imaging::BitmapImage>();
    if (!shown || shown.DecodePixelWidth() != static_cast<int>(kArtworkPixels)) return false;
    winrt::Windows::Foundation::Uri current = shown.UriSource();
    return current && current.Equals(address);
}

// The large cover's handlers, removed once fired or replaced: the bitmap would otherwise hold the follower, which
// holds the bitmap.
void DropLoading(ArtworkFollow& follow) {
    if (!follow.loading) return;
    follow.loading.ImageOpened(follow.openedToken);
    follow.loading.ImageFailed(follow.failedToken);
    follow.loading = nullptr;
    if (follow.pinned) follow.sizer.ClearValue(xaml::FrameworkElement::HeightProperty());
    follow.pinned = false;
}

// Apple's image at once, then the same cover decoded at its full size as soon as it has loaded: Apple's own bitmaps
// are decoded at the size of where Apple shows them (a thumbnail in the play bar), though the file behind is large
// (600 to 1200 px for the library). No empty frame between two songs.
void ShowCover(const std::shared_ptr<ArtworkFollow>& follow, const media::ImageSource& shown,
               const media::ImageSource& broker) {
    DropLoading(*follow);
    winrt::Windows::Foundation::Uri address = AddressOf(broker);
    if (!address) address = AddressOf(shown);
    winrt::Windows::Foundation::Uri shownAddress = AddressOf(shown);
    // Apple's image of another file means a new song even while the loader still gives the previous cover.
    if (ShowsLarge(*follow, address) && (!shownAddress || shownAddress.Equals(address))) return;
    ShowArtwork(*follow, shown);
    if (!address) return;
    media::Imaging::BitmapImage large;
    large.DecodePixelWidth(static_cast<int>(kArtworkPixels));  // a 3000 px catalog cover is not decoded whole
    follow->loading = large;
    // Only the latest bitmap is shown: a slow cover of the previous song that opens late is ignored.
    follow->openedToken = large.ImageOpened([follow](IInspectable const& sender, xaml::RoutedEventArgs const&) noexcept {
        try {
            auto bitmap = sender.as<media::Imaging::BitmapImage>();
            if (follow->loading != bitmap) return;
            if (!follow->off) follow->brush.ImageSource(bitmap);
            DropLoading(*follow);
        } catch (...) {
        }
    });
    follow->failedToken = large.ImageFailed([follow](IInspectable const& sender, xaml::ExceptionRoutedEventArgs const&) noexcept {
        try {
            if (follow->loading != sender.as<media::Imaging::BitmapImage>()) return;
            follow->sizer.Source(follow->brush.ImageSource());
            DropLoading(*follow);
        } catch (...) {
        }
    });
    large.UriSource(address);
    // WinUI decodes a bitmap only once something in the window shows it: the invisible sizer loads it, held at its
    // height meanwhile so the column does not jump, and the visible brush takes it once it has opened.
    if (double held = follow->sizer.ActualHeight(); held > 0) {
        follow->sizer.Height(held);
        follow->pinned = true;
    }
    follow->sizer.Source(large);
}

// Apple's image of the song playing at once, then its cover large (ShowCover).
void RefreshArtwork(const std::shared_ptr<ArtworkFollow>& follow) {
    if (!follow->apple) return;
    ShowCover(follow, follow->apple.Source(), PlayingArtworkSource(kArtworkPixels));
}

// A queue entry's cover (AppleArtworkSource): Apple's loader draws it, then it is decoded large. It does not follow
// anything, as the entry keeps its cover; the image gets a new src when the view shows another entry.
std::shared_ptr<ArtworkFollow> ShowQueueArtwork(const media::ImageBrush& brush, const controls::Image& sizer,
                                                std::uint64_t itemId) {
    auto follow = std::make_shared<ArtworkFollow>();
    follow->brush = brush;
    follow->sizer = sizer;
    media::ImageSource broker = QueueArtworkImage(itemId, kArtworkPixels);
    ShowCover(follow, broker, broker);
    return follow;
}

constexpr auto kArtworkPoll = std::chrono::milliseconds(500);

// The cover of the song playing from Apple's loader, once per song: the key of the now-playing feed tells a new one.
void PollArtwork(const std::shared_ptr<ArtworkFollow>& follow) {
    std::string key = CurrentNowPlaying().trackKey;
    if (follow->off || follow->apple || key == follow->polledKey) return;
    media::ImageSource broker = PlayingArtworkSource(kArtworkPixels);
    if (!broker) return;  // Apple has no cover yet: the next tick asks again
    follow->polledKey = key;
    ShowCover(follow, broker, broker);
}

// Apple's play-bar image was not found in the window (a view in a popup over a page too large to walk, or a layout
// without it): the image would have stayed blank. It follows the song through Apple's loader instead.
void PollArtworkWithoutApple(const std::shared_ptr<ArtworkFollow>& follow) {
    PollArtwork(follow);
    if (!follow->poll) {
        auto queue = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        if (!queue) return;
        follow->poll = queue.CreateTimer();
        follow->poll.Interval(kArtworkPoll);
        follow->poll.IsRepeating(true);
        std::weak_ptr<ArtworkFollow> weak = follow;
        follow->poll.Tick([weak](auto const&, auto const&) noexcept {
            try {
                if (std::shared_ptr<ArtworkFollow> alive = weak.lock()) PollArtwork(alive);
            } catch (...) {
            }
        });
    }
    if (!follow->poll.IsRunning()) follow->poll.Start();
}

bool StartArtwork(const std::shared_ptr<ArtworkFollow>& follow, const xaml::XamlRoot& root) {
    if (follow->off || follow->apple) return true;
    controls::Image apple = FindPlayingArtwork(root);
    if (!apple) {
        PollArtworkWithoutApple(follow);
        return false;
    }
    if (follow->poll) follow->poll.Stop();
    follow->apple = apple;
    follow->token = apple.RegisterPropertyChangedCallback(
        controls::Image::SourceProperty(), [follow](xaml::DependencyObject const&, xaml::DependencyProperty const&) noexcept {
            try {
                RefreshArtwork(follow);
            } catch (...) {
            }
        });
    RefreshArtwork(follow);
    return true;
}

void StopArtwork(ArtworkFollow& follow) {
    DropLoading(follow);
    if (follow.poll) follow.poll.Stop();
    follow.polledKey.clear();  // shown again when the image comes back to the window
    if (!follow.apple) return;
    follow.apple.UnregisterPropertyChangedCallback(controls::Image::SourceProperty(), follow.token);
    follow.apple = nullptr;
}

// The image shows something else now: what it followed stops, and its fade layer leaves the frame.
void DropFollow(const controls::Border& frame, ArtworkFollow& follow) {
    follow.off = true;
    StopArtwork(follow);
    if (follow.loadedToken.value) frame.Loaded(follow.loadedToken);
    if (follow.unloadedToken.value) frame.Unloaded(follow.unloadedToken);
    if (follow.fade.story) follow.fade.story.Stop();
    auto layers = frame.Child().try_as<controls::Grid>();
    std::uint32_t index = 0;
    if (follow.fade.layer && layers && layers.Children().IndexOf(follow.fade.layer, index)) layers.Children().RemoveAt(index);
}

// Ties an image of a view to Apple's own image of the song playing, which changes with every song, and shows that
// cover loaded large. Followed while the view is in the window, where Apple's image can be found.
std::shared_ptr<ArtworkFollow> FollowPlayingArtwork(const controls::Border& frame, const media::ImageBrush& brush,
                                                    const controls::Image& sizer) {
    auto follow = std::make_shared<ArtworkFollow>();
    follow->brush = brush;
    follow->sizer = sizer;
    follow->fade = MakeArtworkFade(brush.Stretch());
    follow->corner = frame.CornerRadius();
    if (auto layers = frame.Child().try_as<controls::Grid>()) layers.Children().Append(follow->fade.layer);
    if (frame.XamlRoot()) StartArtwork(follow, frame.XamlRoot());
    follow->loadedToken = frame.Loaded([follow](IInspectable const& sender, xaml::RoutedEventArgs const&) noexcept {
        try {
            StartArtwork(follow, sender.as<controls::Border>().XamlRoot());
        } catch (...) {
        }
    });
    follow->unloadedToken = frame.Unloaded([follow](IInspectable const&, xaml::RoutedEventArgs const&) noexcept {
        try {
            StopArtwork(*follow);
        } catch (...) {
        }
    });
    return follow;
}

constexpr double kLyricsIdleOpacity = 0.4;  // the lines not being sung
constexpr double kLyricsHoverOpacity = 0.6; // such a line under the pointer
constexpr double kLyricsUnsung = 0.3;       // a word of the current line not reached yet
constexpr double kLyricsIdleScale = 0.92;   // the current line is drawn larger than the others
constexpr int64_t kLyricsLeadMs = 120;      // a word lights up a little ahead, as the eye expects it
constexpr auto kLyricsFrame = std::chrono::milliseconds(33);
constexpr auto kLyricsMotion = std::chrono::milliseconds(350);
constexpr auto kLyricsGlide = std::chrono::milliseconds(250);  // the lines moving, by the song or by the wheel
constexpr auto kLyricsBackFade = std::chrono::milliseconds(150);
constexpr double kLyricsAnchor = 0.3;      // the line being sung stands about a third down the view
constexpr double kLyricsWheelStep = 0.4;   // pixels per unit of wheel turn: a notch (120) moves 48 pixels
constexpr double kLyricsBackSize = 32;
constexpr wchar_t kGlyphUp[] = L"\uE70E";    // ChevronUp, in Segoe Fluent Icons
constexpr wchar_t kGlyphDown[] = L"\uE70D";  // ChevronDown


winrt::Windows::UI::Color WithAlpha(winrt::Windows::UI::Color color, double alpha) {
    color.A = static_cast<uint8_t>(std::lround(std::clamp(alpha, 0.0, 1.0) * color.A));
    return color;
}

void SetBrushAlpha(media::SolidColorBrush& brush, winrt::Windows::UI::Color base, double alpha) {
    winrt::Windows::UI::Color wanted = WithAlpha(base, alpha);
    if (brush.Color() != wanted) brush.Color(wanted);
}

// The lines moved up by `target` pixels, kept within what they hold (their bottom padding lets the last line come up).
void MoveLyrics(LyricsView& view, double target) {
    double room = std::max(0.0, view.lines.ActualHeight() - view.viewport.ActualHeight());
    view.offset = std::clamp(target, 0.0, room);
    view.lines.Translation({0, static_cast<float>(-view.offset), 0});
}

// "Back to the line being sung", shown while the user reads elsewhere; its arrow points to that line.
void ShowLyricsBack(LyricsView& view, bool shown, bool lineAbove = false) {
    view.manual = shown;
    view.back.Opacity(shown ? 1 : 0);
    view.back.IsHitTestVisible(shown);
    view.back.IsTabStop(shown);
    if (shown) view.backIcon.Glyph(lineAbove ? kGlyphUp : kGlyphDown);
}

// The wheel goes to the panel the lyrics stand in while it can still move that way.
bool PanelTakesWheel(const xaml::DependencyObject& from, bool down) {
    for (auto node = media::VisualTreeHelper::GetParent(from); node; node = media::VisualTreeHelper::GetParent(node)) {
        if (auto scroll = node.try_as<controls::ScrollViewer>()) {
            return down ? scroll.VerticalOffset() < scroll.ScrollableHeight() - 0.5 : scroll.VerticalOffset() > 0.5;
        }
    }
    return false;
}

controls::TextBlock LyricLineBlock(LyricsView& view, const LyricLine& line, bool timed) {
    controls::TextBlock block;
    block.TextWrapping(xaml::TextWrapping::Wrap);
    block.FontSize(view.size);
    block.FontWeight(winrt::Windows::UI::Text::FontWeights::Bold());
    xaml::ScalarTransition fade;
    fade.Duration(kLyricsMotion);
    block.OpacityTransition(fade);
    xaml::Vector3Transition grow;
    grow.Duration(kLyricsMotion);
    block.ScaleTransition(grow);
    std::vector<media::SolidColorBrush> brushes;
    for (const KaraokeWord& word : KaraokeWords(line)) {
        winrt::Microsoft::UI::Xaml::Documents::Run run;
        run.Text(winrt::to_hstring(word.piece));
        media::SolidColorBrush brush(view.color);
        run.Foreground(brush);
        block.Inlines().Append(run);
        brushes.push_back(brush);
    }
    view.words.push_back(std::move(brushes));
    if (timed) {
        int64_t start = line.startMs;
        block.Tapped([weak = view.self, start](IInspectable const&, xaml::Input::TappedRoutedEventArgs const&) noexcept {
            try {
                std::shared_ptr<LyricsView> alive = weak.lock();
                if (!alive || !alive->pick) return;
                ShowLyricsBack(*alive, false);  // the view follows the song again from the line picked
                alive->pick(start);
            } catch (...) {
            }
        });
        // A line under the pointer brightens a little, to say it can be clicked; the current one stays as it is.
        block.PointerEntered([](IInspectable const& sender, xaml::Input::PointerRoutedEventArgs const&) noexcept {
            try {
                auto line = sender.as<controls::TextBlock>();
                if (line.Opacity() < 1) line.Opacity(kLyricsHoverOpacity);
            } catch (...) {
            }
        });
        block.PointerExited([](IInspectable const& sender, xaml::Input::PointerRoutedEventArgs const&) noexcept {
            try {
                auto line = sender.as<controls::TextBlock>();
                if (line.Opacity() < 1) line.Opacity(kLyricsIdleOpacity);
            } catch (...) {
            }
        });
        block.Opacity(kLyricsIdleOpacity);
        block.Scale({static_cast<float>(kLyricsIdleScale), static_cast<float>(kLyricsIdleScale), 1});
    }
    return block;
}

void BuildLyricLines(LyricsView& view, const std::shared_ptr<const Lyrics>& lyrics) {
    view.shown = lyrics;
    view.stale = false;
    view.current = -2;
    view.words.clear();
    view.lines.Children().Clear();
    view.placeholder.Visibility(lyrics ? xaml::Visibility::Collapsed : xaml::Visibility::Visible);
    view.placeholder.Foreground(media::SolidColorBrush(WithAlpha(view.color, kLyricsIdleOpacity)));
    if (lyrics) {
        for (const LyricLine& line : lyrics->lines) view.lines.Children().Append(LyricLineBlock(view, line, lyrics->synced));
    }
    ShowLyricsBack(view, false);
    MoveLyrics(view, 0);
}

controls::TextBlock LyricLineAt(const LyricsView& view, int index) {
    if (index < 0 || static_cast<uint32_t>(index) >= view.lines.Children().Size()) return nullptr;
    return view.lines.Children().GetAt(index).try_as<controls::TextBlock>();
}

// Grows from the left edge of its middle, as Apple's lines do.
void SetLineLook(const controls::TextBlock& block, bool current) {
    float scale = current ? 1.0f : static_cast<float>(kLyricsIdleScale);
    block.CenterPoint({0, static_cast<float>(block.ActualHeight() / 2), 0});
    block.Scale({scale, scale, 1});
    block.Opacity(current ? 1.0 : kLyricsIdleOpacity);
}

// The current line about a third down the view. While the user reads elsewhere the lines stay put, until the line
// being sung comes into view by itself: from there the view follows the song again.
// Whether the whole line shows in the view as the lines are moved now; `top` is where it starts among them.
bool LineInView(const LyricsView& view, const controls::TextBlock& block, double& top) {
    top = block.TransformToVisual(view.lines).TransformPoint({0, 0}).Y;
    return top >= view.offset && top + block.ActualHeight() <= view.offset + view.viewport.ActualHeight();
}

void KeepLineInView(LyricsView& view, const controls::TextBlock& block) {
    if (block.ActualHeight() == 0) view.lines.UpdateLayout();  // lines just built: not measured yet, all at 0
    double top = 0;
    bool inView = LineInView(view, block, top);
    double height = view.viewport.ActualHeight();
    if (view.manual) {
        if (!inView) {
            ShowLyricsBack(view, true, top < view.offset);
            return;
        }
        ShowLyricsBack(view, false);
    }
    MoveLyrics(view, top - height * kLyricsAnchor);
}

void PaintLyrics(LyricsView& view) {
    // The window hidden (in the notification area, minimized): 30 frames a second for nobody. The next frame shown
    // catches up with the song at once.
    if (auto root = view.viewport ? view.viewport.XamlRoot() : nullptr; root && !root.IsHostVisible()) return;
    NowPlaying now = CurrentNowPlaying();
    if (view.stale || now.lyrics != view.shown) BuildLyricLines(view, now.lyrics);
    if (!view.shown || !view.shown->synced) return;
    const int64_t tick = static_cast<int64_t>(GetTickCount64());
    KaraokeFrame frame = KaraokeAt(*view.shown, now.PositionAt(tick) + kLyricsLeadMs);
    if (frame.line != view.current) {
        if (controls::TextBlock old = LyricLineAt(view, view.current)) {
            SetLineLook(old, false);
            for (media::SolidColorBrush& brush : view.words[view.current]) SetBrushAlpha(brush, view.color, 1);
        }
        view.current = frame.line;
        if (controls::TextBlock line = LyricLineAt(view, frame.line)) {
            SetLineLook(line, true);
            KeepLineInView(view, line);
        }
    }
    if (frame.line < 0 || static_cast<size_t>(frame.line) >= view.words.size()) return;
    std::vector<media::SolidColorBrush>& brushes = view.words[frame.line];
    for (size_t i = 0; i < brushes.size(); i++) {
        double done = frame.between ? 1.0 : frame.words[i];
        SetBrushAlpha(brushes[i], view.color, kLyricsUnsung + (1 - kLyricsUnsung) * done);
    }
}

void RunLyrics(const std::shared_ptr<LyricsView>& view, bool run) {
    if (!run) {
        if (view->timer) view->timer.Stop();
        return;
    }
    if (!view->timer) {
        view->timer = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
        view->timer.Interval(kLyricsFrame);
        std::weak_ptr<LyricsView> weak = view;  // the timer must not keep the view alive
        view->timer.Tick([weak](auto&&, auto&&) noexcept {
            try {
                if (std::shared_ptr<LyricsView> alive = weak.lock()) PaintLyrics(*alive);
            } catch (...) {
            }
        });
    }
    view->timer.Start();
}

// A small round button in the bottom right corner, hidden until the user reads elsewhere; it fades in and out.
controls::Button MakeLyricsBack(const std::shared_ptr<LyricsView>& view) {
    controls::Button back;
    winrt::hstring label = winrt::to_hstring(i18n::Text("ui.lyricsNow"));
    back.Width(kLyricsBackSize);
    back.Height(kLyricsBackSize);
    back.Padding(xaml::Thickness{0, 0, 0, 0});
    back.CornerRadius(xaml::CornerRadius{kLyricsBackSize / 2, kLyricsBackSize / 2, kLyricsBackSize / 2, kLyricsBackSize / 2});
    back.HorizontalAlignment(xaml::HorizontalAlignment::Right);
    back.VerticalAlignment(xaml::VerticalAlignment::Bottom);
    back.Margin(xaml::Thickness{0, 0, 8, 8});
    back.Background(media::SolidColorBrush(winrt::Windows::UI::Color{0x99, 0, 0, 0}));
    back.BorderThickness(xaml::Thickness{0, 0, 0, 0});
    view->backIcon = controls::FontIcon();
    view->backIcon.FontSize(14);
    view->backIcon.Glyph(kGlyphDown);
    view->backIcon.Foreground(media::SolidColorBrush(view->color));
    back.Content(view->backIcon);
    controls::ToolTipService::SetToolTip(back, winrt::box_value(label));
    automation::AutomationProperties::SetName(back, label);
    xaml::ScalarTransition fade;
    fade.Duration(kLyricsBackFade);
    back.OpacityTransition(fade);
    back.Opacity(0);
    back.IsHitTestVisible(false);
    back.IsTabStop(false);
    std::weak_ptr<LyricsView> weak = view;
    back.Click([weak](IInspectable const&, xaml::RoutedEventArgs const&) noexcept {
        try {
            std::shared_ptr<LyricsView> alive = weak.lock();
            if (!alive) return;
            ShowLyricsBack(*alive, false);
            if (controls::TextBlock sung = LyricLineAt(*alive, alive->current)) KeepLineInView(*alive, sung);
        } catch (...) {
        }
    });
    view->back = back;
    return back;
}

xaml::FrameworkElement MakeLyricsView(const std::shared_ptr<LyricsView>& view) {
    controls::Grid root;
    view->self = view;
    view->lines = controls::StackPanel();
    view->lines.Spacing(view->size * 0.6);
    view->lines.Padding(xaml::Thickness{0, 24, 24, 240});  // the last line can still come up to its place
    xaml::Vector3Transition glide;
    glide.Duration(kLyricsGlide);
    view->lines.TranslationTransition(glide);
    // A Canvas lays the lines out whole, however tall: in a Grid they were cut to its height before they moved.
    controls::Canvas canvas;
    canvas.Children().Append(view->lines);
    view->viewport = controls::Grid();
    view->viewport.Background(media::SolidColorBrush(winrt::Windows::UI::Color{0, 0, 0, 0}));  // takes the wheel
    view->viewport.Children().Append(canvas);
    view->placeholder = controls::TextBlock();
    view->placeholder.TextWrapping(xaml::TextWrapping::Wrap);
    view->placeholder.FontSize(16);
    root.Children().Append(view->viewport);
    root.Children().Append(view->placeholder);
    root.Children().Append(MakeLyricsBack(view));
    std::weak_ptr<LyricsView> weak = view;
    view->viewport.SizeChanged([weak](IInspectable const&, xaml::SizeChangedEventArgs const& args) noexcept {
        try {
            std::shared_ptr<LyricsView> alive = weak.lock();
            if (!alive) return;
            auto size = args.NewSize();
            alive->lines.Width(size.Width);
            media::RectangleGeometry clip;
            clip.Rect({0, 0, size.Width, size.Height});
            alive->viewport.Clip(clip);
        } catch (...) {
        }
    });
    view->viewport.PointerWheelChanged([weak](IInspectable const&, xaml::Input::PointerRoutedEventArgs const& args) noexcept {
        try {
            std::shared_ptr<LyricsView> alive = weak.lock();
            if (!alive || !alive->shown) return;
            int delta = args.GetCurrentPoint(alive->viewport).Properties().MouseWheelDelta();
            if (delta == 0 || PanelTakesWheel(alive->viewport, delta < 0)) return;
            MoveLyrics(*alive, alive->offset - delta * kLyricsWheelStep);
            args.Handled(true);
            if (!alive->shown->synced) return;  // nothing is sung: nothing to come back to
            controls::TextBlock sung = LyricLineAt(*alive, alive->current);
            if (!sung) return;  // no line sung yet: nothing to come back to
            double top = 0;
            bool inView = LineInView(*alive, sung, top);
            ShowLyricsBack(*alive, !inView, top < alive->offset);
        } catch (...) {
        }
    });
    root.Loaded([weak](auto&&, auto&&) noexcept {
        try {
            if (std::shared_ptr<LyricsView> alive = weak.lock()) RunLyrics(alive, true);
        } catch (...) {
        }
    });
    root.Unloaded([weak](auto&&, auto&&) noexcept {
        try {
            if (std::shared_ptr<LyricsView> alive = weak.lock()) RunLyrics(alive, false);
        } catch (...) {
        }
    });
    return root;
}


}  // namespace ampify::win::playing_views

#endif  // AMPIFY_WITH_XAML
