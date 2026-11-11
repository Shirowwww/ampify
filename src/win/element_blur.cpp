#include <windows.h>
#include <unknwn.h>
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include "element_blur.hpp"

#include "artwork_passthrough.hpp"

#if defined(AMPIFY_WITH_XAML)

#include <cstdlib>
#include <utility>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

#include "../core/theme/css_values.hpp"
#include "image_effects.hpp"

namespace ampify::win {

namespace xaml = winrt::Microsoft::UI::Xaml;
namespace media = winrt::Microsoft::UI::Xaml::Media;
namespace comp = winrt::Microsoft::UI::Composition;
using winrt::Windows::Foundation::IInspectable;

namespace {

constexpr wchar_t kSource[] = L"source";
constexpr wchar_t kOpacity[] = L"Opacity";
constexpr size_t kMaxFactories = 64;  // a theme uses a handful of colour filters; past this, the cache starts again
constexpr size_t kAmountOnly = 1;
constexpr size_t kWithEdges = 3;
constexpr size_t kWithColours = 23;

// The gradient that shows the blurred copy at the top and bottom (opaque there) and the sharp one in the middle (its
// inverse). A side of 0 starts clear.
comp::CompositionLinearGradientBrush EdgeMask(const comp::Compositor& compositor, BlurEdges edges, bool inverse) {
    comp::CompositionLinearGradientBrush mask = compositor.CreateLinearGradientBrush();
    mask.StartPoint({0, 0});
    mask.EndPoint({0, 1});
    const winrt::Windows::UI::Color blur{255, 0, 0, 0};
    const winrt::Windows::UI::Color sharp{0, 0, 0, 0};
    auto at = [&](bool blurred) { return (blurred != inverse) ? blur : sharp; };
    for (auto [offset, colour] : {std::pair{0.0f, at(edges.top > 0)}, std::pair{edges.top, at(false)},
                                  std::pair{1.0f - edges.bottom, at(false)}, std::pair{1.0f, at(edges.bottom > 0)}}) {
        mask.ColorStops().Append(compositor.CreateColorGradientStop(offset, colour));
    }
    return mask;
}

comp::CompositionMaskBrush Masked(const comp::Compositor& compositor, const comp::CompositionBrush& source,
                                  const comp::CompositionBrush& mask) {
    comp::CompositionMaskBrush masked = compositor.CreateMaskBrush();
    masked.Source(source);
    masked.Mask(mask);
    return masked;
}

bool ShowsNothing(const ImageFilter& filter) {
    return filter.amount <= 0 && !filter.colours;
}

std::string ColoursKey(const ImageFilter& filter) {
    return filter.colours ? theme::FormatMatrix(*filter.colours) : std::string();
}

// An empty grid of Ampify's in `element`'s cell of `parent`, just above it and letting the pointer through: its child
// visual is free when the parent's is taken. Nothing when the parent is no panel.
xaml::Controls::Grid HostBeside(const xaml::FrameworkElement& element, const xaml::UIElement& parent) {
    auto panel = parent.try_as<xaml::Controls::Panel>();
    uint32_t index = 0;
    if (!panel || !panel.Children().IndexOf(element, index)) return nullptr;
    xaml::Controls::Grid host;
    host.Name(winrt::to_hstring(kEffectHostName));
    host.IsHitTestVisible(false);
    host.Width(element.ActualWidth());
    host.Height(element.ActualHeight());
    host.HorizontalAlignment(element.HorizontalAlignment());
    host.VerticalAlignment(element.VerticalAlignment());
    host.Margin(element.Margin());  // the same place in the cell as the element
    xaml::Controls::Grid::SetRow(host, xaml::Controls::Grid::GetRow(element));
    xaml::Controls::Grid::SetColumn(host, xaml::Controls::Grid::GetColumn(element));
    xaml::Controls::Grid::SetRowSpan(host, xaml::Controls::Grid::GetRowSpan(element));
    xaml::Controls::Grid::SetColumnSpan(host, xaml::Controls::Grid::GetColumnSpan(element));
    xaml::Controls::Canvas::SetZIndex(host, xaml::Controls::Canvas::GetZIndex(element));
    panel.Children().InsertAt(index + 1, host);
    return host;
}

}  // namespace

std::optional<ImageFilter> ImageFilter::Parse(const std::string& text) {
    std::vector<double> numbers;
    for (std::string_view piece : theme::SplitArguments(text)) {
        char* end = nullptr;
        const std::string copy(piece);
        numbers.push_back(std::strtod(copy.c_str(), &end));
        if (copy.empty() || *end != '\0') return std::nullopt;
    }
    if (numbers.size() != kAmountOnly && numbers.size() != kWithEdges && numbers.size() != kWithColours) {
        return std::nullopt;
    }
    ImageFilter filter;
    filter.amount = static_cast<float>(numbers[0]);
    if (numbers.size() >= kWithEdges) filter.edges = {static_cast<float>(numbers[1]), static_cast<float>(numbers[2])};
    if (numbers.size() == kWithColours) {
        theme::ColorMatrix matrix{};
        for (size_t i = 0; i < matrix.size(); i++) matrix[i] = static_cast<float>(numbers[kWithEdges + i]);
        if (!theme::IsIdentity(matrix)) filter.colours = matrix;
    }
    return filter;
}

ElementBlur::~ElementBlur() {
    for (auto& item : m_entries) Drop(item.second);
}

bool ElementBlur::Write(theme::NodeId id, const xaml::UIElement& element, const std::string& text, double ms) noexcept {
    try {
        std::optional<ImageFilter> filter = ImageFilter::Parse(text);
        auto framework = element.try_as<xaml::FrameworkElement>();
        if (!filter || !framework) return false;
        auto found = m_entries.find(id);
        // The copies belong to the element's place in the tree: moved elsewhere, it starts again from there.
        auto parent = media::VisualTreeHelper::GetParent(framework).try_as<xaml::UIElement>();
        if (found != m_entries.end() &&
            (found->second.element.get() != framework || found->second.parent.get() != parent)) {
            Drop(found->second);
            m_entries.erase(found);
            found = m_entries.end();
        }
        m_pending.erase(id);  // the newest write wins
        Entry* entry = found != m_entries.end() ? &found->second : nullptr;
        if (!entry) {
            if (ShowsNothing(*filter)) return true;  // nothing filtered, nothing to undo
            // An element just met, or a theme just applied, shows its filter at once. One filtered before (the end of
            // a hover, a new picture) fades it in over the element, cut away once it has.
            const bool fades = ms > 0 && m_seen.count(id) > 0;
            entry = Start(id, framework, !fades);
            if (!entry) {
                if (!ClipAndContentOf(framework)) {
                    Retry(id, framework, text, ms);
                    return true;
                }
                return false;
            }
            if (!fades) ms = 0;
        }
        entry->text = text;
        Animate(id, *entry, *filter, ms);
        return true;
    } catch (const winrt::hresult_error& error) {
        NoteArtworkFilter(std::to_string(id % 100000) + " refused: " + winrt::to_string(error.message()));
        return false;
    } catch (...) {
        return false;
    }
}

std::string ElementBlur::Read(theme::NodeId id) const {
    if (auto pending = m_pending.find(id); pending != m_pending.end()) return pending->second.text;
    auto found = m_entries.find(id);
    return found == m_entries.end() ? "0" : found->second.text;
}

void ElementBlur::Retry(theme::NodeId id, const xaml::FrameworkElement& element, const std::string& text, double ms) {
    Pending& pending = m_pending[id];
    pending.element = winrt::make_weak(element.as<xaml::UIElement>());
    pending.text = text;
    pending.ms = ms;
    std::string children;
    const int count = media::VisualTreeHelper::GetChildrenCount(element);
    for (int i = 0; i < count && i < 4; i++) {
        auto child = media::VisualTreeHelper::GetChild(element, i).try_as<xaml::FrameworkElement>();
        std::string type = child ? winrt::to_string(winrt::get_class_name(child)) : std::string("?");
        children += " " + type.substr(type.rfind('.') + 1) + (child && !child.Name().empty()
                                                                   ? "#" + winrt::to_string(child.Name()) : "");
    }
    std::string type = winrt::to_string(winrt::get_class_name(element));
    NoteArtworkFilter(std::to_string(id % 100000) + " " + type.substr(type.rfind('.') + 1) + "#" +
                      winrt::to_string(element.Name()) + " waits for its picture to settle, " +
                      std::to_string(count) + " child(ren):" + children);
    std::weak_ptr<bool> alive = m_alive;
    pending.laidOut = element.LayoutUpdated(winrt::auto_revoke, [this, alive, id](const IInspectable&,
                                                                                  const IInspectable&) {
        if (alive.expired()) return;
        try {
            auto found = m_pending.find(id);
            if (found == m_pending.end()) return;
            xaml::UIElement target = found->second.element.get();
            auto framework = target.try_as<xaml::FrameworkElement>();
            if (!framework || !ClipAndContentOf(framework)) return;  // still swapping: the next layout
            const std::string text = found->second.text;
            const double ms = found->second.ms;
            m_pending.erase(found);  // revokes this handler, which runs to its end
            NoteArtworkFilter(std::to_string(id % 100000) + " tried again");
            const bool written = Write(id, target, text, ms);
            if (!written) NoteArtworkFilter(std::to_string(id % 100000) + " refused after waiting");
        } catch (...) {
            // Out of the tree for a moment: Sweep drops it.
        }
    });
}

void ElementBlur::ClipAway(Entry& entry) {
    entry.clipPending = false;
    auto clipped = entry.clipped.get();
    if (!clipped) return;
    if (entry.xamlClip) {
        media::RectangleGeometry none;
        none.Rect({0, 0, 0, 0});  // nothing of the element shows
        clipped.Clip(none);
        return;
    }
    comp::Visual own = xaml::Hosting::ElementCompositionPreview::GetElementVisual(clipped);
    // A clip of no size cuts nothing: one pixel outside the element cuts it all.
    own.Clip(own.Compositor().CreateRectangleClip(-2, -2, -1, -1));
}

void ElementBlur::Sweep() noexcept {
    std::erase_if(m_pending, [](const auto& item) { return !item.second.element.get(); });
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        if (it->second.element.get() && it->second.content.get() && it->second.parent.get() && it->second.host.get()) {
            ++it;
            continue;
        }
        NoteArtworkFilter(std::to_string(it->first % 100000) + " dropped, its element gone");
        Drop(it->second);
        it = m_entries.erase(it);
    }
}

// The element Ampify clips away on screen and the one it draws into the surface: the element and its only child. An
// element Apple animates through its facade properties (Scale, Translation: the play bar's artwork) refuses its own
// visual; then its only child is clipped, and that child's picture (its Image, or else its first child) is drawn.
std::optional<ElementBlur::ClipAndContent> ElementBlur::ClipAndContentOf(const xaml::FrameworkElement& element) {
    if (media::VisualTreeHelper::GetChildrenCount(element) != 1) return std::nullopt;
    auto child = media::VisualTreeHelper::GetChild(element, 0).try_as<xaml::FrameworkElement>();
    if (!child) return std::nullopt;
    try {
        return ClipAndContent{element, child, xaml::Hosting::ElementCompositionPreview::GetElementVisual(element)};
    } catch (...) {
        // The element uses its facade properties: one level down.
    }
    const int count = media::VisualTreeHelper::GetChildrenCount(child);
    if (count == 0) return std::nullopt;
    xaml::UIElement picture{nullptr};
    for (int i = 0; i < count && !picture; i++) {
        picture = media::VisualTreeHelper::GetChild(child, i).try_as<xaml::Controls::Image>();
    }
    if (!picture) picture = media::VisualTreeHelper::GetChild(child, 0).try_as<xaml::UIElement>();
    if (!picture) return std::nullopt;
    try {
        return ClipAndContent{child, picture, xaml::Hosting::ElementCompositionPreview::GetElementVisual(child)};
    } catch (...) {
        // The child used its facade properties too (a theme's zoom on it, for the rest of the session): it is cut
        // away by its XAML clip, and the picture, whose own visual is still free, is drawn.
    }
    try {
        xaml::Hosting::ElementCompositionPreview::GetElementVisual(picture);
        return ClipAndContent{child, picture, nullptr, true};
    } catch (...) {
        return std::nullopt;
    }
}

// The copies live in the parent's child visual, at the content's place: a child visual of the element itself would
// be cut away with it. The content's own visual is drawn into the surface, which the element's clip does not reach.
ElementBlur::Entry* ElementBlur::Start(theme::NodeId id, const xaml::FrameworkElement& element, bool clip) {
    std::optional<ClipAndContent> pair = ClipAndContentOf(element);
    auto parent = media::VisualTreeHelper::GetParent(element).try_as<xaml::UIElement>();
    if (!pair || !parent) {
        NoteArtworkFilter(std::string(pair ? "no parent" : "no single child with a visual"));
        return nullptr;
    }
    const xaml::UIElement content = pair->content;
    // Apple's own child visual (or another of Ampify's) would be replaced and lost: the copies go beside the element.
    xaml::UIElement host = parent;
    const bool ownHost = static_cast<bool>(xaml::Hosting::ElementCompositionPreview::GetElementChildVisual(parent));
    if (ownHost) {
        host = HostBeside(element, parent);
        if (!host) {
            NoteArtworkFilter("no room beside the element for the copies");
            return nullptr;
        }
    }

    comp::Visual own = pair->visual;
    comp::Compositor compositor =
        (own ? own : xaml::Hosting::ElementCompositionPreview::GetElementVisual(content)).Compositor();
    if (m_compositor != compositor) {
        m_factories.clear();
        m_compositor = compositor;
    }
    Entry entry;
    entry.element = winrt::make_weak(element);
    entry.clipped = winrt::make_weak(pair->clipped);
    entry.parent = winrt::make_weak(parent);
    entry.host = winrt::make_weak(host);
    entry.ownHost = ownHost;
    entry.content = winrt::make_weak(content);
    entry.surface = compositor.CreateVisualSurface();
    entry.surface.SourceVisual(xaml::Hosting::ElementCompositionPreview::GetElementVisual(content));
    entry.copy = compositor.CreateSurfaceBrush(entry.surface);
    entry.copy.Stretch(comp::CompositionStretch::None);
    entry.copy.HorizontalAlignmentRatio(0);
    entry.copy.VerticalAlignmentRatio(0);
    entry.layer = compositor.CreateContainerVisual();

    entry.xamlClip = pair->xamlClip;
    if (entry.xamlClip) {
        entry.xamlClipBefore = pair->clipped.Clip();
    } else {
        entry.clipBefore = own.Clip();
        xaml::Hosting::ElementCompositionPreview::SetIsTranslationEnabled(pair->clipped, true);
        entry.place = compositor.CreatePropertySet();
        entry.place.InsertVector3(L"Offset", {0, 0, 0});
        comp::ExpressionAnimation follow = compositor.CreateExpressionAnimation(L"place.Offset + clipped.Translation");
        follow.SetReferenceParameter(L"place", entry.place);
        follow.SetReferenceParameter(L"clipped", own);
        entry.layer.StartAnimation(L"Offset", follow);
    }
    xaml::Hosting::ElementCompositionPreview::SetElementChildVisual(host, entry.layer);
    Entry& kept = m_entries.insert_or_assign(id, std::move(entry)).first->second;
    m_seen.insert(id);
    if (clip) ClipAway(kept);
    else kept.clipPending = true;
    Place(kept);
    std::weak_ptr<bool> alive = m_alive;
    kept.laidOut = element.LayoutUpdated(winrt::auto_revoke, [this, alive, id](const IInspectable&, const IInspectable&) {
        if (alive.expired()) return;
        try {
            if (auto found = m_entries.find(id); found != m_entries.end()) Place(found->second);
        } catch (...) {
            // Out of the parent's tree for a moment: the next pass places it, or Sweep drops it.
        }
    });
    return &kept;
}

comp::CompositionEffectFactory ElementBlur::Factory(const std::string& key, const ImageFilter& filter, bool blurs) {
    if (auto found = m_factories.find(key); found != m_factories.end()) return found->second;
    if (m_factories.size() >= kMaxFactories) m_factories.clear();
    effects::Source source = comp::CompositionEffectSourceParameter(kSource);
    if (filter.colours) source = effects::ColorMatrixOf(source, *filter.colours).as<effects::Source>();
    comp::CompositionEffectFactory factory =
        blurs ? m_compositor.CreateEffectFactory(effects::GaussianBlur(source), {effects::kBlurAmount})
              : m_compositor.CreateEffectFactory(source.as<effects::Effect>());
    m_factories.emplace(key, factory);
    return factory;
}

// The content in the filter's colours, sharp, and blurred when `blurs`: without colours, the sharp copy is the surface
// itself, which costs no pass.
ElementBlur::Look ElementBlur::MakeLook(Entry& entry, const ImageFilter& filter, bool blurs) {
    Look look;
    look.colours = ColoursKey(filter);
    look.blurs = blurs;
    comp::Compositor compositor = entry.layer.Compositor();
    if (filter.colours) {
        comp::CompositionEffectBrush tinted = Factory("sharp" + look.colours, filter, false).CreateBrush();
        tinted.SetSourceParameter(kSource, entry.copy);
        look.sharpBrush = tinted;
    } else {
        look.sharpBrush = entry.copy;
    }
    if (blurs) {
        look.blurBrush = Factory("blur" + look.colours, filter, true).CreateBrush();
        look.blurBrush.SetSourceParameter(kSource, entry.copy);
    }
    look.visual = compositor.CreateContainerVisual();
    look.sharp = compositor.CreateSpriteVisual();
    look.blurred = compositor.CreateSpriteVisual();
    look.visual.Children().InsertAtTop(look.sharp);
    look.visual.Children().InsertAtTop(look.blurred);
    return look;
}

// Puts `look` on top, blurred by `blur` for a start.
void ElementBlur::AddLook(Entry& entry, Look look, float blur) {
    if (look.blurBrush) look.blurBrush.Properties().InsertScalar(effects::kBlurAmount, blur);
    entry.layer.Children().InsertAtTop(look.visual);
    entry.looks.push_back(std::move(look));
    Shape(entry, entry.looks.back(), entry.filter.edges);
    Place(entry);
}

// The copies follow the content's place in the parent and its size; Apple moves the lines inside it, not it.
void ElementBlur::Place(Entry& entry) {
    auto content = entry.content.get().try_as<xaml::FrameworkElement>();
    auto host = entry.host.get();
    if (!content || !host) return;
    if (auto grid = host.try_as<xaml::FrameworkElement>(); entry.ownHost && grid) {
        if (auto element = entry.element.get()) {
            if (grid.Width() != element.ActualWidth()) grid.Width(element.ActualWidth());
            if (grid.Height() != element.ActualHeight()) grid.Height(element.ActualHeight());
        }
    }
    winrt::Windows::Foundation::Point origin = content.TransformToVisual(host).TransformPoint({0, 0});
    if (entry.place) {
        // Without the clipped element's Translation, which the expression adds: TransformToVisual counts it once
        // enabled, and the copies went twice as far as the element.
        winrt::Windows::Foundation::Numerics::float3 moved{};
        if (auto clipped = entry.clipped.get()) {
            xaml::Hosting::ElementCompositionPreview::GetElementVisual(clipped).Properties().TryGetVector3(L"Translation", moved);
        }
        origin.X -= moved.x;
        origin.Y -= moved.y;
    }
    const winrt::Windows::Foundation::Numerics::float2 size{static_cast<float>(content.ActualWidth()),
                                                             static_cast<float>(content.ActualHeight())};
    if (entry.place) {
        entry.place.InsertVector3(L"Offset", {origin.X, origin.Y, 0});
    } else if (entry.layer.Offset().x != origin.X || entry.layer.Offset().y != origin.Y) {
        entry.layer.Offset({origin.X, origin.Y, 0});
    }
    if (entry.surface.SourceSize() != size) entry.surface.SourceSize(size);
    for (Look& look : entry.looks) {
        if (look.sharp.Size() == size) continue;
        look.sharp.Size(size);
        look.blurred.Size(size);
    }
}

// Unblurred, the sharp copy shows alone. Blurred without edges, the blurred one alone. With edges, the sharp copy in
// the middle and the blurred one towards the top and bottom, the two masks adding up to the whole content.
void ElementBlur::Shape(Entry& entry, Look& look, BlurEdges edges) {
    comp::Compositor compositor = entry.layer.Compositor();
    if (!look.blurs) {
        look.blurred.IsVisible(false);
        look.sharp.IsVisible(true);
        look.sharp.Brush(look.sharpBrush);
        return;
    }
    look.blurred.IsVisible(true);
    if (edges == BlurEdges{}) {
        look.sharp.IsVisible(false);
        look.blurred.Brush(look.blurBrush);
        return;
    }
    look.sharp.IsVisible(true);
    look.sharp.Brush(Masked(compositor, look.sharpBrush, EdgeMask(compositor, edges, true)));
    look.blurred.Brush(Masked(compositor, look.blurBrush, EdgeMask(compositor, edges, false)));
}

// Same colours: the blur eases on the look that shows (a look that did not blur gives way at once to one that blurs
// from 0, which looks the same). New colours: a new look fades in over the old ones, their blurs easing together.
void ElementBlur::Animate(theme::NodeId id, Entry& entry, const ImageFilter& filter, double ms) {
    const ImageFilter before = entry.filter;
    const std::uint64_t generation = entry.generation = ++m_writes;
    entry.filter = filter;
    const bool fadesIn = entry.looks.empty() && entry.clipPending && ms > 0;
    if (!fadesIn && (ms <= 0 || entry.looks.empty())) {
        for (Look& look : entry.looks) entry.layer.Children().Remove(look.visual);
        entry.looks.clear();
        if (!ShowsNothing(filter)) AddLook(entry, MakeLook(entry, filter, filter.amount > 0), filter.amount);
        Settle(id, entry);
        return;
    }
    comp::Compositor compositor = entry.layer.Compositor();
    comp::CompositionScopedBatch batch = compositor.CreateScopedBatch(comp::CompositionBatchTypes::Animation);
    const bool blurs = filter.amount > 0 || before.amount > 0;
    if (fadesIn || entry.looks.back().colours != ColoursKey(filter)) {
        AddLook(entry, MakeLook(entry, filter, blurs), before.amount);
        comp::ScalarKeyFrameAnimation fade = compositor.CreateScalarKeyFrameAnimation();
        fade.InsertKeyFrame(0.0f, 0.0f);
        fade.InsertKeyFrame(1.0f, 1.0f, effects::EaseOut(compositor));
        fade.Duration(effects::Milliseconds(ms));
        entry.looks.back().visual.StartAnimation(kOpacity, fade);
    } else if (!entry.looks.back().blurs && blurs) {
        entry.layer.Children().Remove(entry.looks.back().visual);
        entry.looks.pop_back();
        AddLook(entry, MakeLook(entry, filter, true), before.amount);
    } else if (!(before.edges == filter.edges)) {
        Shape(entry, entry.looks.back(), filter.edges);
    }
    comp::ScalarKeyFrameAnimation easing = compositor.CreateScalarKeyFrameAnimation();
    easing.InsertKeyFrame(1.0f, filter.amount, effects::EaseOut(compositor));
    easing.Duration(effects::Milliseconds(ms));
    for (Look& look : entry.looks) {
        if (!look.blurBrush) continue;
        look.blurBrush.StopAnimation(effects::kBlurAmount);  // the blur stays where it was, and eases on from there
        look.blurBrush.StartAnimation(effects::kBlurAmount, easing);
    }
    batch.End();
    std::weak_ptr<bool> alive = m_alive;
    batch.Completed([this, alive, id, generation](const IInspectable&, const comp::CompositionBatchCompletedEventArgs&) {
        if (alive.expired()) return;
        auto found = m_entries.find(id);
        if (found == m_entries.end() || found->second.generation != generation) return;
        try {
            Settle(id, found->second);
        } catch (...) {
            // Torn down meanwhile: Sweep drops it.
        }
    });
}

// Once the easing is over, only the last look stays; an unblurred one stops paying for its blur, and a filter of
// nothing gives the element back.
void ElementBlur::Settle(theme::NodeId id, Entry& entry) {
    if (ShowsNothing(entry.filter)) {
        NoteArtworkFilter(std::to_string(id % 100000) + " given back");
        Drop(entry);
        m_entries.erase(id);
        return;
    }
    if (entry.clipPending) ClipAway(entry);  // the copies cover the element now
    while (entry.looks.size() > 1) {
        entry.layer.Children().Remove(entry.looks.front().visual);
        entry.looks.erase(entry.looks.begin());
    }
    if (entry.looks.back().blurs && entry.filter.amount <= 0) {
        entry.layer.Children().Remove(entry.looks.back().visual);
        entry.looks.pop_back();
        AddLook(entry, MakeLook(entry, entry.filter, false), 0);
    }
}

// Each step on its own: one that fails (an element already torn down) leaves the others to run.
void ElementBlur::Drop(Entry& entry) noexcept {
    entry.laidOut.revoke();
    try {
        if (auto clipped = entry.clipped.get(); clipped && entry.xamlClip) {
            clipped.Clip(entry.xamlClipBefore);
        } else if (clipped) {
            xaml::Hosting::ElementCompositionPreview::GetElementVisual(clipped).Clip(entry.clipBefore);
        }
    } catch (...) {
        // The element may be torn down already: nothing is left to put back.
    }
    try {
        auto host = entry.host.get();
        if (host && xaml::Hosting::ElementCompositionPreview::GetElementChildVisual(host) == entry.layer) {
            xaml::Hosting::ElementCompositionPreview::SetElementChildVisual(host, nullptr);
        }
        auto panel = entry.parent.get().try_as<xaml::Controls::Panel>();
        uint32_t index = 0;
        if (entry.ownHost && host && panel && panel.Children().IndexOf(host, index)) panel.Children().RemoveAt(index);
    } catch (...) {
        // The element may be torn down already: nothing is left to put back.
    }
    try {
        if (entry.surface) entry.surface.SourceVisual(nullptr);
    } catch (...) {
        // The element may be torn down already: nothing is left to put back.
    }
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
