// Apple's audio badge (the Lossless mark under the song in the play bar and in the mini player, and the flyout it
// opens) saying what an
// element asks (Ampify.ui.playbar.audioBadge): its words, its screen reader name and, after Apple's own Lossless mark,
// the element's icon. Apple's objects are changed in place and given back as they were: the words, the name, and the
// width of the mark's canvas, from which Ampify's icon is taken out again. Only while Apple shows its Lossless mark: a
// Dolby badge is left alone.

#if defined(AMPIFY_WITH_XAML)

#include "ui_service_detail.hpp"

#include <cmath>

#include <winrt/Microsoft.UI.Xaml.Shapes.h>

namespace ampify::win::ui_service_detail {

namespace {

namespace shapes = xaml::Shapes;

constexpr wchar_t kBadgeClass[] = L"AMP.UI.Views.AudioBadgeControl";
constexpr wchar_t kBadgeName[] = L"AudioBadgeControl";
constexpr wchar_t kButtonName[] = L"AudioBadgeButton";
constexpr wchar_t kLabelName[] = L"AudioBadgeLabel";
constexpr wchar_t kIconName[] = L"AMPIFY_AudioBadgeIcon";  // Ampify's path, beside Apple's in the mark's canvas
constexpr int kSearchLimit = 6000;  // elements walked in a window to find the badge
constexpr int kBadgeLimit = 200;    // the badge and its flyout hold a few dozen
// The element's paths, checked by ParseSvgIcon (path commands and numbers only), go in Data after "F1 ", the path
// language's nonzero fill, the one SVG uses.
constexpr wchar_t kPathOpen[] = L"<Path xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' Data='F1 ";

// One change made to Apple's badge or flyout, and what it held before.
struct Patch {
    enum class Kind { Text, Name, Icon } kind = Kind::Text;
    xaml::DependencyObject target{nullptr};  // Text: a TextBlock; Name: the badge's button; Icon: Ampify's path
    controls::Canvas canvas{nullptr};        // Icon: Apple's mark it was put in
    winrt::hstring apple;                    // Text, Name: Apple's words
    double appleWidth = 0;                   // Icon: the canvas's width before (NaN: sized by its layout)
    std::int64_t token = 0;                  // Text, Name: the callback that keeps the words while Apple writes again
    bool inFlyout = false;                   // made in the flyout, which Apple may build again at each opening
};

// A callback on the visibility of one of Apple's marks: Lossless and Dolby take turns.
struct WatchedMark {
    winrt::weak_ref<controls::Canvas> mark;
    std::int64_t token = 0;
};

}  // namespace

struct AudioBadgeState {
    UiAudioBadge wanted;
    winrt::weak_ref<xaml::FrameworkElement> control;  // Apple's AudioBadgeControl
    winrt::weak_ref<controls::Button> button;
    winrt::weak_ref<primitives::FlyoutBase> flyout;  // the one whose Opened Ampify listens to
    winrt::event_token opened{};
    std::vector<WatchedMark> watched;
    std::vector<Patch> patches;
    winrt::hstring appleWord;  // Apple's name of the badge ("Lossless"), to know its flyout
    const char* refused = nullptr;  // why the icon was not drawn, for the log
    bool told = false;
};

namespace {

using Shared = UiService::Shared;

template <typename Visit>
void Walk(const xaml::DependencyObject& root, int limit, Visit&& visit) {
    std::vector<xaml::DependencyObject> queue{root};
    for (std::size_t i = 0; i < queue.size() && queue.size() < static_cast<std::size_t>(limit); i++) {
        if (!visit(queue[i])) return;
        int count = media::VisualTreeHelper::GetChildrenCount(queue[i]);
        for (int child = 0; child < count; child++) queue.push_back(media::VisualTreeHelper::GetChild(queue[i], child));
    }
}

xaml::FrameworkElement FindBadge(const xaml::DependencyObject& root) {
    xaml::FrameworkElement found{nullptr};
    Walk(root, kSearchLimit, [&](const xaml::DependencyObject& node) {
        auto element = node.try_as<xaml::FrameworkElement>();
        if (element && element.Name() == kBadgeName && winrt::get_class_name(element) == kBadgeClass) found = element;
        return !found;
    });
    return found;
}

controls::Button FindButton(const xaml::DependencyObject& control) {
    controls::Button button{nullptr};
    Walk(control, kBadgeLimit, [&](const xaml::DependencyObject& node) {
        auto found = node.try_as<controls::Button>();
        if (found && found.Name() == kButtonName) button = found;
        return !button;
    });
    return button;
}

bool IsOurs(const xaml::UIElement& element) {
    auto path = element.try_as<shapes::Path>();
    return path && path.Name() == kIconName;
}

// Apple's path in one of its marks: a canvas without a name holding one path of Apple's (and Ampify's icon, once
// added). The Dolby marks are named (DolbyImageCompact). Null for any other canvas.
shapes::Path ApplePathOf(const controls::Canvas& canvas) {
    if (!canvas.Name().empty()) return nullptr;
    shapes::Path apple{nullptr};
    for (const xaml::UIElement& child : canvas.Children()) {
        if (IsOurs(child)) continue;
        auto path = child.try_as<shapes::Path>();
        if (!path || apple) return nullptr;  // another kind of child, or a second path of Apple's
        apple = path;
    }
    return apple;
}

std::vector<controls::Canvas> LosslessMarks(const xaml::DependencyObject& root) {
    std::vector<controls::Canvas> marks;
    Walk(root, kBadgeLimit, [&](const xaml::DependencyObject& node) {
        auto canvas = node.try_as<controls::Canvas>();
        if (canvas && ApplePathOf(canvas)) marks.push_back(canvas);
        return true;
    });
    return marks;
}

bool Shown(const controls::Canvas& mark) {
    return mark.Visibility() == xaml::Visibility::Visible;
}

// The element's SVG as one geometry of XAML, scaled to `height`, its paths filled as SVG fills them (nonzero). Null
// when it has none or the reader refuses it.
media::Geometry IconGeometry(const uirender::SvgIcon& svg, double height) {
    std::wstring text(kPathOpen);
    for (const std::string& data : svg.paths) {
        text += std::wstring(Wide(data));
        text += L' ';
    }
    text += L"'/>";
    shapes::Path path{nullptr};
    try {
        path = xaml::Markup::XamlReader::Load(winrt::hstring(text)).try_as<shapes::Path>();
    } catch (...) {
        return nullptr;
    }
    if (!path) return nullptr;
    media::Geometry geometry{path.Data()};
    path.Data(nullptr);  // a geometry belongs to one path
    media::ScaleTransform transform;
    transform.ScaleX(height / svg.height);
    transform.ScaleY(height / svg.height);
    geometry.Transform(transform);
    return geometry;
}

Patch* PatchOf(AudioBadgeState& state, const xaml::DependencyObject& target, Patch::Kind kind) {
    for (Patch& patch : state.patches) {
        if (patch.kind == kind && patch.target == target) return &patch;
    }
    return nullptr;
}

// Draws the element's icon right after Apple's mark, as tall as it and in its fill, so it takes the badge's colour;
// the canvas grows by the icon's width. Apple's own path is not touched.
void AddIcon(AudioBadgeState& state, const controls::Canvas& canvas, bool inFlyout) {
    for (const xaml::UIElement& child : canvas.Children()) {
        if (IsOurs(child)) return;  // in place already
    }
    uirender::SvgIcon svg = uirender::ParseSvgIcon(state.wanted.icon);
    double height = canvas.Height() > 0 ? canvas.Height() : canvas.ActualHeight();
    double appleWidth = std::isfinite(canvas.Width()) ? canvas.Width() : canvas.ActualWidth();
    media::Geometry geometry = svg.Usable() && svg.height > 0 && height > 0 ? IconGeometry(svg, height) : nullptr;
    if (!geometry) {
        state.refused = height > 0 ? "the icon could not be drawn" : "Apple's mark has no height yet";
        return;
    }
    double width = svg.width * height / svg.height;
    shapes::Path icon;
    icon.Name(kIconName);
    icon.Fill(ApplePathOf(canvas).Fill());
    icon.Stretch(media::Stretch::None);
    icon.Width(width);
    icon.Height(height);
    icon.Data(geometry);
    controls::Canvas::SetLeft(icon, appleWidth);
    Patch patch;
    patch.kind = Patch::Kind::Icon;
    patch.target = icon;
    patch.canvas = canvas;
    patch.appleWidth = canvas.Width();
    patch.inFlyout = inFlyout;
    canvas.Children().Append(icon);
    canvas.Width(appleWidth + width);
    state.patches.push_back(std::move(patch));
}

void Restore(Patch& patch) {
    if (patch.kind == Patch::Kind::Icon) {
        std::uint32_t at = 0;
        if (patch.canvas.Children().IndexOf(patch.target.as<xaml::UIElement>(), at)) patch.canvas.Children().RemoveAt(at);
        patch.canvas.Width(patch.appleWidth);
        return;
    }
    if (patch.kind == Patch::Kind::Text) {
        auto text = patch.target.as<controls::TextBlock>();
        text.UnregisterPropertyChangedCallback(controls::TextBlock::TextProperty(), patch.token);
        text.Text(patch.apple);
        return;
    }
    patch.target.UnregisterPropertyChangedCallback(automation::AutomationProperties::NameProperty(), patch.token);
    automation::AutomationProperties::SetName(patch.target, patch.apple);
}

// Gives back the patches `which` picks. They leave the list before any is restored: Apple can answer a restored word at
// once, and the Apply it calls then finds a list it may change.
template <typename Which>
void RestoreSome(AudioBadgeState& state, Which&& which) noexcept {
    std::vector<Patch> taken;
    auto keep = std::stable_partition(state.patches.begin(), state.patches.end(),
                                      [&](const Patch& patch) { return !which(patch); });
    std::move(keep, state.patches.end(), std::back_inserter(taken));
    state.patches.erase(keep, state.patches.end());
    for (Patch& patch : taken) {
        try {
            Restore(patch);
        } catch (...) {
            // Apple has torn the object down: nothing is left to give back.
        }
    }
}

void RestoreAll(AudioBadgeState& state) noexcept {
    RestoreSome(state, [](const Patch&) { return true; });
}

void Apply(const std::shared_ptr<AudioBadgeState>& state) noexcept;

// Words of Apple's (a text or a screen reader name) that say the element's label, and keep saying it when Apple
// writes them again with the next song: Apple's new words are kept to give back.
void PatchWords(const std::shared_ptr<AudioBadgeState>& state, const xaml::DependencyObject& target, Patch::Kind kind,
                bool inFlyout = false) {
    const winrt::hstring label = Wide(state->wanted.label);
    auto text = target.try_as<controls::TextBlock>();
    auto read = [&] { return kind == Patch::Kind::Text ? text.Text() : automation::AutomationProperties::GetName(target); };
    auto write = [&](const winrt::hstring& words) {
        if (kind == Patch::Kind::Text) text.Text(words);
        else automation::AutomationProperties::SetName(target, words);
    };
    if (Patch* patch = PatchOf(*state, target, kind)) {
        if (read() != label) {
            patch->apple = read();
            write(label);
        }
        return;
    }
    Patch patch;
    patch.kind = kind;
    patch.target = target;
    patch.apple = read();
    patch.inFlyout = inFlyout;
    auto property = kind == Patch::Kind::Text ? controls::TextBlock::TextProperty()
                                              : automation::AutomationProperties::NameProperty();
    std::weak_ptr<AudioBadgeState> weak = state;
    patch.token = target.RegisterPropertyChangedCallback(
        property, [weak](const xaml::DependencyObject&, const xaml::DependencyProperty&) noexcept {
            if (auto held = weak.lock()) Apply(held);
        });
    state->patches.push_back(std::move(patch));
    write(label);  // after the patch is kept: the callback it fires finds it and leaves the words alone
}

// The flyout of a Lossless badge: its title says the label, and the element's icon follows its large mark.
// Recognised by its title, Apple's name of the badge: the Dolby flyout keeps Apple's. What an earlier opening changed
// is given back first: Apple may have built the flyout again.
void PatchFlyout(const std::shared_ptr<AudioBadgeState>& state, const xaml::DependencyObject& content) {
    RestoreSome(*state, [](const Patch& patch) { return patch.inFlyout; });
    if (state->wanted.elementId.empty() || state->appleWord.empty()) return;
    bool lossless = false;
    Walk(content, kBadgeLimit, [&](const xaml::DependencyObject& node) {
        auto text = node.try_as<controls::TextBlock>();
        if (text && text.Text() == state->appleWord) {
            PatchWords(state, text, Patch::Kind::Text, true);
            lossless = true;
        }
        return true;
    });
    if (!lossless || state->wanted.icon.empty()) return;
    for (const controls::Canvas& mark : LosslessMarks(content)) {
        if (!Shown(mark)) continue;
        AddIcon(*state, mark, true);
        return;  // the large mark only, the flyout's first
    }
}

void WatchFlyout(const std::shared_ptr<AudioBadgeState>& state, const controls::Button& button) {
    if (state->opened) return;
    auto flyout = button.Flyout();
    if (!flyout) return;  // looked for again at the next Apply
    std::weak_ptr<AudioBadgeState> weak = state;
    state->opened = flyout.Opened([weak](const IInspectable& sender, const IInspectable&) noexcept {
        try {
            auto held = weak.lock();
            auto shown = sender.try_as<controls::Flyout>();
            if (held && shown && shown.Content()) PatchFlyout(held, shown.Content());
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });
    state->flyout = winrt::make_weak(flyout);
}

// Puts the wanted badge in place, or Apple's back: called on each change of the request and each time Apple writes
// the badge again (its words, or which of its marks shows).
void Apply(const std::shared_ptr<AudioBadgeState>& state) noexcept {
    try {
        auto control = state->control.get();
        auto button = state->button.get();
        if (!control || !button) return;
        std::vector<controls::Canvas> marks = LosslessMarks(control);
        bool lossless = std::any_of(marks.begin(), marks.end(), Shown);
        if (state->wanted.elementId.empty() || !lossless) {
            RestoreAll(*state);
            return;
        }
        WatchFlyout(state, button);
        if (!PatchOf(*state, button, Patch::Kind::Name)) state->appleWord = automation::AutomationProperties::GetName(button);
        PatchWords(state, button, Patch::Kind::Name);
        Walk(control, kBadgeLimit, [&](const xaml::DependencyObject& node) {
            auto text = node.try_as<controls::TextBlock>();
            if (text && text.Name() == kLabelName) PatchWords(state, text, Patch::Kind::Text);
            return true;
        });
        // The flyout's words too, when Apple writes them again while it is open.
        std::vector<xaml::DependencyObject> flyoutTexts;
        for (const Patch& patch : state->patches) {
            if (patch.inFlyout && patch.kind == Patch::Kind::Text) flyoutTexts.push_back(patch.target);
        }
        for (const auto& text : flyoutTexts) PatchWords(state, text, Patch::Kind::Text, true);
        if (!state->wanted.icon.empty()) {
            for (const controls::Canvas& mark : marks) AddIcon(*state, mark, false);
        }
    } catch (...) {
        // Nothing may escape into Apple Music's XAML frame.
    }
}

void Unwatch(AudioBadgeState& state) noexcept {
    try {
        if (auto flyout = state.flyout.get(); flyout && state.opened) flyout.Opened(state.opened);
    } catch (...) {
        // Apple has torn the flyout down.
    }
    for (const WatchedMark& watched : state.watched) {
        try {
            if (auto mark = watched.mark.get()) {
                mark.UnregisterPropertyChangedCallback(xaml::UIElement::VisibilityProperty(), watched.token);
            }
        } catch (...) {
            // Apple has torn the mark down.
        }
    }
    state.opened = {};
    state.flyout = nullptr;
    state.watched.clear();
}

// Finds Apple's badge and its button, and listens to its marks. False, with nothing kept, until both are there.
bool Attach(const std::shared_ptr<AudioBadgeState>& state, const xaml::DependencyObject& root) {
    xaml::FrameworkElement control = root ? FindBadge(root) : nullptr;
    controls::Button button = control ? FindButton(control) : nullptr;
    if (!button) return false;
    state->control = winrt::make_weak(control);
    state->button = winrt::make_weak(button);
    std::weak_ptr<AudioBadgeState> weak = state;
    for (const controls::Canvas& mark : LosslessMarks(control)) {
        std::int64_t token = mark.RegisterPropertyChangedCallback(
            xaml::UIElement::VisibilityProperty(), [weak](const xaml::DependencyObject&, const xaml::DependencyProperty&) noexcept {
                if (auto held = weak.lock()) Apply(held);
            });
        state->watched.push_back(WatchedMark{winrt::make_weak(mark), token});
    }
    return true;
}

}  // namespace

void FollowAudioBadge(Shared& shared, const std::shared_ptr<WindowContext>& context) {
    // The play bar's badge, and the one under the song in the mini player, on that window's own thread.
    if (context->role != WindowRole::Main && context->role != WindowRole::Mini) return;
    const std::string where = context->role == WindowRole::Mini ? "the mini player's" : "the play bar's";
    UiAudioBadge wanted;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        wanted = shared.audioBadge;
    }
    if (wanted.elementId.empty() && !context->audioBadge) return;
    if (!context->audioBadge) context->audioBadge = std::make_shared<AudioBadgeState>();
    auto state = context->audioBadge;
    bool changed = wanted.label != state->wanted.label || wanted.icon != state->wanted.icon ||
                   wanted.elementId != state->wanted.elementId;
    if (changed) RestoreAll(*state);  // a new icon or label starts again from Apple's badge
    state->wanted = wanted;
    auto control = state->control.get();
    if (!control || !control.IsLoaded() || !state->button.get()) {
        Unwatch(*state);
        RestoreAll(*state);
        state->control = nullptr;
        state->button = nullptr;
        if (!Attach(state, context->root.get())) {
            if (!wanted.elementId.empty()) shared.Once("badge-missing|" + where, "Interface: " + where + " audio badge was not found");
            return;
        }
    }
    Apply(state);
    if (state->refused) {
        shared.Once("badge-icon|" + where,
                    "Interface: " + where + " audio badge's icon was not drawn: " + std::string(state->refused));
        state->refused = nullptr;
    }
    if (!wanted.elementId.empty() && !state->told) {
        shared.journal.Write("Interface: " + where + " audio badge says \"" + wanted.label + "\" for " + wanted.elementId);
        state->told = true;
    }
}

void ReleaseAudioBadge(WindowContext& context) noexcept {
    if (!context.audioBadge) return;
    Unwatch(*context.audioBadge);
    RestoreAll(*context.audioBadge);
    context.audioBadge.reset();
}

}  // namespace ampify::win::ui_service_detail

#endif  // AMPIFY_WITH_XAML
