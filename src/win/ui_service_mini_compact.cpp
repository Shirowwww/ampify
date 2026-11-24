// The compact mini player a theme asks for with --mini-window: compact (src/core/mini_layout.hpp; the Minimal mini
// player snippet does). While Apple shows its compact view (the lyrics and Up Next panes closed, no album view), the
// window ends under the song's row and Apple's row of controls is raised over that row, with Ampify's buttons in the
// band above it (MiniButtonHost). Apple sizes the window itself at each change of view and never shrinks it back, so
// each refresh (the window's SizeChanged queues one) cuts it again; everything is given back when Apple shows more.

#if defined(AMPIFY_WITH_XAML)

#include "ui_service_detail.hpp"

#include <winrt/Microsoft.UI.Xaml.Data.h>

#include "../core/mini_layout.hpp"
#include "ui_renderer.hpp"

namespace ampify::win::ui_service_detail {

namespace {

constexpr std::size_t kSearchLimit = 4000;  // the panes' elements come before the controls in the mini player's tree
constexpr double kDefaultBand = 32;          // Apple's caption band, when Grid#AppTitleBar is not found

struct MiniParts {
    xaml::FrameworkElement view{nullptr};      // MiniPlayerView#miniPlayerView
    controls::Grid display{nullptr};           // Grid#LCD: the song's row
    xaml::FrameworkElement panes{nullptr};     // Grid#LyricsGrid: collapsed while both panes are closed
    xaml::FrameworkElement album{nullptr};     // Grid#AlbumViewContentBackground: shown in the album view only
    xaml::FrameworkElement titleBar{nullptr};  // Grid#AppTitleBar: the band of the caption buttons
    controls::Grid content{nullptr};           // Grid#MainContent: the song's row, then the bottom part
    controls::Grid controls{nullptr};          // the parent of Grid#TransportGrid: progress line and buttons
};

bool Complete(const MiniParts& parts) {
    return parts.view && parts.display && parts.panes && parts.album && parts.titleBar && parts.content &&
           parts.controls;
}

void Take(MiniParts& parts, const xaml::FrameworkElement& element) {
    const winrt::hstring name = element.Name();
    if (name == L"miniPlayerView") {
        parts.view = element;
    } else if (name == L"LCD") {
        if (auto grid = element.try_as<controls::Grid>()) parts.display = grid;  // not MiniNowPlayingControl#LCD
    } else if (name == L"LyricsGrid") {
        parts.panes = element;
    } else if (name == L"AlbumViewContentBackground") {
        parts.album = element;
    } else if (name == L"AppTitleBar") {
        parts.titleBar = element;
    } else if (name == L"MainContent") {
        parts.content = element.try_as<controls::Grid>();
    } else if (name == L"TransportGrid") {
        parts.controls = media::VisualTreeHelper::GetParent(element).try_as<controls::Grid>();
    }
}

MiniParts FindParts(const xaml::UIElement& content) {
    MiniParts parts;
    std::vector<xaml::DependencyObject> queue{content};
    for (std::size_t i = 0; i < queue.size() && queue.size() < kSearchLimit && !Complete(parts); i++) {
        if (auto element = queue[i].try_as<xaml::FrameworkElement>()) Take(parts, element);
        int count = media::VisualTreeHelper::GetChildrenCount(queue[i]);
        for (int child = 0; child < count; child++) queue.push_back(media::VisualTreeHelper::GetChild(queue[i], child));
    }
    return parts;
}

bool AppleShowsCompact(const MiniParts& parts) {
    return Complete(parts) && parts.panes.Visibility() == xaml::Visibility::Collapsed &&
           parts.album.Visibility() == xaml::Visibility::Collapsed;
}

int ClientHeight(HWND hwnd) {
    RECT client{};
    return hwnd && GetClientRect(hwnd, &client) ? client.bottom : 0;
}

// The window's client area at `height` physical pixels, its top left corner where it is.
void SetClientHeight(HWND hwnd, int height) {
    RECT outer{};
    RECT client{};
    if (!hwnd || height <= 0 || !GetWindowRect(hwnd, &outer) || !GetClientRect(hwnd, &client)) return;
    if (client.bottom == height) return;
    const int frame = (outer.bottom - outer.top) - client.bottom;
    SetWindowPos(hwnd, nullptr, 0, 0, outer.right - outer.left, height + frame,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

// Over the song's row and under the raised controls: Apple's in-app acrylic, as see-through as the controls are, so
// the song shows while a theme fades them out and gives way to them under the pointer.
controls::Border MakeVeil(const controls::Grid& row) {
    controls::Border veil;
    veil.Name(L"AMPIFY_MiniVeil");
    veil.IsHitTestVisible(false);
    veil.VerticalAlignment(xaml::VerticalAlignment::Top);
    media::Brush acrylic = ThemeResource(L"AcrylicInAppFillColorDefaultBrush").try_as<media::Brush>();
    veil.Background(acrylic ? acrylic : media::SolidColorBrush(winrt::Windows::UI::Color{230, 32, 32, 32}));
    xaml::Data::Binding opacity;
    opacity.Source(row);
    opacity.Path(xaml::PropertyPath(L"Opacity"));
    veil.SetBinding(xaml::UIElement::OpacityProperty(), opacity);
    if (xaml::ScalarTransition fade = row.OpacityTransition()) {  // the theme's `transition: opacity`, matched
        xaml::ScalarTransition same;
        same.Duration(fade.Duration());
        veil.OpacityTransition(same);
    }
    return veil;
}

// The veil just before the bottom part (whose row of controls is raised over it), across the content's rows.
void PlaceVeil(MiniCompactState& state, const MiniParts& parts, double height) {
    auto bottom = media::VisualTreeHelper::GetParent(parts.controls).try_as<xaml::UIElement>();
    if (!bottom) return;
    if (!state.veil) state.veil = MakeVeil(parts.controls);
    if (state.veil.Parent() != parts.content) {
        RemoveFromParent(state.veil);
        std::uint32_t index = 0;
        if (!parts.content.Children().IndexOf(bottom, index)) index = parts.content.Children().Size();
        parts.content.Children().InsertAt(index, state.veil);
        controls::Grid::SetRowSpan(state.veil, std::max(static_cast<std::int32_t>(parts.content.RowDefinitions().Size()), 1));
    }
    if (state.veil.Height() != height) state.veil.Height(height);
}

void Undo(WindowContext& context, bool giveHeightBack) {
    MiniCompactState& state = context.miniCompact;
    RemoveFromParent(state.veil);
    if (auto row = state.controls.get()) row.Translation(state.controlsTranslation);
    if (auto view = state.view.get(); view && state.viewAlignment) view.VerticalAlignment(*state.viewAlignment);
    if (giveHeightBack) SetClientHeight(WindowHandle(context.window.get()), state.appleClientHeight);
    state = MiniCompactState{};
}

void Begin(MiniCompactState& state, const MiniParts& parts) {
    state.controls = winrt::make_weak(parts.controls);
    state.view = winrt::make_weak(parts.view);
    state.controlsTranslation = parts.controls.Translation();
    // Apple aligns its view to the bottom when the window opens upwards: cut, the song's row would go.
    state.viewAlignment = parts.view.VerticalAlignment();
    parts.view.VerticalAlignment(xaml::VerticalAlignment::Top);
    state.on = true;
}

// Raises the controls so their bottom meets the window's, under the caption band; the window ends there, or under the
// song's row when that is taller. Returns the window's height in layout pixels, 0 while the parts are not laid out.
// Apple refuses a RenderTransform on its row (E_ACCESSDENIED): the raise is a Translation, which layout ignores.
double Raise(MiniCompactState& state, const MiniParts& parts, const xaml::UIElement& content) {
    const double rowHeight = parts.controls.ActualHeight();
    const double songHeight = parts.display.ActualHeight();
    auto parent = media::VisualTreeHelper::GetParent(parts.controls).try_as<xaml::UIElement>();
    if (rowHeight <= 0 || songHeight <= 0 || !parent) return 0;
    state.band = parts.titleBar.ActualHeight() > 0 ? parts.titleBar.ActualHeight() : kDefaultBand;
    const double height = std::max(songHeight, state.band + rowHeight);
    winrt::Windows::Foundation::Point origin{0, 0};
    // Where layout puts the row, Ampify's raise aside: its parent's place and its own offset in it.
    const double top = parent.TransformToVisual(content).TransformPoint(origin).Y + parts.controls.ActualOffset().y;
    const float lift = static_cast<float>(height - rowHeight - top);
    winrt::Windows::Foundation::Numerics::float3 translation = state.controlsTranslation;
    translation.y += lift;
    if (parts.controls.Translation() != translation) parts.controls.Translation(translation);
    return height;
}

}  // namespace

void FollowMiniCompact(UiService::Shared& shared, WindowContext& context, const xaml::UIElement& content) {
    MiniCompactState& state = context.miniCompact;
    const bool wanted = PublishedMiniWindowLayout() == MiniWindowLayout::Compact;
    if (!wanted && !state.on) return;
    const char* step = "finding its parts";
    try {
        MiniParts parts = content ? FindParts(content) : MiniParts{};
        const bool compactView = AppleShowsCompact(parts);
        if (state.on && (!wanted || !compactView || state.controls.get() != parts.controls)) {
            step = "giving it back";
            const int appleHeight = state.appleClientHeight;
            Undo(context, !wanted && compactView);  // the theme let go: Apple's own height comes back
            // Apple rebuilt its row while the window stays cut: the height to give back later is still Apple's.
            if (wanted && compactView) state.appleClientHeight = appleHeight;
        }
        if (!wanted || !compactView) return;
        step = "raising its controls";
        if (!state.on) Begin(state, parts);
        const double height = Raise(state, parts, content);
        if (height <= 0) return;  // laid out at the next refresh
        step = "veiling the song";
        PlaceVeil(state, parts, height);
        step = "cutting its window";
        HWND hwnd = WindowHandle(context.window.get());
        const double scale = content.XamlRoot() ? content.XamlRoot().RasterizationScale() : 1.0;
        const int client = static_cast<int>(std::lround(height * scale));
        if (int now = ClientHeight(hwnd); now > client) state.appleClientHeight = now;  // Apple's, given back later
        SetClientHeight(hwnd, client);
        shared.Once("mini-compact", "Interface: compact mini player, its window cut under the song's row (" +
                                        std::to_string(client) + " px), its controls raised over it");
    } catch (const winrt::hresult_error& error) {
        shared.Once(std::string("mini-compact-failed|") + step, std::string("Interface: compact mini player, error ") +
                                                                    step + " (" + HresultText(error.code().value) + ")");
    }
}

void ReleaseMiniCompact(WindowContext& context) noexcept {
    try {
        if (context.miniCompact.on) Undo(context, false);  // the window is going: its size does not matter
    } catch (...) {
        context.miniCompact = MiniCompactState{};
    }
}

}  // namespace ampify::win::ui_service_detail

#endif  // AMPIFY_WITH_XAML
