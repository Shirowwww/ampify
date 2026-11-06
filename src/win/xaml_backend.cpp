// Written against the C++/WinRT projection of the Windows App SDK. What WinUI 3 did differently from its documentation
// is in docs/development/xaml-lessons.md.

#if defined(AMPIFY_WITH_XAML)

#include "xaml_backend.hpp"

#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <set>

#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
#include <winrt/Microsoft.UI.Xaml.Navigation.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.Foundation.Numerics.h>

#include "../core/color_extract.hpp"
#include "../core/theme/colour_keys.hpp"
#include "../core/theme/properties.hpp"
#include "../core/theme/selector.hpp"
#include "../core/text_util.hpp"
#include "../core/ui_render/render_tree.hpp"
#include "sidebar_hover.hpp"
#include "xaml_host.hpp"
#include "xaml_backend_detail.hpp"

namespace ampify::win {

using namespace xaml_detail;

bool XamlTreeBackend::Attach() {
    if (*m_closed) return false;
    xaml::Window window = AppWindow(L"MainWindow");
    if (!window) return false;
    m_window = winrt::make_weak(window);
    m_animations.Window(reinterpret_cast<HWND>(static_cast<std::uintptr_t>(window.AppWindow().Id().Value)));
    // Walking or writing the tree while Apple Music closes crashed it in Microsoft.UI.Xaml.dll (bench, 30/10).
    std::shared_ptr<bool> closed = m_closed;
    window.Closed([closed](IInspectable const&, xaml::WindowEventArgs const&) noexcept { *closed = true; });
    std::weak_ptr<bool> alive = m_alive;
    window.SizeChanged([this, alive](IInspectable const&, xaml::WindowSizeChangedEventArgs const&) noexcept {
        if (!alive.expired()) OnSizeChanged();
    });
    return true;
}

std::optional<theme::ViewSize> XamlTreeBackend::WindowSize() {
    try {
        xaml::Window window = m_window.get();
        if (!window) return std::nullopt;
        winrt::Windows::Foundation::Rect bounds = window.Bounds();
        return theme::ViewSize{bounds.Width, bounds.Height, SidebarKeptFolded()};
    } catch (...) {
        return std::nullopt;
    }
}

std::vector<bool> XamlTreeBackend::MediaAnswers() {
    std::vector<bool> answers;
    std::optional<theme::ViewSize> size = WindowSize();
    for (const theme::MediaQuery& query : m_mediaQueries) answers.push_back(size && query.Matches(size->width, size->height, size->sidebarFolded));
    return answers;
}

void XamlTreeBackend::WatchSize(const std::vector<theme::MediaQuery>& queries) {
    if (queries == m_mediaQueries) return;
    m_mediaQueries = queries;
    m_mediaAnswers = MediaAnswers();
}

// Resizing fires many times a second: only a change of answer costs a pass.
void XamlTreeBackend::OnSizeChanged() noexcept {
    try {
        if (m_mediaQueries.empty()) return;
        std::vector<bool> answers = MediaAnswers();
        if (answers == m_mediaAnswers) return;
        m_mediaAnswers = std::move(answers);
        SchedulePass(0);
    } catch (...) {
    }
}

bool XamlTreeBackend::SetAppMode(std::optional<bool> dark) {
    if (*m_closed) return false;
    bool refresh = std::exchange(m_resourcesDirty, false);
    xaml::FrameworkElement root = m_modeRoot.get();
    if (!root) {
        if (xaml::Window window = m_window.get()) root = window.Content().try_as<xaml::FrameworkElement>();
        if (!root) return false;
        if (dark) {
            m_modeRoot = winrt::make_weak(root);
            m_modeOriginal = root.RequestedTheme();
        }
    }
    xaml::ElementTheme wanted = root.RequestedTheme();
    if (!dark) {
        if (m_modeOriginal) wanted = *m_modeOriginal;
        m_modeRoot = nullptr;
        m_modeOriginal.reset();
    } else {
        wanted = *dark ? xaml::ElementTheme::Dark : xaml::ElementTheme::Light;
    }
    if (refresh) {
        m_refreshReason = m_dirtyReason + (m_dirtyCount > 1 ? " and " + std::to_string(m_dirtyCount - 1) + " more" : "");
        // {ThemeResource} is resolved again on a change of mode only: the other mode for a moment, then the wanted one.
        bool shownDark = root.ActualTheme() == xaml::ElementTheme::Dark;
        root.RequestedTheme(shownDark ? xaml::ElementTheme::Light : xaml::ElementTheme::Dark);
    }
    if (refresh || root.RequestedTheme() != wanted) root.RequestedTheme(wanted);
    if (refresh) ReadPopupsAgain(root.XamlRoot());
    SetCaptionMode(dark);
    return refresh;
}

namespace {

using ColourRef = winrt::Windows::Foundation::IReference<winrt::Windows::UI::Color>;

bool SameColour(const ColourRef& a, const ColourRef& b) {
    if (!a || !b) return !a && !b;
    winrt::Windows::UI::Color x = a.Value(), y = b.Value();
    return x.A == y.A && x.R == y.R && x.G == y.G && x.B == y.B;
}

std::string ColourText(const ColourRef& colour) {
    if (!colour) return "unset";
    winrt::Windows::UI::Color c = colour.Value();
    char hex[10];
    std::snprintf(hex, sizeof(hex), "#%02X%02X%02X%02X", c.A, c.R, c.G, c.B);
    return hex;
}

ColourRef Grey(std::uint8_t alpha, std::uint8_t value) {
    return ColourRef(winrt::Windows::UI::Color{alpha, value, value, value});
}

// The header of a playlist's page and of an album's (.album-header in builtins.cpp).
bool IsPageHeader(const std::string& name) {
    return name == "ContainerDetailHeaderLockupRegularContent" || name == "AlbumDetailHeaderLockupContent";
}

}  // namespace

// The system draws the caption buttons from the title bar's colours, not from the XAML tree, and Apple Music sets them
// for the system's mode: black on the dark themes of a PC in light mode. They follow the theme's mode, with the
// alphas Apple uses (0x33 under the pointer, 0x66 pressed), and get the app's own back when the theme goes.
void XamlTreeBackend::SetCaptionMode(std::optional<bool> dark) {
    try {
        xaml::Window window = m_window.get();
        if (!window || (!dark && !m_captionOriginal)) return;
        auto bar = window.AppWindow().TitleBar();
        if (dark && !m_captionOriginal) {
            m_captionOriginal = CaptionColours{bar.ButtonForegroundColor(), bar.ButtonHoverForegroundColor(),
                                               bar.ButtonHoverBackgroundColor(), bar.ButtonPressedForegroundColor(),
                                               bar.ButtonPressedBackgroundColor()};
            m_captionNote = "Themes: caption buttons follow the theme's mode (the app's foreground was " +
                            ColourText(m_captionOriginal->foreground) + ")";
        }
        if (!dark) {
            WriteCaption(bar, Restored(bar));
            m_captionOriginal.reset();
            m_captionWritten.reset();
            return;
        }
        std::uint8_t ink = *dark ? 0xFF : 0x00;
        m_captionWritten = CaptionColours{Grey(0xFF, ink), Grey(0xFF, ink), Grey(0x33, ink), Grey(0xFF, ink), Grey(0x66, ink)};
        WriteCaption(bar, *m_captionWritten);
    } catch (...) {
    }
}

// The app's colours back, except where the app wrote its own since (a change of the system's mode): those stay.
XamlTreeBackend::CaptionColours XamlTreeBackend::Restored(const winrt::Microsoft::UI::Windowing::AppWindowTitleBar& bar) const {
    CaptionColours restore = *m_captionOriginal;
    CaptionColours written = m_captionWritten.value_or(restore);
    auto keepApps = [](ColourRef& target, const ColourRef& now, const ColourRef& ours) {
        if (!SameColour(now, ours)) target = now;
    };
    keepApps(restore.foreground, bar.ButtonForegroundColor(), written.foreground);
    keepApps(restore.hoverForeground, bar.ButtonHoverForegroundColor(), written.hoverForeground);
    keepApps(restore.hoverBackground, bar.ButtonHoverBackgroundColor(), written.hoverBackground);
    keepApps(restore.pressedForeground, bar.ButtonPressedForegroundColor(), written.pressedForeground);
    keepApps(restore.pressedBackground, bar.ButtonPressedBackgroundColor(), written.pressedBackground);
    return restore;
}

// Writes only the colours that differ: each write makes the system draw the caption buttons again.
void XamlTreeBackend::WriteCaption(const winrt::Microsoft::UI::Windowing::AppWindowTitleBar& bar,
                                   const CaptionColours& wanted) {
    if (!SameColour(bar.ButtonForegroundColor(), wanted.foreground)) bar.ButtonForegroundColor(wanted.foreground);
    if (!SameColour(bar.ButtonHoverForegroundColor(), wanted.hoverForeground)) {
        bar.ButtonHoverForegroundColor(wanted.hoverForeground);
    }
    if (!SameColour(bar.ButtonHoverBackgroundColor(), wanted.hoverBackground)) {
        bar.ButtonHoverBackgroundColor(wanted.hoverBackground);
    }
    if (!SameColour(bar.ButtonPressedForegroundColor(), wanted.pressedForeground)) {
        bar.ButtonPressedForegroundColor(wanted.pressedForeground);
    }
    if (!SameColour(bar.ButtonPressedBackgroundColor(), wanted.pressedBackground)) {
        bar.ButtonPressedBackgroundColor(wanted.pressedBackground);
    }
}

xaml::DependencyObject XamlTreeBackend::Resolve(theme::NodeId id) const {
    // A decor first: its id is the address of a new Image, which may be that of an element of the last snapshot
    // destroyed since, still listed in m_elements with a dead reference.
    for (const auto& [key, decor] : m_decors) {
        if (decor.imageId == id) return decor.image;
    }
    auto it = m_elements.find(id);
    if (it != m_elements.end()) return it->second.get();  // null when the element has been destroyed since the snapshot
    // An element written to or followed for :hover may be out of the last snapshot and still alive: the entry of a
    // closed menu, a row scrolled away.
    if (auto touched = m_touched.find(id); touched != m_touched.end()) return touched->second.get();
    auto hover = m_hoverHandlers.find(id);
    if (hover != m_hoverHandlers.end()) {
        if (xaml::UIElement element = hover->second.element.get()) return element;
    }
    return nullptr;
}

theme::Value XamlTreeBackend::Keep(IInspectable object) {
    void* identity = ComIdentity(object);
    auto known = m_keptTokens.find(identity);
    if (known != m_keptTokens.end()) return {theme::ValueKind::Opaque, "handle:" + std::to_string(known->second)};

    std::uint64_t token = m_nextToken++;
    m_kept[token] = object;  // holds the object, so its identity cannot be reused while the token exists
    m_keptTokens[identity] = token;
    return {theme::ValueKind::Opaque, "handle:" + std::to_string(token)};
}

void XamlTreeBackend::Remember(theme::NodeId id, std::string_view property, const theme::Value& value, IInspectable object) {
    Written written{value, std::move(object), {}};
    if (xaml::DependencyObject element = Resolve(id)) written.element = winrt::make_weak(element);
    m_written[std::make_pair(id, std::string(property))] = std::move(written);
}

std::optional<theme::Value> XamlTreeBackend::Recognize(theme::NodeId id, std::string_view property,
                                                       const IInspectable& object) const {
    auto it = m_written.find(std::make_pair(id, std::string(property)));
    if (it == m_written.end() || it->second.object != object) return std::nullopt;
    return it->second.value;
}

void XamlTreeBackend::Forget(theme::NodeId id, std::string_view property) {
    m_written.erase(std::make_pair(id, std::string(property)));
}

media::Brush XamlTreeBackend::ArtworkBrush(const std::string& text) {
    media::ImageBrush brush;
    std::string stretch = text.substr(text.find('|') == std::string::npos ? text.size() : text.find('|') + 1);
    brush.Stretch(stretch == "Uniform" ? media::Stretch::Uniform
                  : stretch == "Fill"  ? media::Stretch::Fill
                                       : media::Stretch::UniformToFill);
    if (m_artwork) brush.ImageSource(m_artwork);
    m_artworkLayers.push_back(ArtworkLayer{brush, nullptr, {}});
    return brush;
}

// `content: artwork`: a Border painted by an artwork brush, with the layer that fades the previous cover out on top.
controls::Border XamlTreeBackend::ArtworkDecor(const std::string& text, const wchar_t* name) {
    controls::Border artwork;
    artwork.Name(name);
    artwork.IsHitTestVisible(false);
    xaml::Automation::AutomationProperties::SetAccessibilityView(artwork, xaml::Automation::Peers::AccessibilityView::Raw);
    auto brush = ArtworkBrush(text).as<media::ImageBrush>();
    ArtworkLayer& layer = m_artworkLayers.back();
    layer.owner = artwork;
    layer.fade = MakeArtworkFade(brush.Stretch());
    artwork.Background(brush);
    artwork.Child(layer.fade.layer);
    return artwork;
}

bool XamlTreeBackend::DecorHolds(const controls::Border& owner) const {
    for (const auto& [key, decor] : m_decors) {
        if (decor.image && ComIdentity(decor.image) == ComIdentity(owner)) return true;
    }
    return false;
}

// Gives the artwork brushes the image of the song playing, and lets go of those nothing holds any more (the theme was
// removed, the decor replaced). Strong references: weak ones to what Ampify made never resolve in Apple Music.
void XamlTreeBackend::FollowArtwork(const media::ImageSource& artwork) {
    if (artwork) m_artwork = artwork;  // between two songs the image may be missing for a moment: keep the last one
    // COM identity: the brush is stored as a Brush, an interface pointer other than the ImageBrush one.
    std::erase_if(m_artworkLayers, [&](const ArtworkLayer& layer) {
        if (layer.owner) return !DecorHolds(layer.owner);
        for (const auto& [key, written] : m_written) {
            if (ComIdentity(written.object) == ComIdentity(layer.brush)) return false;
        }
        return true;
    });
    for (ArtworkLayer& layer : m_artworkLayers) {
        media::ImageSource shown = layer.brush.ImageSource();
        if (!m_artwork || shown == m_artwork) continue;
        if (layer.owner) FadeFromPrevious(layer.fade, shown, layer.owner.CornerRadius());
        layer.brush.ImageSource(m_artwork);
    }
}

// Reads the colours of an artwork once per image: rendered small, then median cut (color_extract.hpp). Each prefix
// ("" for the play bar, kPageArtworkPrefix for the page on screen) holds its own variables and key. A blank or failed
// reading leaves the key unset, so the next pass tries again.
void XamlTreeBackend::SampleArtwork(const controls::Image& image, const std::string& prefix) {
    constexpr int kSampleSize = 48;
    media::ImageSource source = image.Source();
    if (!source || image.ActualWidth() < 1) return;
    std::string key;
    if (auto bitmap = source.try_as<media::Imaging::BitmapImage>()) {
        if (bitmap.PixelWidth() == 0) return;  // not decoded yet
        if (auto uri = bitmap.UriSource()) key = Utf8(uri.AbsoluteUri());
    }
    if (key.empty()) key = std::to_string(reinterpret_cast<std::uintptr_t>(winrt::get_abi(source)));
    std::shared_ptr<ArtworkColours> shared = m_artworkColours;
    bool later = false;
    {
        std::lock_guard<std::mutex> guard(shared->lock);
        if (shared->keys[prefix] == key) return;
        later = !shared->busy.insert(prefix).second;
    }
    if (later && !prefix.empty()) return;  // the page's artwork waits for the next pass
    if (later) {
        // The reading of the previous image is still under way: this one is read right after it.
        constexpr auto kRetry = std::chrono::milliseconds(120);
        try {
            if (!m_artworkRetry) {
                m_artworkRetry = image.DispatcherQueue().CreateTimer();
                m_artworkRetry.IsRepeating(false);
                std::weak_ptr<bool> alive = m_alive;
                m_artworkRetry.Tick([this, alive](auto const&, auto const&) noexcept {
                    if (!alive.expired()) OnArtworkChanged();
                });
            }
            m_artworkRetry.Interval(kRetry);
            m_artworkRetry.Start();
        } catch (...) {
        }
        return;
    }
    auto done = [shared, prefix](std::optional<std::map<std::string, std::string>> variables, const std::string& from) {
        {
            std::lock_guard<std::mutex> guard(shared->lock);
            shared->busy.erase(prefix);
            if (!variables) return;
            const std::string own = prefix + "art-";
            std::erase_if(shared->variables, [&](const auto& entry) { return entry.first.starts_with(own); });
            for (auto& [name, value] : *variables) shared->variables[prefix + name] = std::move(value);
            shared->keys[prefix] = from;
        }
        shared->changed.notify_all();
    };
    try {
        media::Imaging::RenderTargetBitmap target;
        using winrt::Windows::Foundation::AsyncStatus;
        target.RenderAsync(image, kSampleSize, kSampleSize)
            .Completed([target, key, done](auto const&, AsyncStatus status) {
                if (status != AsyncStatus::Completed) return done(std::nullopt, key);
                target.GetPixelsAsync().Completed([target, key, done](auto const& operation, AsyncStatus read) {
                    try {
                        if (read != AsyncStatus::Completed) return done(std::nullopt, key);
                        auto buffer = operation.GetResults();
                        size_t width = static_cast<size_t>(target.PixelWidth());
                        size_t height = static_cast<size_t>(target.PixelHeight());
                        if (buffer.Length() != width * height * 4) return done(std::nullopt, key);
                        // BGRA with premultiplied alpha, to the straight RGBA the extractor reads.
                        std::vector<uint8_t> rgba(buffer.Length());
                        const uint8_t* bgra = buffer.data();
                        for (size_t i = 0; i < rgba.size(); i += 4) {
                            uint8_t alpha = bgra[i + 3];
                            auto straight = [alpha](uint8_t channel) {
                                return alpha ? static_cast<uint8_t>(std::min(255, channel * 255 / alpha)) : uint8_t{0};
                            };
                            rgba[i] = straight(bgra[i + 2]);
                            rgba[i + 1] = straight(bgra[i + 1]);
                            rgba[i + 2] = straight(bgra[i]);
                            rgba[i + 3] = alpha;
                        }
                        std::optional<ExtractedColors> colors = ExtractColors(rgba.data(), rgba.size(), width, height);
                        if (!colors) return done(std::nullopt, key);
                        done(ArtworkVariables(*colors), key);
                    } catch (...) {
                        done(std::nullopt, key);
                    }
                });
            });
    } catch (...) {
        done(std::nullopt, key);
    }
}

// Follows the play bar's artwork between passes: ImageOpened for an image that loads, and Source for one Apple Music
// had already loaded (a cached artwork opens nothing). Apple's own image, so a weak reference resolves. The handlers
// read the artwork at once, not through SchedulePass: a song's colours follow it within a quarter second, and
// OnArtworkChanged lets nothing escape into Apple Music's XAML.
void XamlTreeBackend::FollowArtworkImage(const controls::Image& image) {
    controls::Image followed = m_artworkImage.get();
    if (followed == image) return;
    try {
        if (followed) {
            followed.ImageOpened(m_artworkOpened);
            followed.UnregisterPropertyChangedCallback(controls::Image::SourceProperty(), m_artworkSourceToken);
        }
        std::weak_ptr<bool> alive = m_alive;
        m_artworkOpened = image.ImageOpened([this, alive](IInspectable const&, xaml::RoutedEventArgs const&) noexcept {
            if (!alive.expired()) OnArtworkChanged();
        });
        m_artworkSourceToken = image.RegisterPropertyChangedCallback(
            controls::Image::SourceProperty(),
            [this, alive](xaml::DependencyObject const&, xaml::DependencyProperty const&) noexcept {
                if (!alive.expired()) OnArtworkChanged();
            });
        m_artworkImage = winrt::make_weak(image);
    } catch (...) {
        m_artworkImage = nullptr;  // the next pass tries again
    }
}

void XamlTreeBackend::OnArtworkChanged() noexcept {
    try {
        if (*m_closed) return;
        controls::Image image = m_artworkImage.get();
        if (!image) return;
        FollowArtwork(image.Source());
        SampleArtwork(image);
    } catch (...) {
    }
}

IInspectable XamlTreeBackend::Restore(const theme::Value& token) const {
    std::uint64_t number = std::stoull(token.text.substr(std::string("handle:").size()));
    auto it = m_kept.find(number);
    return it == m_kept.end() ? IInspectable{nullptr} : it->second;
}

std::vector<theme::NodeInfo> XamlTreeBackend::Snapshot() {
    std::vector<theme::NodeInfo> nodes;
    m_elements.clear();

    if (*m_closed) return nodes;
    xaml::Window window = m_window.get();  // the main window, from AMP.Utils (Attach)
    if (!window) window = AppWindow(L"MainWindow");
    if (!window) return nodes;
    xaml::UIElement content = window.Content();
    if (!content) return nodes;
    // A popup this pass finds first may still be filling in: it gets its passes as if the check had found it. A popup
    // torn down meanwhile can throw: the walk of the window still runs.
    try {
        if (m_pass) WatchPopups(content);
        if (LearnPopups(content.XamlRoot())) SchedulePass(1);
    } catch (...) {
    }

    // Preorder walk with an explicit stack. A parent is added before its children, as the engine expects. The window's
    // content comes first, then each popup as a root of its own.
    struct Pending {
        xaml::DependencyObject object;
        int parent;
        int depth;
        int position;
        int siblings;
        bool player = false;  // in one of Apple's player windows
    };
    std::vector<Pending> stack;
    for (auto it = m_popups.rbegin(); it != m_popups.rend(); ++it) {
        if (primitives::Popup popup = it->get()) stack.push_back({popup, -1, 0, 0, 1});
    }
    // Apple's full-screen player is a window of its own on this same thread: its content is a root as well, so themes
    // reach it. A window on another thread (the mini player) cannot be walked from here.
    for (const wchar_t* name : {L"FullScreenPlayerWindow", L"MiniPlayerWindow"}) {
        try {
            xaml::Window other = AppWindow(name);
            if (!other || other == window || !other.DispatcherQueue().HasThreadAccess()) continue;
            if (xaml::UIElement root = other.Content()) stack.push_back({root, -1, 0, 0, 1, true});
        } catch (...) {
        }
    }
    stack.push_back({content, -1, 0, 0, 1});
    std::set<void*> popupsSeen;  // a popup of the window's tree, reached there first, is not walked twice
    std::set<theme::NodeId> decorPanels;  // their ::before and ::after images are left out, so no selector meets them
    for (const auto& [key, decor] : m_decors) decorPanels.insert(key.first);
    std::vector<int> playingBars;  // the visible Grid#NowPlayingIndicator of list rows (MarkPlayingRows)
    int lcdDepth = -1;
    int headerDepth = -1;
    media::ImageSource artwork{nullptr};
    controls::Image artworkImage{nullptr};
    controls::Image pageImage{nullptr};
    while (!stack.empty()) {
        Pending item = stack.back();
        stack.pop_back();
        auto popup = item.object.try_as<primitives::Popup>();
        if (popup && !popupsSeen.insert(winrt::get_abi(popup.as<winrt::Windows::Foundation::IUnknown>())).second) continue;
        if (IsStandIn(item.object)) continue;  // it bears a moved element's name: no selector must meet it

        theme::NodeInfo node;
        node.id = reinterpret_cast<std::uint64_t>(winrt::get_abi(item.object));
        node.fullType = Utf8(winrt::get_class_name(item.object));
        node.type = ShortName(node.fullType);
        if (auto element = item.object.try_as<xaml::FrameworkElement>()) node.name = Utf8(element.Name());
        if (auto text = item.object.try_as<controls::TextBlock>()) node.icon = IsIconText(text.Text());
        if (auto panel = item.object.try_as<controls::Panel>()) node.panel = TakesDecor(panel);
        node.parent = item.parent;
        node.depth = item.depth;
        node.position = item.position;
        node.siblings = item.siblings;
        node.player = item.player;
        // The play bar's artwork: the first Image#StaticArtwork under NowPlayingControl#LCD.
        if (lcdDepth >= 0 && item.depth <= lcdDepth) lcdDepth = -1;
        if (node.name == "LCD" && node.type == "NowPlayingControl") lcdDepth = item.depth;
        if (lcdDepth >= 0 && !artwork && node.name == "StaticArtwork") {
            if (auto image = item.object.try_as<controls::Image>()) {
                artwork = image.Source();
                artworkImage = image;
            }
        }
        // The artwork of the album or playlist on screen: the first Image#StaticArtwork of its page's header.
        if (headerDepth >= 0 && item.depth <= headerDepth) headerDepth = -1;
        if (node.type == "Grid" && IsPageHeader(node.name)) headerDepth = item.depth;
        if (headerDepth >= 0 && !pageImage && node.name == "StaticArtwork") pageImage = item.object.try_as<controls::Image>();

        int index = static_cast<int>(nodes.size());
        if (node.name == "NowPlayingIndicator") {
            auto element = item.object.try_as<xaml::UIElement>();
            if (element && element.Visibility() == xaml::Visibility::Visible) playingBars.push_back(index);
        }
        nodes.push_back(node);
        m_elements.emplace(node.id, winrt::weak_ref<xaml::DependencyObject>(item.object));
        if (m_pass) Watch(item.object, node.id, node.type);

        int count = media::VisualTreeHelper::GetChildrenCount(item.object);
        if (decorPanels.count(node.id)) {
            std::vector<xaml::DependencyObject> own;
            for (int i = 0; i < count; i++) {
                xaml::DependencyObject child = media::VisualTreeHelper::GetChild(item.object, i);
                if (!IsDecor(child)) own.push_back(child);
            }
            const int shown = static_cast<int>(own.size());
            for (int i = shown - 1; i >= 0; i--) {
                stack.push_back({own[i], index, item.depth + 1, i, shown, item.player});
            }
        } else {
            for (int i = count - 1; i >= 0; i--) {
                stack.push_back(
                    {media::VisualTreeHelper::GetChild(item.object, i), index, item.depth + 1, i, count, item.player});
            }
        }
        // A popup's content is not one of its visual children.
        if (popup && count == 0) {
            if (xaml::UIElement child = popup.Child()) stack.push_back({child, index, item.depth + 1, 0, 1, item.player});
        }
    }

    theme::MarkPlayingRows(nodes, playingBars);

    // What Ampify remembers of destroyed elements cannot be used any more: a long session stays small.
    for (auto it = m_written.begin(); it != m_written.end();) {
        if (it->second.element.get()) {
            ++it;
            continue;
        }
        m_translations.erase(it->first.first);
        m_clipSizes.erase(it->first.first);
        it = m_written.erase(it);
    }
    // A part rebuilt by its template leaves its control behind: the keys it held there go back to Apple Music's.
    std::vector<std::pair<theme::NodeId, std::string>> gone;
    for (const auto& [entry, held] : m_heldStates) {
        if (!held.element.get()) gone.push_back(entry);
    }
    for (const auto& [id, property] : gone) ReleaseStates(id, property);
    std::erase_if(m_checked, [](const auto& entry) { return !entry.second.control.get(); });
    FollowArtwork(artwork);
    if (artworkImage) {
        FollowArtworkImage(artworkImage);
        SampleArtwork(artworkImage);
    }
    if (pageImage) SampleArtwork(pageImage, kPageArtworkPrefix);
    std::erase_if(m_watched, [](const auto& entry) { return !entry.second.get(); });
    std::erase_if(m_touched, [](const auto& entry) { return !entry.second.get(); });
    DropDeadHolds();
    std::erase_if(m_decors, [this](const auto& entry) {
        if (entry.second.panel.get()) return false;
        m_bars.Drop(entry.second.image);
        m_animations.Forget(entry.second.imageId);
        return true;
    });
    for (auto it = m_hoverHandlers.begin(); it != m_hoverHandlers.end();) {
        if (it->second.element.get()) {
            ++it;
            continue;
        }
        m_hover.Forget(it->first, *this);
        it = m_hoverHandlers.erase(it);
    }
    m_animations.Sweep([this](theme::NodeId id) {
        return std::any_of(m_decors.begin(), m_decors.end(), [id](const auto& entry) { return entry.second.imageId == id; });
    });
    return nodes;
}

void XamlTreeBackend::WatchChanges(std::function<void()> pass) {
    m_pass = std::move(pass);
}

void XamlTreeBackend::Watch(const xaml::DependencyObject& object, theme::NodeId id, const std::string& type) {
    if (type != "Frame" && type != "ListView" && type != "GridView" && type != "ItemsRepeater") return;
    if (auto known = m_watched.find(id); known != m_watched.end() && known->second.get() == object) return;
    // The handlers run inside Apple Music's XAML: SchedulePass lets nothing escape, and the weak pointer makes them
    // do nothing once the backend is gone, as for the hover handlers.
    std::weak_ptr<bool> alive = m_alive;
    try {
        if (auto frame = object.try_as<controls::Frame>()) {
            frame.Navigated([this, alive](IInspectable const&, auto const&) noexcept {
                if (!alive.expired()) SchedulePass(2);
            });
        } else if (auto list = object.try_as<controls::ListViewBase>()) {
            list.ContainerContentChanging([this, alive](auto const&, auto const&) noexcept {
                if (!alive.expired()) SchedulePass(0);
            });
        } else if (auto repeater = object.try_as<controls::ItemsRepeater>()) {
            repeater.ElementPrepared([this, alive](auto const&, auto const&) noexcept {
                if (!alive.expired()) SchedulePass(0);
            });
        } else {
            return;
        }
    } catch (...) {
        return;
    }
    m_watched[id] = winrt::make_weak(object);
}

void XamlTreeBackend::FollowAppleRewrites(theme::NodeId id, const xaml::DependencyObject& object,
                                          std::string_view property) {
    // Apple writes a lyrics line's alignment again in code (wrapped rows went back to the left for up to a tick).
    auto text = object.try_as<controls::TextBlock>();
    if (!text || property != "TextAlignment") return;
    if (auto known = m_rewritesFollowed.find(id); known != m_rewritesFollowed.end()) {
        if (known->second.text.get() == text) return;
        StopFollowingRewrites(id);
    }
    std::erase_if(m_rewritesFollowed, [](const auto& entry) { return !entry.second.text.get(); });
    std::weak_ptr<bool> alive = m_alive;
    RewriteFollow follow{winrt::make_weak(text)};
    try {
        follow.token = text.RegisterPropertyChangedCallback(controls::TextBlock::TextAlignmentProperty(),
                                                            [this, alive](auto const&, auto const&) noexcept {
                                                                if (!alive.expired()) SchedulePass(0);
                                                            });
    } catch (...) {
        return;
    }
    m_rewritesFollowed[id] = std::move(follow);
}

void XamlTreeBackend::StopFollowingRewrites(theme::NodeId id) noexcept {
    auto known = m_rewritesFollowed.find(id);
    if (known == m_rewritesFollowed.end()) return;
    try {
        if (controls::TextBlock text = known->second.text.get()) {
            text.UnregisterPropertyChangedCallback(controls::TextBlock::TextAlignmentProperty(), known->second.token);
        }
    } catch (...) {
    }
    m_rewritesFollowed.erase(known);
}

// The theme's value is gone: nothing of Ampify's keeps it up any more (an alignment followed, a `visible` held).
void XamlTreeBackend::GaveBack(theme::NodeId id, std::string_view property) {
    if (property == "TextAlignment") StopFollowingRewrites(id);
    if (property == "Visibility") ReleaseVisible(id);
}

void XamlTreeBackend::SchedulePass(int followUps) noexcept {
    // 60 ms groups the events of one scroll step or one page build; the follow-ups catch a page that fills in later
    // (its data arrives after the navigation).
    constexpr auto kSoon = std::chrono::milliseconds(60);
    try {
        if (*m_closed || !m_pass) return;
        m_followUps = std::max(m_followUps, followUps);
        if (!m_passTimer) {
            xaml::Window window = m_window.get();
            if (!window) return;
            m_passTimer = window.DispatcherQueue().CreateTimer();
            m_passTimer.IsRepeating(false);
            std::weak_ptr<bool> alive = m_alive;
            m_passTimer.Tick([this, alive](auto const&, auto const&) noexcept {
                if (!alive.expired()) RunPass();
            });
        }
        if (m_passTimer.IsRunning()) return;  // a pass is already due: this change rides along
        m_passTimer.Interval(kSoon);
        m_passTimer.Start();
    } catch (...) {
    }
}

void XamlTreeBackend::RunPass() noexcept {
    constexpr auto kFollowUp = std::chrono::milliseconds(450);
    try {
        if (*m_closed || !m_pass) return;
        m_pass();
        if (m_followUps > 0 && m_passTimer) {
            m_followUps--;
            m_passTimer.Interval(kFollowUp);
            m_passTimer.Start();
        }
    } catch (...) {
    }
}

// Remembers each popup open now, except Ampify's own and another mod's. True when one of them has opened since the
// last look: WinUI puts a menu's entries back into it at each opening, and Apple Music sets their colours again.
bool XamlTreeBackend::LearnPopups(const xaml::XamlRoot& root) {
    std::erase_if(m_popups, [](const auto& popup) { return !popup.get(); });
    std::vector<void*> open;
    bool opened = false;
    if (root) {
        for (const primitives::Popup& popup : media::VisualTreeHelper::GetOpenPopupsForXamlRoot(root)) {
            auto child = popup.Child().try_as<xaml::FrameworkElement>();
            std::string name = child ? Utf8(child.Name()) : std::string();
            if (uirender::IsOwnName(name) || uirender::IsForeignModName(name)) continue;
            void* identity = winrt::get_abi(popup.as<winrt::Windows::Foundation::IUnknown>());
            open.push_back(identity);
            if (std::find(m_openPopups.begin(), m_openPopups.end(), identity) == m_openPopups.end()) opened = true;
            if (std::none_of(m_popups.begin(), m_popups.end(), [&](const auto& kept) { return kept.get() == popup; })) {
                m_popups.push_back(winrt::make_weak(popup));
            }
        }
    }
    m_openPopups = std::move(open);
    return opened;
}

// A popup opened by a click or a key is looked for once the input has been dealt with (handled events too: the button
// that opens a flyout marks its click handled). The others, such as a tooltip or a submenu that opens under the
// pointer, within a quarter of a second.
void XamlTreeBackend::WatchPopups(const xaml::UIElement& content) {
    constexpr auto kPopupCheck = std::chrono::milliseconds(250);
    if (m_inputRoot.get() == content) return;
    std::weak_ptr<bool> alive = m_alive;
    if (!m_popupTimer) {
        m_popupTimer = content.DispatcherQueue().CreateTimer();
        m_popupTimer.Interval(kPopupCheck);
        m_popupTimer.Tick([this, alive](auto const&, auto const&) noexcept {
            if (!alive.expired()) CheckPopups();
        });
        m_popupTimer.Start();
    }
    auto check = [this, alive](IInspectable const&, auto const&) noexcept {
        try {
            if (alive.expired() || *m_closed) return;
            xaml::Window window = m_window.get();
            if (!window) return;
            window.DispatcherQueue().TryEnqueue(winrt::Microsoft::UI::Dispatching::DispatcherQueuePriority::Low,
                                                [this, alive]() noexcept {
                                                    if (!alive.expired()) CheckPopups();
                                                });
        } catch (...) {
        }
    };
    content.AddHandler(xaml::UIElement::PointerReleasedEvent(),
                       winrt::box_value(xaml::Input::PointerEventHandler(check)), true);
    content.AddHandler(xaml::UIElement::KeyUpEvent(), winrt::box_value(xaml::Input::KeyEventHandler(check)), true);
    m_inputRoot = winrt::make_weak(content);
}

// A popup that opens gets a pass now, and another once its content is filled in.
void XamlTreeBackend::CheckPopups() noexcept {
    try {
        if (*m_closed || !WindowShown()) return;  // a hidden window opens no menu
        xaml::Window window = m_window.get();
        if (!window) return;
        xaml::UIElement content = window.Content();
        if (content && LearnPopups(content.XamlRoot())) SchedulePass(1);
    } catch (...) {
    }
}

bool XamlTreeBackend::WindowShown() const {
    try {
        xaml::Window window = m_window.get();
        xaml::UIElement content = window ? window.Content() : nullptr;
        xaml::XamlRoot root = content ? content.XamlRoot() : nullptr;
        return !root || root.IsHostVisible();
    } catch (...) {
        return true;
    }
}

// A property Apple Music's element refuses (E_ACCESSDENIED, a sealed object, a value out of range) throws: it only
// costs that value, reported by the engine as refused, never the rest of the pass.
std::optional<theme::Value> XamlTreeBackend::Read(theme::NodeId id, std::string_view property) {
    try {
        return m_hover.Read(id, property, *this);
    } catch (...) {
        return std::nullopt;
    }
}

bool XamlTreeBackend::Write(theme::NodeId id, std::string_view property, const theme::Value& value) {
    try {
        if (!m_hover.Write(id, property, value, *this)) return false;
        if (auto it = m_elements.find(id); it != m_elements.end()) m_touched.insert_or_assign(id, it->second);
        return true;
    } catch (...) {
        return false;
    }
}

bool XamlTreeBackend::Alive(theme::NodeId id) {
    try {
        return static_cast<bool>(Resolve(id));
    } catch (...) {
        return false;
    }
}

// Only the inspector asks, once per dump: an element out of the live tree (a closed menu's entries) throws and has none.
std::optional<theme::NodeBox> XamlTreeBackend::Box(theme::NodeId id) {
    try {
        auto element = Resolve(id).try_as<xaml::FrameworkElement>();
        if (!element || !element.XamlRoot()) return std::nullopt;
        winrt::Windows::Foundation::Point origin =
            element.TransformToVisual(nullptr).TransformPoint(winrt::Windows::Foundation::Point{0, 0});
        return theme::NodeBox{origin.X, origin.Y, element.ActualWidth(), element.ActualHeight()};
    } catch (...) {
        return std::nullopt;
    }
}

// Puts the element into `panel` at `index` (or last). False when the panel refuses it.
static bool InsertChild(const controls::Panel& panel, const xaml::UIElement& element, int index) noexcept {
    try {
        controls::UIElementCollection children = panel.Children();
        const std::uint32_t size = children.Size();
        const auto place = index < 0 ? size : std::min(static_cast<std::uint32_t>(index), size);
        children.InsertAt(place, element);
        return true;
    } catch (...) {
        return false;
    }
}

// The parents walked up from a moved element to the visual states that name it: the times sit 2 to 4 below the root of
// the song display's template.
constexpr int kStatesReach = 8;

// A state's target given as the name, or as an element of that name (the element itself, or its stand-in).
static bool NamesElement(const IInspectable& target, const winrt::hstring& name) {
    if (!target) return false;
    if (auto framework = target.try_as<xaml::FrameworkElement>()) return framework.Name() == name;
    return winrt::unbox_value_or<winrt::hstring>(target, L"") == name;
}

// A state's animations that name the element point at `to` (with no `to`, nothing changes). True when one names it.
static bool PointStory(const animation::Storyboard& story, const winrt::hstring& name,
                       const xaml::DependencyObject& to) {
    bool named = false;
    bool running = false;
    for (const animation::Timeline& timeline : story.Children()) {
        try {
            if (animation::Storyboard::GetTargetName(timeline) != name) continue;
            if (!to) return true;
            // A state that ran keeps its end on its old target (the song's times faded out): it lets go, and plays
            // again on the new one, so the element back in place looks as Apple left the state.
            if (!named) {
                running = story.GetCurrentState() != animation::ClockState::Stopped;
                story.Stop();
            }
            named = true;
            animation::Storyboard::SetTarget(timeline, to);
        } catch (...) {
        }
    }
    try {
        if (running) story.Begin();
    } catch (...) {
    }
    return named;
}

// One state's setters and animations that name the element point at `to` (with no `to`, nothing changes). True when
// the state names it.
static bool PointState(const xaml::VisualState& state, const winrt::hstring& name, const xaml::DependencyObject& to) {
    bool named = false;
    for (const xaml::SetterBase& base : state.Setters()) {
        try {
            auto setter = base.try_as<xaml::Setter>();
            xaml::TargetPropertyPath path = setter ? setter.Target() : nullptr;
            if (!path || !NamesElement(path.Target(), name)) continue;
            named = true;
            if (to) path.Target(to);
        } catch (...) {
        }
    }
    animation::Storyboard story = state.Storyboard();
    return (story && PointStory(story, name, to)) || named;
}

// The states of `from` and the parents above it that name the element point at `to`. True when one of them names it.
static bool PointStates(const xaml::DependencyObject& from, const winrt::hstring& name,
                        const xaml::DependencyObject& to) {
    bool named = false;
    xaml::DependencyObject node = from;
    for (int up = 0; node && up < kStatesReach; up++, node = media::VisualTreeHelper::GetParent(node)) {
        auto owner = node.try_as<xaml::FrameworkElement>();
        if (!owner) continue;
        for (const xaml::VisualStateGroup& group : xaml::VisualStateManager::GetVisualStateGroups(owner)) {
            for (const xaml::VisualState& state : group.States()) named = PointState(state, name, to) || named;
        }
    }
    return named;
}

// An element of the same class and name, empty, sized 0 and out of the walk (Snapshot), that stands where a moved
// element was. Null when the class cannot be made outside its template.
xaml::FrameworkElement XamlTreeBackend::StandIn(const xaml::FrameworkElement& element) {
    const std::wstring key = std::wstring(winrt::get_class_name(element)) + L"#" + std::wstring(element.Name());
    if (auto known = m_standIns.find(key); known != m_standIns.end()) return known->second;
    xaml::FrameworkElement made{nullptr};
    try {
        made = winrt::get_activation_factory(winrt::get_class_name(element)).ActivateInstance<xaml::FrameworkElement>();
        made.Name(element.Name());
        made.Width(0);
        made.Height(0);
        made.IsHitTestVisible(false);
        made.Visibility(xaml::Visibility::Collapsed);
        m_standInIds.insert(winrt::get_abi(made.as<xaml::DependencyObject>()));  // as the walk sees it
    } catch (...) {
        made = nullptr;
    }
    m_standIns.emplace(key, made);
    return made;
}

// Apple's states find their targets by name in the template, and an element that leaves the tree leaves its name
// there: moved, the times made the song display's pointer states fail ("animation target not specified", 0x800F1000)
// and Apple Music closed at the first hover of the song. The stand-in takes the element's place and name, and the
// states play on it, so they hide nothing the theme placed elsewhere; back in place, they drive the element again.
void XamlTreeBackend::HandStatesOver(const xaml::UIElement& element, const controls::Panel& from, std::uint32_t at,
                                     const controls::Panel& to) noexcept {
    try {
        auto framework = element.try_as<xaml::FrameworkElement>();
        if (!framework || framework.Name().empty()) return;
        PointStates(to, framework.Name(), element);
        if (!PointStates(from, framework.Name(), nullptr)) return;  // no state names it: nothing to stand in for
        xaml::FrameworkElement standIn = StandIn(framework);
        PointStates(from, framework.Name(), standIn ? xaml::DependencyObject(standIn) : element);
        if (!standIn) return;
        // Still in a template Apple Music has since rebuilt: it moves to the new one.
        if (auto old = media::VisualTreeHelper::GetParent(standIn).try_as<controls::Panel>()) {
            if (old == from) return;
            WithdrawStandIn(element, old);
        }
        from.Children().InsertAt(std::min(at, from.Children().Size()), standIn);
    } catch (...) {
    }
}

// Before the element comes back to `panel`, its stand-in leaves it: the name is the element's again.
void XamlTreeBackend::WithdrawStandIn(const xaml::UIElement& element, const controls::Panel& panel) noexcept {
    try {
        auto framework = element.try_as<xaml::FrameworkElement>();
        if (!framework || framework.Name().empty()) return;
        const std::wstring key = std::wstring(winrt::get_class_name(framework)) + L"#" + std::wstring(framework.Name());
        auto known = m_standIns.find(key);
        std::uint32_t at = 0;
        if (known == m_standIns.end() || !known->second || !panel.Children().IndexOf(known->second, at)) return;
        panel.Children().RemoveAt(at);
    } catch (...) {
    }
}

bool XamlTreeBackend::IsStandIn(const xaml::DependencyObject& object) const {
    return !m_standInIds.empty() && m_standInIds.count(winrt::get_abi(object)) > 0;
}

// The element leaves its panel and joins the other one at `index` (or last): the visual parent says which panel it
// leaves, so no snapshot is needed. When the other panel refuses it, the element goes back to its old place.
bool XamlTreeBackend::Reparent(theme::NodeId id, theme::NodeId parent, int index) {
    try {
        auto element = Resolve(id).try_as<xaml::UIElement>();
        auto target = Resolve(parent).try_as<controls::Panel>();
        if (!element || !target) return false;
        auto from = media::VisualTreeHelper::GetParent(element).try_as<controls::Panel>();
        std::uint32_t at = 0;
        if (!from || !from.Children().IndexOf(element, at)) return false;
        from.Children().RemoveAt(at);
        WithdrawStandIn(element, target);
        if (InsertChild(target, element, index)) {
            HandStatesOver(element, from, at, target);
            return true;
        }
        // Without this the scrubber and the times would vanish from Apple Music's bar.
        return InsertChild(from, element, static_cast<int>(at));
    } catch (...) {
        return false;
    }
}

// An icon or a text usually shows its button's colour and font, a template part its control's values: none of them
// its own. Those read as kNoOwnValue, so that removing the theme clears them instead of freezing what they showed.
std::optional<theme::Value> XamlTreeBackend::ReadOriginal(theme::NodeId id, std::string_view property) {
    try {
        xaml::DependencyObject object = Resolve(id);
        if (!object) return std::nullopt;
        if (!m_hover.Has(id)) {
            xaml::DependencyProperty own = OwnProperty(object, property);
            IInspectable local = own ? object.ReadLocalValue(own) : nullptr;
            if (own && local == xaml::DependencyProperty::UnsetValue()) return theme::kNoOwnValue;
            // Apple painted it in code with a brush of Ampify's resources: that brush comes back, recoloured by then
            // (OwnBrush), not the theme's colour it showed when it was read.
            if (local && IsOwnBrush(local)) return Keep(local);
        }
        return Read(id, property);
    } catch (...) {
        return std::nullopt;
    }
}

bool XamlTreeBackend::IsOwnBrush(const IInspectable& value) const {
    if (!value.try_as<media::SolidColorBrush>()) return false;
    return std::any_of(m_ownBrushes.begin(), m_ownBrushes.end(),
                       [&](const auto& own) { return own.second && own.second == value; });
}

// One brush per key for the life of the process, recoloured in place: Apple Music paints some texts in code (an
// album's artist) with the brush it looked up then, which kept a theme's accent after the theme until a restart.
media::SolidColorBrush XamlTreeBackend::OwnBrush(const std::string& key, ColorValue color) {
    media::SolidColorBrush& brush = m_ownBrushes.try_emplace(key, nullptr).first->second;
    if (!brush) brush = media::SolidColorBrush(color);
    else if (brush.Color() != color) brush.Color(color);
    return brush;
}

// The known colour keys the application holds. Its dictionaries are not enumerated: that realised every deferred
// resource out of its context, and Apple Music crashed at the next theme change (0xc000027b, ERROR_NOT_FOUND stowed in
// Microsoft.UI.Xaml.dll; 4 launches out of 4 with the inspector on, 31/10). Looking up a known key is safe.
std::vector<std::string> XamlTreeBackend::ResourceKeys() {
    std::vector<std::string> keys;
    xaml::ResourceDictionary resources = xaml::Application::Current().Resources();
    for (std::string_view key : theme::ColourResourceKeys()) {
        try {
            if (resources.HasKey(winrt::box_value(winrt::to_hstring(std::string(key))))) keys.emplace_back(key);
        } catch (...) {
        }
    }
    return keys;
}

std::optional<theme::Value> XamlTreeBackend::ReadResource(std::string_view key) {
    xaml::ResourceDictionary resources = xaml::Application::Current().Resources();
    winrt::hstring name = winrt::to_hstring(std::string(key));
    IInspectable value{nullptr};
    try {
        if (!resources.HasKey(winrt::box_value(name))) return std::nullopt;
        value = resources.Lookup(winrt::box_value(name));
    } catch (...) {
        return std::nullopt;  // a resource Apple Music cannot give out of its context: read as absent
    }
    if (auto solid = value.try_as<media::SolidColorBrush>()) return theme::Value{theme::ValueKind::Color, ColorText(solid.Color())};
    // A Color resource reads as a colour too: an opaque handle would differ on every tick and be written again.
    if (auto color = value.try_as<winrt::Windows::Foundation::IReference<ColorValue>>()) {
        return theme::Value{theme::ValueKind::Color, ColorText(color.Value())};
    }
    return Keep(value);
}

bool XamlTreeBackend::WriteResource(std::string_view key, const theme::Value& value) {
    std::string keyText(key);
    xaml::ResourceDictionary resources = xaml::Application::Current().Resources();
    IInspectable name = winrt::box_value(winrt::to_hstring(keyText));
    // The value Apple Music had comes back: a key Ampify added on top of the theme dictionaries is removed, so the
    // app's light and dark values work again, instead of the one read under the theme staying for good.
    if (auto added = m_addedKeys.find(keyText); added != m_addedKeys.end() && added->second == value) {
        m_addedKeys.erase(added);
        std::optional<ColorValue> before = value.kind == theme::ValueKind::Color ? ColorFromText(value.text) : std::nullopt;
        if (auto own = m_ownBrushes.find(keyText); own != m_ownBrushes.end() && own->second && before) {
            own->second.Color(*before);  // what Apple painted with it takes Apple's colour back
        }
        if (OwnsKey(resources, name)) resources.Remove(name);
        MarkResourcesDirty("resource " + keyText + " removed");
        return true;
    }
    if (!OwnsKey(resources, name) && !m_addedKeys.count(keyText)) {
        if (std::optional<theme::Value> before = ReadResource(key)) m_addedKeys.emplace(keyText, *before);
    }

    std::optional<IInspectable> object;
    if (value.kind == theme::ValueKind::Color) {
        std::optional<ColorValue> color = ColorFromText(value.text);
        if (!color) return false;
        object = OwnBrush(keyText, *color);
    } else if (value.kind == theme::ValueKind::Opaque) {
        object = Restore(value);
    }
    if (!object) return false;

    // Fluent names a Color without the Brush suffix (SolidBackgroundFillColorBase) and the brush with it: a brush in
    // place of a Color breaks every brush built on it, so the value keeps the kind already there.
    if (value.kind == theme::ValueKind::Color && resources.HasKey(name)) {
        if (std::optional<ColorValue> color = ColorFromText(value.text);
            color && resources.Lookup(name).try_as<winrt::Windows::Foundation::IReference<ColorValue>>()) {
            object = winrt::box_value(*color);
        }
    }
    // Our brush already holds the key: recoloured above, everything drawn with it follows, nothing to read again.
    if (OwnsKey(resources, name) && resources.Lookup(name) == *object) return true;
    resources.Insert(name, *object);
    MarkResourcesDirty("resource " + keyText);
    return true;
}

void XamlTreeBackend::MarkResourcesDirty(std::string why) {
    if (!m_resourcesDirty) {
        m_dirtyReason = std::move(why);
        m_dirtyCount = 0;
    }
    m_resourcesDirty = true;
    m_dirtyCount++;
}

bool XamlTreeBackend::SetDictionary(std::string_view xaml) {
    xaml::ResourceDictionary resources = xaml::Application::Current().Resources();
    auto merged = resources.MergedDictionaries();

    if (m_merged) {
        uint32_t index = 0;
        if (merged.IndexOf(m_merged, index)) merged.RemoveAt(index);
        m_merged = nullptr;
    }
    MarkResourcesDirty("dictionary");
    if (xaml.empty()) return true;

    xaml::ResourceDictionary dictionary = xaml::Markup::XamlReader::Load(winrt::to_hstring(std::string(xaml))).try_as<xaml::ResourceDictionary>();
    if (!dictionary) return false;
    merged.Append(dictionary);
    m_merged = dictionary;
    return true;
}

// A panel that refuses a child (a sealed collection, a decode the app refuses) costs only its own image.
theme::DecorChange XamlTreeBackend::SetDecor(theme::NodeId id, theme::PseudoElement slot, const theme::Decor* decor) {
    if (*m_closed) return theme::DecorChange::Refused;
    try {
        return decor ? PlaceDecor(id, slot, *decor) : RemoveDecor(id, slot);
    } catch (...) {
        return theme::DecorChange::Refused;
    }
}

// A changed decor gets a new Image where the old one stood (Children.SetAt): no value of the old one (a width the
// rule no longer gives, a running animation) is left behind, and the panel never holds two.
theme::DecorChange XamlTreeBackend::PlaceDecor(theme::NodeId id, theme::PseudoElement slot, const theme::Decor& decor) {
    controls::Panel panel{nullptr};
    if (xaml::DependencyObject object = Resolve(id)) panel = object.try_as<controls::Panel>();
    if (!TakesDecor(panel)) return theme::DecorChange::Refused;
    controls::UIElementCollection children = panel.Children();
    auto key = std::make_pair(id, slot);
    auto known = m_decors.find(key);
    const bool same = known != m_decors.end() && known->second.decor == decor;
    if (same && KeepDecorInPlace(children, known->second.image, slot)) return theme::DecorChange::Unchanged;
    xaml::FrameworkElement old{nullptr};  // held until it leaves the panel
    if (known != m_decors.end()) {
        old = known->second.image;
        m_animations.Forget(known->second.imageId);
    }
    xaml::FrameworkElement image{nullptr};
    const wchar_t* name = slot == theme::PseudoElement::Before ? L"AmpifyDecorBefore" : L"AmpifyDecorAfter";
    if (decor.image.starts_with(theme::kBarsContent)) {
        image = m_bars.Make(std::stoi(decor.image.substr(theme::kBarsContent.size())), name);
    } else if (decor.image.starts_with(theme::kArtworkImage)) {
        // The song's artwork, painted by a brush FollowArtwork keeps up to date: a Border, so it moves and turns.
        image = ArtworkDecor(decor.image, name);
    } else {
        image = DecorImageFrom(decor.image, slot);
    }
    auto imageId = reinterpret_cast<std::uint64_t>(winrt::get_abi(image.as<xaml::DependencyObject>()));
    m_decors[key] = DecorImage{winrt::make_weak(panel), image, imageId, decor};
    WriteDecor(imageId, panel, decor);
    std::uint32_t index = 0;
    if (old && children.IndexOf(old, index)) {
        children.SetAt(index, image);
    } else if (slot == theme::PseudoElement::Before) {
        children.InsertAt(0, image);
    } else {
        children.Append(image);
    }
    m_bars.Drop(old);
    // In the tree first: the compositor animates an element that has its visual.
    for (const auto& [property, value] : decor.properties) {
        if (property == "Animation") WriteRaw(imageId, property, value);
    }
    return theme::DecorChange::Applied;
}

// Size, margin, alignment, cell, opacity, transform: the values AppliesToDecor lets through, written as on any element.
// Over the whole Grid unless the theme names a row or a column, so the image lies over the panel.
void XamlTreeBackend::WriteDecor(theme::NodeId imageId, const controls::Panel& panel, const theme::Decor& decor) {
    bool row = false;
    bool column = false;
    for (const auto& [property, value] : decor.properties) {
        row = row || property == "Grid.Row" || property == "Grid.RowSpan";
        column = column || property == "Grid.Column" || property == "Grid.ColumnSpan";
        if (property != "Animation" && property != "Foreground") WriteRaw(imageId, property, value);
    }
    xaml::FrameworkElement element{nullptr};
    if (xaml::DependencyObject object = Resolve(imageId)) element = object.try_as<xaml::FrameworkElement>();
    // `color` paints the bars of bars(n); an image has no colour of its own.
    auto bars = decor.image.starts_with(theme::kBarsContent) && element ? element.try_as<controls::Grid>() : nullptr;
    for (const auto& [property, value] : decor.properties) {
        if (property != "Foreground" || !bars) continue;
        media::Brush brush{nullptr};
        if (value.kind == theme::ValueKind::Color) {
            if (std::optional<ColorValue> color = ColorFromText(value.text)) brush = media::SolidColorBrush(*color);
        } else if (value.kind == theme::ValueKind::Gradient) {
            brush = GradientFrom(value.text);
        }
        if (brush) m_bars.SetBrush(bars, brush);
    }
    auto grid = panel.try_as<controls::Grid>();
    if (!grid || !element) return;
    auto span = [](std::uint32_t count) { return std::max<std::int32_t>(1, static_cast<std::int32_t>(count)); };
    if (!row) controls::Grid::SetRowSpan(element, span(grid.RowDefinitions().Size()));
    if (!column) controls::Grid::SetColumnSpan(element, span(grid.ColumnDefinitions().Size()));
}

theme::DecorChange XamlTreeBackend::RemoveDecor(theme::NodeId id, theme::PseudoElement slot) {
    auto known = m_decors.find(std::make_pair(id, slot));
    if (known == m_decors.end()) return theme::DecorChange::Unchanged;
    DecorImage held = std::move(known->second);  // released at the end of this call, on the interface thread
    m_decors.erase(known);
    m_bars.Drop(held.image);
    m_animations.Forget(held.imageId);
    controls::Panel panel = held.panel.get();
    if (!panel) return theme::DecorChange::Refused;
    controls::UIElementCollection children = panel.Children();
    std::uint32_t index = 0;
    if (children.IndexOf(held.image, index)) children.RemoveAt(index);
    return theme::DecorChange::Applied;
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
