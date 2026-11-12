#include "sidebar_hover_state.hpp"

#if defined(AMPIFY_WITH_XAML)

#include "xaml_chrome.hpp"

#include "../core/i18n.hpp"

namespace ampify::win::sidebar_detail {

constexpr int kTurnMs = 180;           // the pin's glyph turns in this time
constexpr wchar_t kPinName[] = L"AMPIFY_SidebarPin";
constexpr double kPinSize = 28;        // the height of Apple's "···" beside it, square
constexpr double kPinGlyphSize = 16;   // the size of Apple's own icons
constexpr double kMoreWidth = 32;      // Apple's "···" before its first layout
constexpr double kHeaderSpacing = 2;   // as between "···" and Ampify's entry in the header

// A quarter turn into place while the element fades in: the pin's glyph, as it changes.
animation::Storyboard EaseTurn(const media::CompositeTransform& turn, const xaml::UIElement& element) {
    animation::Storyboard story;
    animation::CubicEase ease;
    ease.EasingMode(animation::EasingMode::EaseOut);
    auto duration = xaml::DurationHelper::FromTimeSpan(
        std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(std::chrono::milliseconds(kTurnMs)));
    animation::DoubleAnimation spin;
    spin.From(-90.0);
    spin.To(0.0);
    spin.Duration(duration);
    spin.EasingFunction(ease);
    animation::Storyboard::SetTarget(spin, turn);
    animation::Storyboard::SetTargetProperty(spin, L"Rotation");
    animation::DoubleAnimation fade;
    fade.From(0.0);
    fade.To(1.0);
    fade.Duration(duration);
    fade.EasingFunction(ease);
    animation::Storyboard::SetTarget(fade, element);
    animation::Storyboard::SetTargetProperty(fade, L"Opacity");
    story.Children().Append(spin);
    story.Children().Append(fade);
    return story;
}

// Apple's "···" in its header grid (MenuButton#MoreButton), which the pin stands beside.
xaml::FrameworkElement FindMore(const controls::Grid& header) {
    for (const xaml::UIElement& child : header.Children()) {
        auto element = child.try_as<xaml::FrameworkElement>();
        if (element && element.Name() == L"MoreButton") return element;
    }
    return nullptr;
}

// Just left of "···", in its cell and level with it. Read again whenever "···" moves: a theme's @media writes its
// margin as the pane is pinned or the window changes size.
void PlacePin(const State& state) {
    auto more = state.more.get();
    if (!state.pin || !more) return;
    xaml::Thickness margin = more.Margin();
    double width = more.ActualWidth() > 0 ? more.ActualWidth() : kMoreWidth;
    xaml::Thickness wanted{0, margin.Top, ampify::LeftOfMoreMargin(margin.Right, width, kHeaderSpacing), margin.Bottom};
    if (state.pin.Margin() != wanted) state.pin.Margin(wanted);
    controls::Grid::SetRow(state.pin, controls::Grid::GetRow(more));
    controls::Grid::SetColumn(state.pin, controls::Grid::GetColumn(more));
    state.pin.VerticalAlignment(more.VerticalAlignment());
}

// A square flat button with Fluent's subtle hover, the corners of Apple's "···" (4, well under half its height).
controls::Button MakePinButton() {
    const std::wstring text =
        L"<Button xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' "
        L"xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml' HorizontalAlignment='Right' "
        L"VerticalAlignment='Center' MinWidth='0' MinHeight='0' Padding='0' CornerRadius='4' BorderThickness='0'>" +
        SubtleButtonResources() + L"</Button>";
    controls::Button button = xaml::Markup::XamlReader::Load(text).as<controls::Button>();
    button.Name(kPinName);
    button.Width(kPinSize);
    button.Height(kPinSize);
    return button;
}

controls::FontIcon MakePinGlyph(media::CompositeTransform& turn) {
    controls::FontIcon glyph;
    glyph.FontFamily(media::FontFamily(L"Segoe Fluent Icons, Segoe MDL2 Assets"));
    glyph.FontSize(kPinGlyphSize);
    glyph.HorizontalAlignment(xaml::HorizontalAlignment::Center);
    glyph.VerticalAlignment(xaml::VerticalAlignment::Center);
    turn = media::CompositeTransform();
    glyph.RenderTransformOrigin(winrt::Windows::Foundation::Point{0.5f, 0.5f});
    glyph.RenderTransform(turn);
    return glyph;
}

// The pin shows only while the pane is open, with the glyph and the words of the pane's state, in the colour of "···".
// A change of state turns the glyph once, unless the system animations are off.
void ShowPin(State& state, bool animate) {
    auto navigation = state.navigation.get();
    if (!state.pin || !navigation) return;
    const ampify::PinFace face = ampify::PinButtonFace(state.pinned);
    const winrt::hstring label = winrt::to_hstring(i18n::Text(face.label));
    if (state.pinGlyph.Glyph() != face.glyph) state.pinGlyph.Glyph(face.glyph);
    xaml::Automation::AutomationProperties::SetName(state.pin, label);
    controls::ToolTipService::SetToolTip(state.pin, winrt::box_value(label));
    auto more = state.more.get();
    if (auto control = more ? more.try_as<controls::Control>() : nullptr) state.pinGlyph.Foreground(control.Foreground());
    state.pin.Visibility(navigation.IsPaneOpen() ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
    PlacePin(state);
    if (!animate || !MotionAllowed()) return;
    if (state.glyphStory) state.glyphStory.Stop();
    state.glyphStory = EaseTurn(state.pinTurn, state.pinGlyph);
    state.glyphStory.Begin();
}

// The pin taken out of Apple's header, and the watch on "···" taken off.
void RemovePin(State& state) {
    if (state.glyphStory) state.glyphStory.Stop();
    state.glyphStory = nullptr;
    if (auto more = state.more.get(); more && state.moreToken != 0) {
        more.UnregisterPropertyChangedCallback(xaml::FrameworkElement::MarginProperty(), state.moreToken);
        if (state.moreSizeToken.value != 0) more.SizeChanged(state.moreSizeToken);
    }
    state.moreSizeToken = {};
    uint32_t index = 0;
    if (state.pinHeader && state.pin && state.pinHeader.Children().IndexOf(state.pin, index)) {
        state.pinHeader.Children().RemoveAt(index);
    }
    state.moreToken = 0;
    state.more = nullptr;
    state.pin = nullptr;
    state.pinGlyph = nullptr;
    state.pinTurn = nullptr;
    state.pinHeader = nullptr;
}

// The light-dismiss layer of the sidebar's SplitView, searched below it but never inside another SplitView (the pages').
shapes::Rectangle FindOwnDismissLayer(const xaml::DependencyObject& root, int depth) {
    if (!root || depth > kSearchDepth) return nullptr;
    int count = media::VisualTreeHelper::GetChildrenCount(root);
    for (int i = 0; i < count; i++) {
        xaml::DependencyObject child = media::VisualTreeHelper::GetChild(root, i);
        if (auto layer = child.try_as<shapes::Rectangle>(); layer && layer.Name() == L"LightDismissLayer") return layer;
        if (child.try_as<controls::SplitView>()) continue;
        if (auto deeper = FindOwnDismissLayer(child, depth + 1)) return deeper;
    }
    return nullptr;
}

shapes::Rectangle FindDismissLayer(const controls::NavigationView& navigation) {
    auto split = FindFirst<controls::SplitView>(navigation, L"RootSplitView", 0);
    return split ? FindOwnDismissLayer(split, 0) : nullptr;
}

void RestoreDismissLayer(State& state) {
    if (!state.dismiss) return;
    PutBackLocal(state.dismiss, xaml::UIElement::IsHitTestVisibleProperty(), state.dismissHit);
    PutBackLocal(state.dismiss, shapes::Shape::FillProperty(), state.dismissFill);
    state.dismiss = nullptr;
    state.dismissHit = nullptr;
    state.dismissFill = nullptr;
}

// While the pane is folded, the layer must not take the first click on the pages, and its veil stays clear.
void SetDismissLayer(State& state, const controls::NavigationView& navigation, bool hover) {
    if (!hover) {
        RestoreDismissLayer(state);
        return;
    }
    if (!state.dismiss) {
        state.dismiss = FindDismissLayer(navigation);
        if (!state.dismiss) return;
        state.dismissHit = state.dismiss.ReadLocalValue(xaml::UIElement::IsHitTestVisibleProperty());
        state.dismissFill = state.dismiss.ReadLocalValue(shapes::Shape::FillProperty());
    }
    state.dismiss.IsHitTestVisible(false);
    state.dismiss.Fill(media::SolidColorBrush(winrt::Windows::UI::Colors::Transparent()));
}

namespace {

// The DisplayModeStates group of the sidebar SplitView's template, where its open and close transitions are.
xaml::VisualStateGroup FindModeGroup(const controls::NavigationView& navigation) {
    auto split = FindFirst<controls::SplitView>(navigation, L"RootSplitView", 0);
    if (!split || media::VisualTreeHelper::GetChildrenCount(split) == 0) return nullptr;
    auto root = media::VisualTreeHelper::GetChild(split, 0).try_as<xaml::FrameworkElement>();
    if (!root) return nullptr;
    for (const xaml::VisualStateGroup& group : xaml::VisualStateManager::GetVisualStateGroups(root)) {
        if (group.Name() == L"DisplayModeStates") return group;
    }
    return nullptr;
}

}  // namespace

// Pinned over narrow pages, WinUI closes the pane after a click on an item and Ampify opens it again in the same tick
// (WatchPaneOpen): with the SplitView's transitions, that frame still drew the squashed column, then the pane slid
// open again. Without them the close and the reopen land on the same frame. Given back once unpinned or pushing.
void HoldPaneStill(State& state, const controls::NavigationView& navigation, bool still) {
    if (!still) {
        if (state.modeGroup) {
            for (const xaml::VisualTransition& transition : state.transitions) state.modeGroup.Transitions().Append(transition);
        }
        state.modeGroup = nullptr;
        state.transitions.clear();
        return;
    }
    if (state.modeGroup || !navigation) return;
    xaml::VisualStateGroup group = FindModeGroup(navigation);
    if (!group) return;
    for (const xaml::VisualTransition& transition : group.Transitions()) state.transitions.push_back(transition);
    group.Transitions().Clear();
    state.modeGroup = group;
}

namespace {

using winrt::Windows::Foundation::TimeSpan;

bool StartsWith(const winrt::hstring& text, std::wstring_view start) {
    return std::wstring_view(text).substr(0, start.size()) == start;
}

// When the last of a transition's key frames ends.
TimeSpan Length(const xaml::VisualTransition& transition) {
    TimeSpan end{0};
    animation::Storyboard story = transition.Storyboard();
    if (!story) return end;
    auto later = [&](TimeSpan at) { end = at > end ? at : end; };
    for (const animation::Timeline& timeline : story.Children()) {
        if (auto frames = timeline.try_as<animation::DoubleAnimationUsingKeyFrames>()) {
            for (const animation::DoubleKeyFrame& frame : frames.KeyFrames()) later(frame.KeyTime().TimeSpan);
        } else if (auto objects = timeline.try_as<animation::ObjectAnimationUsingKeyFrames>()) {
            for (const animation::ObjectKeyFrame& frame : objects.KeyFrames()) later(frame.KeyTime().TimeSpan);
        }
    }
    return end;
}

// Where the SplitView's own fold out of `open` leaves the clip of the pane: its last key frame on
// PaneClipRectangleTransform (left of the column, or right of it for a pane on the right).
std::optional<double> FoldedClip(const xaml::VisualStateGroup& group, const winrt::hstring& open) {
    for (const xaml::VisualTransition& transition : group.Transitions()) {
        if (transition.From() != open || !StartsWith(transition.To(), L"Closed") || !transition.Storyboard()) continue;
        for (const animation::Timeline& timeline : transition.Storyboard().Children()) {
            auto frames = timeline.try_as<animation::DoubleAnimationUsingKeyFrames>();
            if (!frames || frames.KeyFrames().Size() == 0) continue;
            if (animation::Storyboard::GetTargetName(timeline) != L"PaneClipRectangleTransform") continue;
            return frames.KeyFrames().GetAt(frames.KeyFrames().Size() - 1).Value();
        }
    }
    return std::nullopt;
}

// How long the pane takes to open into `open`: the longest transition into it.
TimeSpan OpeningLength(const xaml::VisualStateGroup& group, const winrt::hstring& open) {
    TimeSpan longest{0};
    for (const xaml::VisualTransition& transition : group.Transitions()) {
        if (transition.To() != open || !StartsWith(transition.From(), L"Closed")) continue;
        TimeSpan length = Length(transition);
        longest = length > longest ? length : longest;
    }
    if (longest.count() > 0) return longest;
    return std::chrono::duration_cast<TimeSpan>(ampify::kFoldSlideFallback);
}

}  // namespace

// The pane folding under the pointer slides shut, still open, then folds. WinUI's fold could not show it: Apple Music
// narrows its column to the folded width the moment the pane closes, from a state of its own, so the SplitView's fold
// slid its clip over an empty pane (seen frame by frame on 2026-11-10). Ampify moves that same clip, the SplitView's
// PaneClipRectangleTransform, to where the fold leaves it, in the opening's time and along kFoldEase; `shut` then folds
// the pane at once (HoldPaneStill), its clip already in place. Works for a pane on either side, under any theme. False
// when the pane is not in an open overlay state or its clip is not found: the caller folds it as before.
bool SlideShut(State& state, const controls::NavigationView& navigation, std::function<void()> shut) {
    if (Sliding(state)) return true;
    if (state.modeGroup) return false;  // pinned over narrow pages: the pane holds still
    xaml::VisualStateGroup group = FindModeGroup(navigation);
    if (!group || !group.CurrentState()) return false;
    const winrt::hstring open = group.CurrentState().Name();
    if (!StartsWith(open, L"Open")) return false;
    auto split = FindFirst<controls::SplitView>(navigation, L"RootSplitView", 0);
    auto root = split && media::VisualTreeHelper::GetChildrenCount(split) > 0
                    ? media::VisualTreeHelper::GetChild(split, 0).try_as<xaml::FrameworkElement>()
                    : nullptr;
    auto clip = root ? root.FindName(L"PaneClipRectangleTransform").try_as<xaml::DependencyObject>() : nullptr;
    std::optional<double> to = FoldedClip(group, open);
    if (!clip || !to) return false;
    const wchar_t* property = nullptr;
    double from = 0;
    if (auto translate = clip.try_as<media::TranslateTransform>()) {
        property = L"X";
        from = translate.X();
    } else if (auto composite = clip.try_as<media::CompositeTransform>()) {
        property = L"TranslateX";
        from = composite.TranslateX();
    } else {
        return false;
    }
    animation::DoubleAnimationUsingKeyFrames move;
    animation::DiscreteDoubleKeyFrame start;
    start.KeyTime(animation::KeyTimeHelper::FromTimeSpan(TimeSpan{0}));
    start.Value(from);
    animation::SplineDoubleKeyFrame end;
    end.KeyTime(animation::KeyTimeHelper::FromTimeSpan(OpeningLength(group, open)));
    end.Value(*to);
    animation::KeySpline ease;
    ease.ControlPoint1({static_cast<float>(ampify::kFoldEase.x1), static_cast<float>(ampify::kFoldEase.y1)});
    ease.ControlPoint2({static_cast<float>(ampify::kFoldEase.x2), static_cast<float>(ampify::kFoldEase.y2)});
    end.KeySpline(ease);
    move.KeyFrames().Append(start);
    move.KeyFrames().Append(end);
    animation::Storyboard::SetTarget(move, clip);
    animation::Storyboard::SetTargetProperty(move, property);
    animation::Storyboard story;
    story.Children().Append(move);
    story.Completed([shut = std::move(shut), weak = winrt::make_weak(story)](IInspectable const&, IInspectable const&) noexcept {
        try {
            auto done = weak.get();
            if (!done || TheState().slide != done) return;  // stopped, or another slide began
            shut();
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });
    state.slide = story;
    story.Begin();
    return true;
}

bool Sliding(const State& state) {
    return state.slide != nullptr;
}

// The slide taken off: the clip goes back to what the pane's state gives it, open (the pointer came back) or folded.
void StopSlide(State& state) {
    if (!state.slide) return;
    animation::Storyboard story = state.slide;
    state.slide = nullptr;
    try {
        story.Stop();
    } catch (...) {
        // The template may be torn down already: nothing is left to stop.
    }
}

// A brush that shows: a theme's acrylic or image, or a colour that is not see-through.
bool Paints(const media::Brush& brush) {
    if (!brush || brush.Opacity() <= 0) return false;
    auto solid = brush.try_as<media::SolidColorBrush>();
    return !solid || solid.Color().A > 0;
}

constexpr const wchar_t* kPaneAcrylicKeys[] = {L"NavigationViewExpandedPaneBackground",
                                               L"NavigationViewDefaultPaneBackground"};

// Under Ampify's fill, Apple's in-app acrylic on Grid#PaneRoot is never wanted: as the pane folded, its rows went
// first and the acrylic stayed a frame or two as wide as the open pane, a grey veil over the pages. Its keys in the
// NavigationView's own resources go see-through while the fill is there.
void ClearPaneAcrylic(State& state, const controls::NavigationView& navigation) {
    if (state.acrylicKeysOn || !navigation) return;
    auto resources = navigation.Resources();
    for (const wchar_t* name : kPaneAcrylicKeys) {
        resources.Insert(winrt::box_value(name), media::SolidColorBrush(winrt::Windows::UI::Colors::Transparent()));
    }
    state.acrylicKeysOn = navigation;
}

void RestorePaneAcrylic(State& state) {
    auto navigation = state.acrylicKeysOn.try_as<controls::NavigationView>();
    state.acrylicKeysOn = nullptr;
    if (!navigation) return;
    try {
        auto resources = navigation.Resources();
        for (const wchar_t* name : kPaneAcrylicKeys) {
            if (auto key = winrt::box_value(name); resources.HasKey(key)) resources.Remove(key);
        }
    } catch (...) {
    }
}

void RemoveFill(State& state) {
    RestorePaneAcrylic(state);
    uint32_t index = 0;
    if (state.fillGrid && state.fill && state.fillGrid.Children().IndexOf(state.fill, index)) {
        state.fillGrid.Children().RemoveAt(index);
    }
    state.fillGrid = nullptr;
    state.fill = nullptr;
}

// Apple's column of the pane (Grid#PaneContentGrid), searched once: a weak reference to Apple's own object resolves.
controls::Grid Column(State& state, const controls::NavigationView& navigation) {
    if (auto known = state.column.get()) return known;
    auto grid = FindFirst<controls::Grid>(navigation, L"PaneContentGrid", 0);
    if (grid) state.column = winrt::make_weak(grid);
    return grid;
}

// Apple's own shadow under the open pane shows: Grid#ShadowCaster, unless a theme collapsed it (floating-panels).
bool AppleShadowShows(State& state, const controls::NavigationView& navigation) {
    auto caster = state.caster.get();
    if (!caster) {
        caster = FindFirst<controls::Grid>(navigation, L"ShadowCaster", 0);
        if (!caster) return false;
        state.caster = winrt::make_weak(caster);
    }
    return caster.Visibility() == xaml::Visibility::Visible;
}

// Apple's shadow grid goes while Ampify folds and opens the pane: HoldPaneStill takes out the transitions that brought
// its opacity and width back, and once the pane folded it stayed as wide as the open pane, an empty frame of shadow over
// the pages. EdgeOverPages draws the open pane's edge instead.
void HideAppleShadow(State& state, const controls::NavigationView& navigation) {
    if (state.casterHeld) return;
    auto caster = FindFirst<controls::Grid>(navigation, L"ShadowCaster", 0);
    if (!caster) return;
    state.casterHeld = caster;
    state.casterVisibility = caster.ReadLocalValue(xaml::UIElement::VisibilityProperty());
    caster.Visibility(xaml::Visibility::Collapsed);
}

void RestoreAppleShadow(State& state) {
    if (!state.casterHeld) return;
    PutBackLocal(state.casterHeld, xaml::UIElement::VisibilityProperty(), state.casterVisibility);
    state.casterHeld = nullptr;
    state.casterVisibility = nullptr;
}

// Apple's own brush for its pane (NavigationView's resources): the in-app acrylic open, which is lighter than what the
// folded column shows. A theme's brush is a brush of its own, never one of these.
bool ApplesPaneBrush(const media::Brush& brush) {
    if (!brush) return false;
    auto resources = xaml::Application::Current().Resources();
    for (const wchar_t* name : {L"NavigationViewExpandedPaneBackground", L"NavigationViewDefaultPaneBackground"}) {
        auto key = winrt::box_value(name);
        if (resources.HasKey(key) && resources.Lookup(key).try_as<media::Brush>() == brush) return true;
    }
    return false;
}

// The theme already draws an edge round this grid (Glass's rim, High contrast's line).
bool HasOwnEdge(const controls::Grid& grid) {
    const xaml::Thickness sides = grid.BorderThickness();
    return (sides.Left > 0 || sides.Top > 0 || sides.Right > 0 || sides.Bottom > 0) && Paints(grid.BorderBrush());
}

void RemoveEdge(State& state) {
    uint32_t index = 0;
    if (state.edgeHost && state.edge && state.edgeHost.Children().IndexOf(state.edge, index)) {
        state.edgeHost.Children().RemoveAt(index);
    }
    state.edgeHost = nullptr;
    state.edge = nullptr;
}

// Ours, on top of everything in the grid and over all its rows: a hairline in Windows' own surface outline, which
// follows the app's mode through its theme resource.
controls::Border MakeEdge(const controls::Grid& grid) {
    const wchar_t* text =
        L"<Border xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' BorderThickness='1' "
        L"BorderBrush='{ThemeResource SurfaceStrokeColorDefaultBrush}' IsHitTestVisible='False'/>";
    controls::Border edge{nullptr};
    try {
        edge = xaml::Markup::XamlReader::Load(text).as<controls::Border>();
    } catch (...) {
        return nullptr;  // a WinUI without that resource: the pane keeps no edge rather than a wrong one
    }
    controls::Grid::SetRowSpan(edge, std::max<int32_t>(1, static_cast<int32_t>(grid.RowDefinitions().Size())));
    controls::Grid::SetColumnSpan(edge, std::max<int32_t>(1, static_cast<int32_t>(grid.ColumnDefinitions().Size())));
    grid.Children().Append(edge);
    return edge;
}

// Over the pages, Apple shadows its pane from a square rectangle at the window's edge, which floating-panels hides: a
// pane of the page's own colour (Ember, Cinema) then had no edge at all. Where nothing else draws one, a hairline
// follows the corners of the grid that paints the pane: its column, or the pane itself when only the pane is painted.
void EdgeOverPages(State& state, const controls::NavigationView& navigation, bool over) {
    auto pane = state.pane.get().try_as<controls::Grid>();
    auto column = Column(state, navigation);
    if (!pane || !column) return;
    const bool paneOnly = Paints(pane.Background()) && !Paints(column.Background()) && !state.fill;
    const controls::Grid host = paneOnly ? pane : column;
    const bool shown = over && !AppleShadowShows(state, navigation) && !HasOwnEdge(host);
    if (state.edge && state.edgeHost != host) RemoveEdge(state);
    if (!state.edge && shown) {
        state.edge = MakeEdge(host);
        state.edgeHost = host;
    }
    if (!state.edge) return;
    if (state.edge.CornerRadius() != host.CornerRadius()) state.edge.CornerRadius(host.CornerRadius());
    state.edge.Visibility(shown ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
}

// Ours, behind everything in Apple's pane grid and over all its rows: the plain base of Windows' window backdrop, the
// colour the folded or pinned pane shows (#202020 in dark mode). An in-app acrylic took on the tint of the pages and
// artwork under it, so the pane changed colour as it opened. The theme resource follows the app's light or dark mode.
controls::Border MakeFill(const controls::Grid& grid) {
    const wchar_t* text =
        L"<Border xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' IsHitTestVisible='False' "
        L"Background='{ThemeResource SolidBackgroundFillColorBaseBrush}'/>";
    controls::Border fill{nullptr};
    try {
        fill = xaml::Markup::XamlReader::Load(text).as<controls::Border>();
    } catch (...) {
        return nullptr;  // a WinUI without that resource: the pane stays as Apple leaves it
    }
    controls::Grid::SetRowSpan(fill, std::max<int32_t>(1, static_cast<int32_t>(grid.RowDefinitions().Size())));
    controls::Grid::SetColumnSpan(fill, std::max<int32_t>(1, static_cast<int32_t>(grid.ColumnDefinitions().Size())));
    grid.Children().InsertAt(0, fill);
    return fill;
}

// Apple leaves its pane see-through over its own window backdrop, and so does a theme that sets the pane on the
// window's material (Fluent): open over the pages, they showed through its search box and menu, and the pin sat on the
// Play button. While nothing paints it, a layer of Ampify's own fills it, folded, open and pinned alike: folded and
// pinned, the window's Mica showed through with the wallpaper's tint, so the pane changed colour as it opened. A
// theme's background on Apple's grids would be written again by the theme.
void FillPane(State& state, const controls::NavigationView& navigation) {
    auto pane = state.pane.get();
    auto grid = state.fillGrid ? state.fillGrid : Column(state, navigation);
    if (!pane || !grid) return;
    auto panel = pane.try_as<controls::Panel>();
    // Only a brush the pages cannot show through: Fluent's layer colour is 30 % opaque, its entries were unreadable
    // over the artwork of the page under them. An acrylic or an image covers by itself.
    auto covers = [](const media::Brush& brush) {
        auto solid = brush.try_as<media::SolidColorBrush>();
        return !solid || (solid.Color().A == 255 && brush.Opacity() >= 1);
    };
    auto themes = [&](const media::Brush& brush) {
        return Paints(brush) && covers(brush) && !ApplesPaneBrush(brush);
    };
    const bool painted = themes(grid.Background()) || (panel && themes(panel.Background()));
    if (!state.fill && !painted) {
        state.fill = MakeFill(grid);
        state.fillGrid = state.fill ? grid : nullptr;
    }
    if (!state.fill) return;
    state.fill.CornerRadius(grid.CornerRadius());
    state.fill.Visibility(painted ? xaml::Visibility::Collapsed : xaml::Visibility::Visible);
    if (painted) {
        RestorePaneAcrylic(state);
    } else {
        ClearPaneAcrylic(state, navigation);
    }
}

// A group has children: realized ones, a MenuItems list, or an items source that the list fills in later.
bool HasChildren(const Group& item) {
    return item.HasUnrealizedChildren() || item.MenuItems().Size() > 0 || item.MenuItemsSource() != nullptr;
}

// Apple's groups: the items of its own sidebar list that have children (Library, Playlists), with whether each is open.
std::vector<std::pair<Group, bool>> SidebarGroups(const controls::NavigationView& navigation) {
    std::vector<std::pair<Group, bool>> groups;
    auto host = FindFirst<controls::ItemsRepeater>(navigation, L"MenuItemsHost", 0);
    int count = host ? media::VisualTreeHelper::GetChildrenCount(host) : 0;
    for (int i = 0; i < count; i++) {
        auto item = media::VisualTreeHelper::GetChild(host, i).try_as<Group>();
        if (item && HasChildren(item)) groups.emplace_back(item, item.IsExpanded());
    }
    return groups;
}

// Folded: the open groups collapse, and the ones collapsed here are remembered, so the same ones open again.
void FoldGroups(State& state, const controls::NavigationView& navigation) {
    for (const Group& group : state.groups.Fold(SidebarGroups(navigation))) {
        try {
            group.IsExpanded(false);
        } catch (...) {
        }
    }
}

void UnfoldGroups(State& state) {
    for (const Group& group : state.groups.Unfold()) {
        try {
            group.IsExpanded(true);
        } catch (...) {
        }
    }
}

void SyncGroups(State& state, const controls::NavigationView& navigation) {
    if (navigation.IsPaneOpen()) {
        UnfoldGroups(state);
    } else {
        FoldGroups(state, navigation);
    }
}

// The pin button in Apple's header grid (the logo, "Apple Music" and "···"), beside "···". Found again when Apple
// rebuilt its header.
void AddPin(State& state, const controls::NavigationView& navigation, const xaml::RoutedEventHandler& onClick) {
    auto header = navigation.PaneHeader().try_as<controls::Grid>();
    xaml::FrameworkElement more = header ? FindMore(header) : nullptr;
    if (!more) return;
    state.pin = MakePinButton();
    state.pinGlyph = MakePinGlyph(state.pinTurn);
    state.pin.Content(state.pinGlyph);
    state.pin.Click(onClick);
    uint32_t index = 0;
    header.Children().IndexOf(more, index);
    header.Children().InsertAt(index, state.pin);  // before "···", so the Tab key meets them in the order they show
    state.pinHeader = header;
    state.more = winrt::make_weak(more);
    state.moreToken = more.RegisterPropertyChangedCallback(
        xaml::FrameworkElement::MarginProperty(), [](xaml::DependencyObject const&, xaml::DependencyProperty const&) noexcept {
            try {
                PlacePin(TheState());
            } catch (...) {
            }
        });
    // Its width too: a theme that sized "···" (Mirror) gives it back after its margin, at the next layout. Read with the
    // margin alone, the first open after that theme left the pin 46 pixels short of "···" (seen at 125%).
    state.moreSizeToken = more.SizeChanged([](IInspectable const&, xaml::SizeChangedEventArgs const&) noexcept {
        try {
            PlacePin(TheState());
        } catch (...) {
        }
    });
    ShowPin(state, false);
}

}  // namespace ampify::win::sidebar_detail

#endif  // AMPIFY_WITH_XAML
