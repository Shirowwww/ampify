#pragma once
// `filter`: XAML has no filters. The element's content is drawn into a surface of its own (Composition's visual
// surface), filtered there (Direct2D's colour matrix, then its Gaussian blur: image_effects.*), and shown in the
// element's place, the element itself being clipped away on screen; pointer input still goes to the element. With
// edges, a mask shows the sharp copy in the middle and the blurred one towards the top and bottom. A blur of the
// backdrop instead fed back on itself over Apple's own blurred full-screen background. A transition of `filter` eases
// the blur on the compositor's thread and crossfades a change of colours (Composition animates no colour matrix).

#if defined(AMPIFY_WITH_XAML)

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

#include "../core/theme/color_matrix.hpp"
#include "../core/theme/tree.hpp"

namespace ampify::win {

// The name of the grid that holds the copies beside an element whose parent's child visual is taken: the theme
// engine's walk leaves it out, so that no selector meets it.
inline constexpr char kEffectHostName[] = "AmpifyEffectHost";

// The shares of the height blurred at the top and at the bottom (0 to 0.5); both 0: the whole rectangle.
struct BlurEdges {
    float top = 0;
    float bottom = 0;
    bool operator==(const BlurEdges&) const = default;
};

// What ValueKind::Blur says: the blur, its edges, and the colour matrix (none: the colours as they are).
struct ImageFilter {
    float amount = 0;
    BlurEdges edges;
    std::optional<theme::ColorMatrix> colours;

    // From the text of a ValueKind::Blur value; nothing when it does not read.
    static std::optional<ImageFilter> Parse(const std::string& text);
};

class ElementBlur {
public:
    ElementBlur() = default;
    ElementBlur(const ElementBlur&) = delete;
    ElementBlur& operator=(const ElementBlur&) = delete;
    ~ElementBlur();

    // Filters `element` as `text` (a ValueKind::Blur value) says, easing for `ms` from what it shows now (0: at
    // once); "0" shows the element as before. When the parent already holds a child visual (Apple's, around the play
    // bar's artwork), the copies go in an empty grid of Ampify's laid in the element's cell, just above it. False when
    // the text does not read, the element does not hold exactly one child, or the parent holds a child visual and is
    // no panel. Interface thread only.
    bool Write(theme::NodeId id, const winrt::Microsoft::UI::Xaml::UIElement& element, const std::string& text,
               double ms) noexcept;

    // What was last written for `id`: "0" when none.
    std::string Read(theme::NodeId id) const;

    // Forgets the elements that are gone. Interface thread only.
    void Sweep() noexcept;

private:
    // One way of drawing the content: its colours, blurred or not. A change of colours fades a new look in over the
    // old ones, which go once it has.
    struct Look {
        std::string colours;  // FormatMatrix of the matrix, empty for none
        bool blurs = false;
        winrt::Microsoft::UI::Composition::ContainerVisual visual{nullptr};
        winrt::Microsoft::UI::Composition::SpriteVisual sharp{nullptr};
        winrt::Microsoft::UI::Composition::SpriteVisual blurred{nullptr};
        winrt::Microsoft::UI::Composition::CompositionBrush sharpBrush{nullptr};        // the content in its colours
        winrt::Microsoft::UI::Composition::CompositionEffectBrush blurBrush{nullptr};   // and blurred, when it blurs
    };

    struct Entry {
        winrt::weak_ref<winrt::Microsoft::UI::Xaml::FrameworkElement> element;
        winrt::weak_ref<winrt::Microsoft::UI::Xaml::UIElement> clipped;   // cut away on screen: the element or its child
        winrt::weak_ref<winrt::Microsoft::UI::Xaml::UIElement> parent;    // the element's visual parent
        // Holds the copies as its child visual: the parent, or Ampify's grid beside the element (ownHost).
        winrt::weak_ref<winrt::Microsoft::UI::Xaml::UIElement> host;
        bool ownHost = false;
        winrt::weak_ref<winrt::Microsoft::UI::Xaml::UIElement> content;   // the element's child, drawn into the surface
        winrt::Microsoft::UI::Composition::CompositionClip clipBefore{nullptr};  // the element's clip before Ampify's
        winrt::Microsoft::UI::Composition::CompositionVisualSurface surface{nullptr};
        winrt::Microsoft::UI::Composition::CompositionSurfaceBrush copy{nullptr};   // the content, as it is
        winrt::Microsoft::UI::Composition::ContainerVisual layer{nullptr};          // the looks, the last on top
        std::vector<Look> looks;
        winrt::Microsoft::UI::Xaml::FrameworkElement::LayoutUpdated_revoker laidOut;
        ImageFilter filter;   // the last written
        std::string text;     // as written, for Read
        std::uint64_t generation = 0;  // the write a finished easing belongs to
        bool clipPending = false;      // the copies fade in over the element, which is cut away once they have
        // Cut away by its XAML clip: its visual is refused (facade properties used on it), only its picture's is free.
        bool xamlClip = false;
        winrt::Microsoft::UI::Xaml::Media::RectangleGeometry xamlClipBefore{nullptr};
    };

    // A write the element could not take yet (Apple swapping its picture: two children for a moment), tried again at
    // its next layout, as no theme pass may follow.
    struct Pending {
        winrt::weak_ref<winrt::Microsoft::UI::Xaml::UIElement> element;
        std::string text;
        double ms = 0;
        winrt::Microsoft::UI::Xaml::FrameworkElement::LayoutUpdated_revoker laidOut;
    };
    void Retry(theme::NodeId id, const winrt::Microsoft::UI::Xaml::FrameworkElement& element, const std::string& text,
               double ms);
    static void ClipAway(Entry& entry);

    struct ClipAndContent {
        winrt::Microsoft::UI::Xaml::UIElement clipped{nullptr};
        winrt::Microsoft::UI::Xaml::UIElement content{nullptr};
        winrt::Microsoft::UI::Composition::Visual visual{nullptr};  // the clipped one's
        bool xamlClip = false;  // `visual` refused: the clipped one is cut by its XAML clip
    };
    static std::optional<ClipAndContent> ClipAndContentOf(const winrt::Microsoft::UI::Xaml::FrameworkElement& element);
    Entry* Start(theme::NodeId id, const winrt::Microsoft::UI::Xaml::FrameworkElement& element, bool clip);
    Look MakeLook(Entry& entry, const ImageFilter& filter, bool blurs);
    void AddLook(Entry& entry, Look look, float blur);
    void Place(Entry& entry);
    void Shape(Entry& entry, Look& look, BlurEdges edges);
    void Animate(theme::NodeId id, Entry& entry, const ImageFilter& filter, double ms);
    void Settle(theme::NodeId id, Entry& entry);
    winrt::Microsoft::UI::Composition::CompositionEffectFactory Factory(const std::string& key,
                                                                         const ImageFilter& filter, bool blurs);
    void Drop(Entry& entry) noexcept;

    std::map<theme::NodeId, Entry> m_entries;
    std::map<theme::NodeId, Pending> m_pending;
    std::set<theme::NodeId> m_seen;  // filtered before: a filter coming back fades in, as the hover's end
    std::uint64_t m_writes = 0;  // numbers every write across entries: a finished easing ends only its own entry
    winrt::Microsoft::UI::Composition::Compositor m_compositor{nullptr};
    // Compiled once per compositor and kind of look: "blur" or "sharp", then the colours.
    std::map<std::string, winrt::Microsoft::UI::Composition::CompositionEffectFactory> m_factories;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);  // the handlers do nothing once this is gone
};

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
