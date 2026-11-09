// Transitions and transforms: the implicit animations of a value, and the transforms drawn by a storyboard.

#if defined(AMPIFY_WITH_XAML)

#include "xaml_backend.hpp"
#include "xaml_properties_detail.hpp"

namespace ampify::win {

using namespace xaml_detail;

namespace {

bool DrawsBackground(const xaml::DependencyObject& object) {
    return object.try_as<controls::Panel>() || object.try_as<controls::Border>() ||
           object.try_as<controls::ContentPresenter>();
}

// What draws an element's background: the element, or for a control the root of its template, whose background its
// states change. A menu entry's background (MenuFlyoutItem, a control) changed in one frame under its transition.
xaml::DependencyObject BackgroundHost(const xaml::DependencyObject& object) {
    if (DrawsBackground(object)) return object;
    if (!object.try_as<controls::Control>() || xaml::Media::VisualTreeHelper::GetChildrenCount(object) == 0) return nullptr;
    xaml::DependencyObject root = xaml::Media::VisualTreeHelper::GetChild(object, 0);
    return DrawsBackground(root) ? root : nullptr;
}

xaml::BrushTransition BackgroundTransitionOf(const xaml::DependencyObject& host) {
    if (auto panel = host.try_as<controls::Panel>()) return panel.BackgroundTransition();
    if (auto border = host.try_as<controls::Border>()) return border.BackgroundTransition();
    if (auto presenter = host.try_as<controls::ContentPresenter>()) return presenter.BackgroundTransition();
    return nullptr;
}

// A brush eases only from another brush: a host with no background (a row of Morceaux) took its light in one frame.
// It starts from a transparent one.
void SetBackgroundTransition(const xaml::DependencyObject& host, const xaml::BrushTransition& transition) {
    // Black, not Transparent's white: eased to a dark colour, a transparent white went through a light grey first.
    const xaml::Media::SolidColorBrush clear(winrt::Windows::UI::Color{0, 0, 0, 0});
    if (auto panel = host.try_as<controls::Panel>()) {
        panel.BackgroundTransition(transition);
        if (transition && !panel.Background()) panel.Background(clear);
    } else if (auto border = host.try_as<controls::Border>()) {
        border.BackgroundTransition(transition);
        if (transition && !border.Background()) border.Background(clear);
    } else if (auto presenter = host.try_as<controls::ContentPresenter>()) {
        presenter.BackgroundTransition(transition);
        if (transition && !presenter.Background()) presenter.Background(clear);
    }
}

}  // namespace

bool XamlTreeBackend::WriteTransition(theme::NodeId id, const xaml::DependencyObject& object, const theme::Value& value) {
    auto element = object.try_as<xaml::UIElement>();
    if (!element) return false;
    // "transform:200;opacity:150;background:300": duration in ms by property.
    std::map<std::string, double, std::less<>> durations;
    for (const std::string& item : Split(value.text, ';')) {
        size_t colon = item.find(':');
        if (colon != std::string::npos) durations[item.substr(0, colon)] = std::atof(item.c_str() + colon + 1);
    }
    // `width` glides the widths Apple writes (the smooth progress line), on its own, on any element with a width.
    if (auto sized = object.try_as<xaml::FrameworkElement>()) {
        auto width = durations.find("width");
        m_widthGlides.Write(id, sized, width == durations.end() ? 0 : width->second);
    }
    xaml::DependencyObject backgroundHost = BackgroundHost(object);
    try {
        // The transitions Apple Music set come back when the theme stops animating this element.
        auto [originals, first] = m_transitionOriginals.try_emplace(id);
        if (first) {
            originals->second.opacity = element.OpacityTransition();
            originals->second.scale = element.ScaleTransition();
            originals->second.rotation = element.RotationTransition();
            originals->second.translation = element.TranslationTransition();
            if (backgroundHost) originals->second.background = BackgroundTransitionOf(backgroundHost);
        }
        const TransitionOriginals& before = originals->second;

        xaml::ScalarTransition opacity = before.opacity;
        if (auto ms = durations.find("opacity"); ms != durations.end()) {
            opacity = xaml::ScalarTransition();
            opacity.Duration(Milliseconds(ms->second));
        }
        element.OpacityTransition(opacity);

        xaml::BrushTransition background = before.background;
        if (auto ms = durations.find("background"); ms != durations.end()) {
            background = xaml::BrushTransition();
            background.Duration(Milliseconds(ms->second));
        }
        if (backgroundHost) SetBackgroundTransition(backgroundHost, background);

        xaml::Vector3Transition scale = before.scale;
        xaml::ScalarTransition rotation = before.rotation;
        xaml::Vector3Transition translation = before.translation;
        if (auto ms = durations.find("transform"); ms != durations.end()) {
            scale = xaml::Vector3Transition();
            scale.Duration(Milliseconds(ms->second));
            rotation = xaml::ScalarTransition();
            rotation.Duration(Milliseconds(ms->second));
            translation = xaml::Vector3Transition();
            translation.Duration(Milliseconds(ms->second));
        }
        element.ScaleTransition(scale);
        element.RotationTransition(rotation);
        element.TranslationTransition(translation);
    } catch (...) {
        // An element Apple Music drives through its composition visual refuses these properties. A transition of the
        // transform alone still eases, on its RenderTransform (WriteStoryTransform); any other is refused.
        auto transform = durations.find("transform");
        // The width glides apart, above, and a filter on the compositor (ElementBlur reads m_transitions): neither
        // needs these properties.
        size_t others = durations.size() - durations.count("width") - durations.count("filter");
        if (!value.text.empty() && others == 0) {
            m_transitionOriginals.erase(id);
            m_transitions[id] = value.text;
            return true;
        }
        if (!value.text.empty() && (transform == durations.end() || others != 1)) return false;
        m_transitionOriginals.erase(id);
        if (value.text.empty()) {
            m_storyDurations.erase(id);
            m_transitions.erase(id);
        } else {
            m_storyDurations[id] = transform->second;
            m_transitions[id] = value.text;
        }
        return true;
    }
    if (value.text.empty()) {
        m_transitions.erase(id);
        m_transitionOriginals.erase(id);
    } else {
        m_transitions[id] = value.text;
    }
    return true;
}

// For an element whose transform animates: the transform goes to UIElement.Scale, Rotation and Translation, which
// the transitions above animate, around the element's RenderTransformOrigin. A value from before the theme (Opaque)
// animates back to no transform. False when the element refuses, and the caller writes RenderTransform instead.
bool XamlTreeBackend::WriteAnimatedTransform(theme::NodeId id, const xaml::UIElement& element, const theme::Value& value) {
    std::vector<double> parts{1, 1, 0, 0, 0};
    if (value.kind == theme::ValueKind::Transform) {
        parts = Numbers(value.text, ',');
        if (parts.size() != 5) return false;
    } else if (value.kind != theme::ValueKind::Opaque) {
        return false;
    }
    try {
        // CSS scales around the centre by default, XAML around the top left corner: an origin left at 0,0 means the
        // theme did not choose one.
        auto origin = OriginOf(id, element);
        if (origin.X == 0 && origin.Y == 0 && m_facadeOrigins.count(id) == 0) origin = {0.5f, 0.5f};
        auto size = element.ActualSize();
        element.CenterPoint({size.x * origin.X, size.y * origin.Y, 0});
        element.Scale({static_cast<float>(parts[0]), static_cast<float>(parts[1]), 1});
        element.Rotation(static_cast<float>(parts[2]));
        auto translation = element.Translation();
        element.Translation({static_cast<float>(parts[3]), static_cast<float>(parts[4]), translation.z});
    } catch (...) {
        return false;
    }
    if (value.kind == theme::ValueKind::Transform) {
        m_facadeTransforms[id] = value.text;
    } else {
        m_facadeTransforms.erase(id);
    }
    return true;
}

winrt::Windows::Foundation::Point XamlTreeBackend::OriginOf(theme::NodeId id, const xaml::UIElement& element) const {
    auto kept = m_facadeOrigins.find(id);
    return kept != m_facadeOrigins.end() ? kept->second : element.RenderTransformOrigin();
}

// A CompositeTransform turns around the element's RenderTransformOrigin by itself. Its centre takes the origin the
// element refused, or the middle when none was chosen: CSS scales from the centre, XAML from the top left corner (a
// hovered button grew towards its bottom right corner).
void XamlTreeBackend::CentreOn(theme::NodeId id, const xaml::UIElement& element,
                               const media::CompositeTransform& transform) const {
    winrt::Windows::Foundation::Point origin = OriginOf(id, element);
    bool refused = m_facadeOrigins.count(id) > 0;
    if (!refused && (origin.X != 0 || origin.Y != 0)) return;
    if (!refused) origin = {0.5f, 0.5f};
    auto size = element.ActualSize();
    transform.CenterX(size.x * origin.X);
    transform.CenterY(size.y * origin.Y);
}

// For an element Apple Music animates itself, whose facade refuses the transitions: the transform eases from where it
// is to the theme's value on Ampify's CompositeTransform, a Storyboard WinUI runs off the interface thread. Back to the
// value from before the theme (Opaque), it eases to no transform, then puts that value back.
bool XamlTreeBackend::WriteStoryTransform(theme::NodeId id, const xaml::UIElement& element, const theme::Value& value,
                                          double ms) {
    std::vector<double> target{1, 1, 0, 0, 0};
    if (value.kind == theme::ValueKind::Transform) {
        target = Numbers(value.text, ',');
        if (target.size() != 5) return false;
    } else if (value.kind != theme::ValueKind::Opaque) {
        return false;
    }
    try {
        media::CompositeTransform transform{nullptr};
        if (auto ours = m_storyTransforms.find(id);
            ours != m_storyTransforms.end() && ComIdentity(element.RenderTransform()) == ComIdentity(ours->second)) {
            transform = ours->second;
        }
        if (!transform) {
            if (value.kind == theme::ValueKind::Opaque) return false;  // nothing of Ampify's to ease back
            transform = media::CompositeTransform();
            element.RenderTransform(transform);
            m_storyTransforms[id] = transform;
        }
        CentreOn(id, element, transform);
        // Read before stopping the last storyboard, which would put its end values back.
        const double from[5] = {transform.ScaleX(), transform.ScaleY(), transform.Rotation(), transform.TranslateX(),
                                transform.TranslateY()};
        if (auto running = m_stories.find(id); running != m_stories.end()) {
            running->second.Stop();
            m_stories.erase(running);
        }
        transform.ScaleX(target[0]);
        transform.ScaleY(target[1]);
        transform.Rotation(target[2]);
        transform.TranslateX(target[3]);
        transform.TranslateY(target[4]);

        animation::Storyboard story = EaseTransform(transform, from, target, ms);
        Remember(id, "RenderTransform", value, transform);
        bool back = value.kind == theme::ValueKind::Opaque;
        IInspectable original = back ? Restore(value) : nullptr;
        if (story.Children().Size() == 0) {
            if (back) {
                m_storyTransforms.erase(id);
                Forget(id, "RenderTransform");
                element.RenderTransform(original.try_as<media::Transform>());
            }
            return true;
        }
        // The handler knows its storyboard by address only: holding it would keep both alive for good.
        void* self = winrt::get_abi(story);
        winrt::weak_ref<xaml::UIElement> weak = winrt::make_weak(element);
        std::weak_ptr<bool> alive = m_alive;
        story.Completed([this, alive, id, self, back, weak, original](IInspectable const&, IInspectable const&) noexcept {
            try {
                if (alive.expired()) return;
                auto running = m_stories.find(id);
                if (running == m_stories.end() || winrt::get_abi(running->second) != self) return;  // a newer motion
                m_stories.erase(running);
                if (!back) return;
                // Only over Ampify's transform: a theme value written since, by another path, stays.
                auto ours = m_storyTransforms.find(id);
                xaml::UIElement live = weak.get();
                if (ours == m_storyTransforms.end() || !live ||
                    ComIdentity(live.RenderTransform()) != ComIdentity(ours->second)) {
                    return;
                }
                m_storyTransforms.erase(ours);
                Forget(id, "RenderTransform");
                live.RenderTransform(original.try_as<media::Transform>());
            } catch (...) {
                // nothing may escape into Apple Music's XAML; the next write of this transform puts it right
            }
        });
        m_stories[id] = story;
        story.Begin();
    } catch (...) {
        return false;
    }
    return true;
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
