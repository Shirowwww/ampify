// Written against the C++/WinRT projection of the Windows App SDK. What WinUI 3 did differently from its documentation
// is in docs/development/xaml-lessons.md.

#if defined(AMPIFY_WITH_XAML)

#include "xaml_backend.hpp"

#include <atomic>
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
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
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
#include "artwork_cap.hpp"
#include "artwork_passthrough.hpp"
#include "player_controls.hpp"
#include "sidebar_hover.hpp"
#include "xaml_host.hpp"
#include "xaml_backend_detail.hpp"

namespace ampify::win {

using namespace xaml_detail;

namespace {

// The application's resources as the main window's thread sees them, by COM identity only (never called from another
// thread). WinUI is expected to give each interface thread its own copy, as UWP did for each view: the mini player's
// thread writing the theme into its copy is what colours it. When a build of WinUI shares one copy, the main window's
// backend alone writes it.
std::atomic<void*> g_mainResources{nullptr};

}  // namespace

bool XamlTreeBackend::Attach(WindowRole role) {
    if (*m_closed) return false;
    xaml::Window window = AppWindowOf(role);
    if (!window) return false;
    void* resources = winrt::get_abi(xaml::Application::Current().Resources());
    if (role == WindowRole::Main) {
        g_mainResources = resources;
    } else {
        void* main = g_mainResources.load();
        m_sharedResources = !main || main == resources;  // the main window not themed yet: nothing is assumed
    }
    m_role = role;
    m_window = winrt::make_weak(window);
    m_animations.WindowShown([handle = WindowHandle(window)] { return !handle || WindowOnScreen(handle); });
    // Walking or writing the tree while Apple Music closes crashed it in Microsoft.UI.Xaml.dll.
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
        return theme::ViewSize{bounds.Width, bounds.Height, SidebarKeptFolded(), SidebarOverPages()};
    } catch (...) {
        return std::nullopt;
    }
}

// The answer of each watched query, then whether the sidebar stands over the pages when the theme reads that.
std::vector<bool> XamlTreeBackend::MediaAnswers() {
    std::vector<bool> answers;
    std::optional<theme::ViewSize> size = WindowSize();
    for (const theme::MediaQuery& query : m_mediaQueries) answers.push_back(size && query.Matches(size->width, size->height, size->sidebarFolded));
    if (m_watchOverPages) answers.push_back(size && size->sidebarOverPages);
    return answers;
}

void XamlTreeBackend::WatchSize(const std::vector<theme::MediaQuery>& queries) {
    if (queries == m_mediaQueries) return;
    m_mediaQueries = queries;
    m_mediaAnswers = MediaAnswers();
}

void XamlTreeBackend::WatchSidebarOverPages(bool watch) {
    if (watch == m_watchOverPages) return;
    m_watchOverPages = watch;
    m_mediaAnswers = MediaAnswers();
}

// Resizing fires many times a second: only a change of answer costs a pass.
void XamlTreeBackend::OnSizeChanged() noexcept {
    try {
        if (m_mediaQueries.empty() && !m_watchOverPages) return;
        std::vector<bool> answers = MediaAnswers();
        if (answers == m_mediaAnswers) return;
        m_mediaAnswers = std::move(answers);
        SchedulePass(0);
    } catch (...) {
        // Nothing may escape into Apple Music's XAML frame.
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

std::string ColourText(const ColourRef& colour) {
    return colour ? ampify::win::ColourText(colour.Value()) : "unset";
}

ColourRef Grey(std::uint8_t alpha, std::uint8_t value) {
    return ColourRef(winrt::Windows::UI::Color{alpha, value, value, value});
}

// The header of a playlist's page, an album's or an editorial playlist's of Apple (.album-header in builtins.cpp).
bool IsPageHeader(const std::string& name) {
    return name == "ContainerDetailHeaderLockupRegularContent" || name == "AlbumDetailHeaderLockupContent" ||
           name == "PlaylistDetailHeaderLockupContent";
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
        // The title bar refuses the colours: Windows keeps its own.
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
    if (auto held = m_held.find(id); held != m_held.end()) return held->second;
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
    if (known != m_keptTokens.end()) return {theme::ValueKind::Opaque, theme::EncodeHandle(known->second)};

    std::uint64_t token = m_nextToken++;
    m_kept[token] = object;  // holds the object, so its identity cannot be reused while the token exists
    m_keptTokens[identity] = token;
    return {theme::ValueKind::Opaque, theme::EncodeHandle(token)};
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

IInspectable XamlTreeBackend::Restore(const theme::Value& token) const {
    std::optional<std::uint64_t> number = theme::DecodeHandle(token.text);
    auto it = number ? m_kept.find(*number) : m_kept.end();
    return it == m_kept.end() ? IInspectable{nullptr} : it->second;
}

std::vector<theme::NodeInfo> XamlTreeBackend::Snapshot() {
    std::vector<theme::NodeInfo> nodes;
    m_elements.clear();
    m_held.clear();

    if (*m_closed) return nodes;
    xaml::Window window = m_window.get();  // the window of m_role, from AMP.Utils (Attach)
    if (!window) window = AppWindowOf(m_role);
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
    // reach it. A window on another thread (the mini player) cannot be walked from here: it has a backend of its own.
    for (WindowRole role : {WindowRole::Player, WindowRole::Mini}) {
        if (m_role != WindowRole::Main) break;
        try {
            xaml::Window other = AppWindowOf(role);
            if (!other || other == window || !other.DispatcherQueue().HasThreadAccess()) continue;
            if (xaml::UIElement root = other.Content()) stack.push_back({root, -1, 0, 0, 1, true});
        } catch (...) {
            // A window of another thread, or one closing, is not walked.
        }
    }
    // A player window's own content stays a player's tree: only selectors that name the player reach it, as above.
    stack.push_back({content, -1, 0, 0, 1, m_role != WindowRole::Main});
    std::set<void*> popupsSeen;  // a popup of the window's tree, reached there first, is not walked twice
    std::set<theme::NodeId> decorPanels;  // their ::before and ::after images are left out, so no selector meets them
    for (const auto& [key, decor] : m_decors) decorPanels.insert(key.first);
    std::vector<int> playingBars;  // the visible Grid#NowPlayingIndicator of list rows (MarkPlayingRows)
    std::vector<int> litDots;      // the lit dots under Apple's mode buttons (MarkCheckedByDot)
    int lcdDepth = -1;
    xaml::FrameworkElement lcdArtwork{nullptr};  // the play bar's HoverGrid#ThumbnailHoverGrid (KeepArtworkPassthrough)
    xaml::FrameworkElement paneRoot{nullptr};    // the sidebar's Grid#PaneRoot, for the same
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
        // Ampify's app pages sit beside Apple's frame but stay out of themes, as when they lived in a popup: the kit
        // renderer owns their values.
        if (node.name == "AMPIFY_PageHost" || node.name == kLargeArtworkLoaderName || node.name == kEffectHostName) {
            continue;
        }
        if (auto text = item.object.try_as<controls::TextBlock>()) node.icon = IsIconText(text.Text());
        if (auto panel = item.object.try_as<controls::Panel>()) node.panel = TakesDecor(panel);
        if (m_watchStates) TakeStates(item.object, node);
        node.parent = item.parent;
        node.depth = item.depth;
        node.position = item.position;
        node.siblings = item.siblings;
        node.player = item.player;
        // The play bar's artwork: the first Image#StaticArtwork under NowPlayingControl#LCD.
        if (lcdDepth >= 0 && item.depth <= lcdDepth) lcdDepth = -1;
        if (node.name == "LCD" && node.type == "NowPlayingControl") lcdDepth = item.depth;
        if (lcdDepth >= 0 && !lcdArtwork && !item.player && node.name == "ThumbnailHoverGrid") {
            lcdArtwork = item.object.try_as<xaml::FrameworkElement>();
        }
        if (!paneRoot && !item.player && node.name == "PaneRoot") paneRoot = item.object.try_as<xaml::FrameworkElement>();
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
        if (m_watchStates && node.name.size() > 3 && node.name.ends_with("Dot")) {
            if (auto dot = item.object.try_as<xaml::FrameworkElement>()) {
                if (DotLit(dot)) litDots.push_back(index);
                WatchDot(dot, node.id);
            }
        }
        nodes.push_back(node);
        auto [kept, added] = m_elements.emplace(node.id, winrt::weak_ref<xaml::DependencyObject>(item.object));
        if (added && !kept->second.get()) m_held.insert_or_assign(node.id, item.object);
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
    if (m_watchStates) theme::MarkSelectedPresenters(nodes);
    if (m_watchStates) theme::MarkCheckedByDot(nodes, litDots);
    if (m_role == WindowRole::Main) {
        KeepArtworkCaps();  // its map belongs to the main window's thread
        KeepArtworkPassthrough(lcdArtwork, paneRoot);
        KeepVeilOnArtwork(lcdArtwork);
    }

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
    FollowArtwork(artworkImage, artwork);
    if (artworkImage) {
        FollowArtworkImage(artworkImage);
        SampleArtwork(artworkImage);
    }
    if (pageImage) SampleArtwork(pageImage, kPageArtworkPrefix);
    std::erase_if(m_watched, [](const auto& entry) { return !entry.second.get(); });
    std::erase_if(m_stateWatched, [](const auto& entry) { return !entry.second.get(); });
    std::erase_if(m_touched, [](const auto& entry) { return !entry.second.get(); });
    DropDeadHolds();
    m_paneCorners.Sweep();
    m_gradientFit.Sweep();
    m_blur.Sweep();
    m_widthGlides.Sweep();
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

// :checked, :selected and :disabled as the control says them. A change of toggle or selection asks for a pass at once;
// a control that turns disabled is seen at the next pass (watching IsEnabled would put a handler on every control).
void XamlTreeBackend::TakeStates(const xaml::DependencyObject& object, theme::NodeInfo& node) {
    xaml::DependencyProperty watched{nullptr};
    if (auto toggle = object.try_as<primitives::ToggleButton>()) {
        auto checked = toggle.IsChecked();
        node.checked = checked && checked.Value();
        watched = primitives::ToggleButton::IsCheckedProperty();
    } else if (auto toggleSwitch = object.try_as<controls::ToggleSwitch>()) {
        node.checked = toggleSwitch.IsOn();
        watched = controls::ToggleSwitch::IsOnProperty();
    } else if (auto item = object.try_as<primitives::SelectorItem>()) {
        node.selected = item.IsSelected();
        watched = primitives::SelectorItem::IsSelectedProperty();
    } else if (auto entry = object.try_as<controls::NavigationViewItemBase>()) {
        node.selected = entry.IsSelected();
        watched = controls::NavigationViewItemBase::IsSelectedProperty();
    }
    if (auto control = object.try_as<controls::Control>()) node.disabled = !control.IsEnabled();
    if (!watched) return;
    if (auto known = m_stateWatched.find(node.id); known != m_stateWatched.end() && known->second.get() == object) return;
    std::weak_ptr<bool> alive = m_alive;
    try {
        object.RegisterPropertyChangedCallback(watched, [this, alive](xaml::DependencyObject const&, xaml::DependencyProperty const&) {
            // The handler runs inside Apple Music's XAML: SchedulePass lets nothing escape, and the weak pointer makes
            // it do nothing once the backend is gone, as for the other watchers.
            if (!alive.expired() && m_watchStates) SchedulePass(0);
        });
        m_stateWatched[node.id] = winrt::make_weak(object);
    } catch (...) {
        // A control being torn down refuses the callback: its state is read again at the next pass.
    }
}

// A mode button's dot asks for a pass when Apple lights or clears it: its fill (replaced, or its colour changed),
// opacity or visibility. Once per dot, as TakeStates.
void XamlTreeBackend::WatchDot(const xaml::FrameworkElement& dot, theme::NodeId id) {
    if (auto known = m_stateWatched.find(id); known != m_stateWatched.end() && known->second.get() == dot) return;
    std::weak_ptr<bool> alive = m_alive;
    auto changed = [this, alive](xaml::DependencyObject const&, xaml::DependencyProperty const&) {
        if (!alive.expired() && m_watchStates) SchedulePass(0);
    };
    try {
        dot.RegisterPropertyChangedCallback(xaml::UIElement::OpacityProperty(), changed);
        dot.RegisterPropertyChangedCallback(xaml::UIElement::VisibilityProperty(), changed);
        if (auto shape = dot.try_as<xaml::Shapes::Shape>()) {
            shape.RegisterPropertyChangedCallback(xaml::Shapes::Shape::FillProperty(), changed);
            if (auto fill = shape.Fill().try_as<media::SolidColorBrush>()) {
                fill.RegisterPropertyChangedCallback(media::SolidColorBrush::ColorProperty(), changed);
            }
        }
        m_stateWatched[id] = winrt::make_weak(dot.as<xaml::DependencyObject>());
    } catch (...) {
        // A dot being torn down refuses the callback: it is read again at the next pass.
    }
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
        // The element may be torn down already: nothing is left to put back.
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
        // Nothing may escape into Apple Music's XAML frame.
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
        // Nothing may escape into Apple Music's XAML frame.
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
            if (uirender::IsAmpifyOrModName(name)) continue;
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
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
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
        // Nothing may escape into Apple Music's XAML frame.
    }
}

bool XamlTreeBackend::WindowShown() const {
    try {
        xaml::Window window = m_window.get();
        xaml::UIElement content = window ? window.Content() : nullptr;
        xaml::XamlRoot root = content ? content.XamlRoot() : nullptr;
        if (!root || root.IsHostVisible()) return true;
        // Hidden while Apple's full-screen player stands in its place: this window's passes theme that player (its
        // content is walked from here, on this thread), so they go on (minimal-fullscreen did nothing there).
        if (m_role != WindowRole::Main) return false;
        xaml::Window player = AppWindowOf(WindowRole::Player);
        if (!player || !player.DispatcherQueue().HasThreadAccess()) return false;
        xaml::UIElement shown = player.Content();
        return shown && shown.XamlRoot() && shown.XamlRoot().IsHostVisible();
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

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
