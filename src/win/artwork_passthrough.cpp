#include "artwork_passthrough.hpp"

#if defined(AMPIFY_WITH_XAML)

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Microsoft.UI.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

#include <winrt/Microsoft.UI.Xaml.Shapes.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

#include "../core/journal.hpp"
#include "../core/topbar.hpp"
#include "xaml_chrome.hpp"

namespace ampify::win {

namespace {

namespace xaml = winrt::Microsoft::UI::Xaml;
using winrt::Microsoft::UI::Input::InputNonClientPointerSource;
using winrt::Microsoft::UI::Input::NonClientRegionKind;
using winrt::Windows::Graphics::RectInt32;

std::vector<DeviceRect> g_ours;  // the regions Ampify added last time, taken out before the new ones go in
std::vector<DeviceRect> g_wanted;  // the artwork's region as the last pass saw it, put back whenever Apple's regions change
winrt::Microsoft::UI::WindowId g_watched{};  // the window whose RegionsChanged is followed
Journal* g_journal = nullptr;  // KeepBarArtworkJournal's
std::string g_lastRegions;     // the last regions written to the journal, so a line goes only on a change
std::string g_lastVeil;
int g_lines = 0;               // lines written this session, kept few
constexpr int kMostLines = 120;

void Note(std::string& last, const std::string& line) {
    if (!g_journal || line == last || g_lines >= kMostLines) return;
    last = line;
    g_lines++;
    g_journal->Write(line);
}

std::string RectsText(const winrt::com_array<RectInt32>& rects) {
    std::string text;
    for (const RectInt32& rect : rects) {
        text += (text.empty() ? "" : " ") + std::to_string(rect.X) + "," + std::to_string(rect.Y) + " " +
                std::to_string(rect.Width) + "x" + std::to_string(rect.Height);
    }
    return text.empty() ? "none" : text;
}

std::string DeviceText(const std::vector<DeviceRect>& rects) {
    std::string text;
    for (const DeviceRect& rect : rects) {
        text += (text.empty() ? "" : " ") + std::to_string(rect.x) + "," + std::to_string(rect.y) + " " +
                std::to_string(rect.width) + "x" + std::to_string(rect.height);
    }
    return text.empty() ? "none" : text;
}

// The artwork's slot in its parent, in the window's device pixels: its layout, not where a turn draws it.
std::vector<DeviceRect> SlotOf(const xaml::FrameworkElement& artwork) {
    auto parent = artwork ? xaml::Media::VisualTreeHelper::GetParent(artwork).try_as<xaml::UIElement>() : nullptr;
    xaml::XamlRoot root = artwork ? artwork.XamlRoot() : nullptr;
    if (!parent || !root || !root.Content()) return {};
    winrt::Windows::Foundation::Rect slot = xaml::Controls::Primitives::LayoutInformation::GetLayoutSlot(artwork);
    if (slot.Width <= 0 || slot.Height <= 0) return {};
    winrt::Windows::Foundation::Rect bounds = parent.TransformToVisual(root.Content()).TransformBounds(slot);
    return {ToDeviceRect(TopbarRect{bounds.X, bounds.Y, bounds.Width, bounds.Height}, root.RasterizationScale())};
}

// Where the sidebar's pane stands in the window, in device pixels. A mirrored pane (Mirror's, on the right) is measured
// by its bounds, which do not depend on the side its coordinates count from.
std::vector<DeviceRect> PaneOf(const xaml::FrameworkElement& pane) {
    xaml::XamlRoot root = pane ? pane.XamlRoot() : nullptr;
    if (!root || !root.Content() || pane.ActualWidth() <= 0 || pane.ActualHeight() <= 0) return {};
    const winrt::Windows::Foundation::Rect own{0, 0, static_cast<float>(pane.ActualWidth()),
                                               static_cast<float>(pane.ActualHeight())};
    winrt::Windows::Foundation::Rect bounds = pane.TransformToVisual(root.Content()).TransformBounds(own);
    // Not over the window's caption strip, which stays where the window is dragged from.
    const double top = std::max<double>(bounds.Y, kTopbarCaptionHeight);
    const double height = bounds.Y + bounds.Height - top;
    if (height <= 0) return {};
    return {ToDeviceRect(TopbarRect{bounds.X, top, bounds.Width, height}, root.RasterizationScale())};
}

// Ampify's region among the window's passthrough regions, Apple's kept as they are. Writes only when it is missing or
// moved, so the RegionsChanged this write raises finds nothing to do.
void Apply(const InputNonClientPointerSource& source, const std::vector<DeviceRect>& now) {
    std::vector<DeviceRect> current;
    for (const RectInt32& rect : source.GetRegionRects(NonClientRegionKind::Passthrough)) {
        current.push_back(DeviceRect{rect.X, rect.Y, rect.Width, rect.Height});
    }
    auto held = [&](const DeviceRect& rect) { return std::find(current.begin(), current.end(), rect) != current.end(); };
    auto wanted = [&](const DeviceRect& rect) { return std::find(now.begin(), now.end(), rect) != now.end(); };
    // In place already, in whatever order the other writers (Apple, the top bar) left the list: nothing to write.
    const bool settled = std::all_of(now.begin(), now.end(), held) &&
                         std::none_of(g_ours.begin(), g_ours.end(), [&](const DeviceRect& rect) {
                             return held(rect) && !wanted(rect);
                         });
    std::vector<DeviceRect> merged = MergeRegions(current, g_ours, now);
    if (!settled) {
        std::vector<RectInt32> rects;
        for (const DeviceRect& rect : merged) rects.push_back(RectInt32{rect.x, rect.y, rect.width, rect.height});
        source.SetRegionRects(NonClientRegionKind::Passthrough, rects);
    }
    g_ours = now;  // only once written: a refused write keeps the old region known as Ampify's
}

}  // namespace

void KeepArtworkPassthrough(const xaml::FrameworkElement& artwork, const xaml::FrameworkElement& pane) noexcept {
    try {
        xaml::Window window = AppWindowOf(WindowRole::Main);
        if (!window) return;
        g_wanted = SlotOf(artwork);
        for (const DeviceRect& rect : PaneOf(pane)) g_wanted.push_back(rect);
        const winrt::Microsoft::UI::WindowId id = window.AppWindow().Id();
        auto source = InputNonClientPointerSource::GetForWindowId(id);
        // Apple sets its regions again as its layout moves, which dropped Ampify's until the next theme pass: the
        // first click on the artwork after a page change fell to the caption. Its region goes back at each change.
        if (g_watched.Value != id.Value) {
            source.RegionsChanged([](InputNonClientPointerSource const& sender, auto const&) noexcept {
                try {
                    Apply(sender, g_wanted);
                } catch (...) {
                    // A window closing refuses its regions: nothing may escape into Apple Music's XAML frame.
                }
            });
            g_watched = id;  // once subscribed: a refused subscription is tried again at the next pass
        }
        Apply(source, g_wanted);
        // For the next look at a theme whose pointer never reaches a control (Mirror's pane on the right): the regions
        // as the window has them, Ampify's among them, in device pixels.
        Note(g_lastRegions, "Window regions: caption " + RectsText(source.GetRegionRects(NonClientRegionKind::Caption)) +
                                "; passthrough " + RectsText(source.GetRegionRects(NonClientRegionKind::Passthrough)) +
                                "; Ampify's " + DeviceText(g_wanted));
    } catch (...) {
        // A window closing refuses its regions: nothing may escape into Apple Music's XAML frame.
    }
}

namespace {

// The first element named `name` among the few below `root` (the slot holds a handful).
xaml::FrameworkElement NamedBelow(const xaml::DependencyObject& root, std::wstring_view name) {
    constexpr size_t kMost = 48;
    std::vector<xaml::DependencyObject> queue{root};
    for (size_t i = 0; i < queue.size() && queue.size() < kMost; i++) {
        auto element = queue[i].try_as<xaml::FrameworkElement>();
        if (i > 0 && element && element.Name() == name) return element;
        using xaml::Media::VisualTreeHelper;
        const int count = VisualTreeHelper::GetChildrenCount(queue[i]);
        for (int child = 0; child < count; child++) queue.push_back(VisualTreeHelper::GetChild(queue[i], child));
    }
    return nullptr;
}

}  // namespace

namespace {

// "x,y wxh" of an element in the window, in device-independent pixels, as drawn (its transforms included).
std::string BoxText(const xaml::FrameworkElement& element) {
    xaml::XamlRoot root = element ? element.XamlRoot() : nullptr;
    if (!root || !root.Content()) return "none";
    const winrt::Windows::Foundation::Rect own{0, 0, static_cast<float>(element.ActualWidth()),
                                               static_cast<float>(element.ActualHeight())};
    winrt::Windows::Foundation::Rect box = element.TransformToVisual(root.Content()).TransformBounds(own);
    auto n = [](double value) { return std::to_string(std::lround(value * 10) / 10.0).substr(0, 6); };
    return n(box.X) + "," + n(box.Y) + " " + n(box.Width) + "x" + n(box.Height);
}

std::string Visible(const xaml::UIElement& element) {
    return element.Visibility() == xaml::Visibility::Visible ? "shown " + std::to_string(element.Opacity()).substr(0, 4)
                                                              : "collapsed";
}

// The picture's own elements, three levels down, as drawn: under the pointer the artwork vanished behind a light veil
// (Artwork), and which of them hides, shrinks or loses its image reads from this.
std::string PictureParts(const xaml::DependencyObject& picture, int depth) {
    std::string text;
    const int count = depth > 0 ? xaml::Media::VisualTreeHelper::GetChildrenCount(picture) : 0;
    for (int i = 0; i < count; i++) {
        auto part = xaml::Media::VisualTreeHelper::GetChild(picture, i).try_as<xaml::FrameworkElement>();
        if (!part) continue;
        std::string type = winrt::to_string(winrt::get_class_name(part));
        type = type.substr(type.rfind('.') + 1);
        std::string name = winrt::to_string(part.Name());
        text += " [" + type + (name.empty() ? "" : "#" + name) + " " + BoxText(part) + " " + Visible(part) + " scale " +
                std::to_string(part.Scale().x).substr(0, 4);
        if (auto image = part.try_as<xaml::Controls::Image>()) text += image.Source() ? " image" : " no image";
        if (auto border = part.try_as<xaml::Controls::Border>()) text += border.Background() ? " brush" : " no brush";
        text += PictureParts(part, depth - 1) + "]";
    }
    return text;
}

// The veil fills the artwork's slot, which a theme can make taller or shorter than the picture (Artwork: 55x66 around
// a 54x54 picture): its shade then stood past the picture, or left the picture's corner out, square either way. The
// shade takes the picture's box and radius as they stand, whichever rule gave them (a theme's @media, a snippet).
void FitShadeToPicture(const xaml::Shapes::Rectangle& shade, const xaml::FrameworkElement& veil,
                       const xaml::FrameworkElement& picture, const xaml::FrameworkElement& slot) {
    // Half a pixel past the picture on each side: a picture centred in a slot one pixel wider stands half a pixel off,
    // and its edge showed as a dark line beside the shade (Artwork, grown on hover).
    constexpr double kOverlap = 0.5;
    const double width = picture.ActualWidth();
    const double height = picture.ActualHeight();
    if (auto control = picture.try_as<xaml::Controls::Control>()) {
        // At most half the side: a round artwork's 999 drew a rounded square (Spinning artwork), not a disc.
        double radius = control.CornerRadius().TopLeft;
        if (width > 0 && height > 0) radius = std::min(radius, std::min(width, height) / 2 + kOverlap);
        if (shade.RadiusX() != radius) shade.RadiusX(radius);
        if (shade.RadiusY() != radius) shade.RadiusY(radius);
    }
    if (width <= 0 || height <= 0 || slot.ActualWidth() <= 0) return;  // not laid out yet: the next pass
    // From the slot, which stays laid out: the veil is collapsed until the pointer comes (no size to measure from, so
    // the shade kept the slot's box, a rounded square over a round artwork). The veil fills the slot, less its margin.
    // Layout offsets, not TransformToVisual: a theme that grows the picture on hover (Artwork) must not move the shade.
    double x = 0;
    double y = 0;
    xaml::DependencyObject at = picture;
    for (; at && at != slot; at = xaml::Media::VisualTreeHelper::GetParent(at)) {
        auto element = at.try_as<xaml::UIElement>();
        if (!element) return;
        const auto offset = element.ActualOffset();
        x += offset.x;
        y += offset.y;
    }
    if (at != slot) return;  // the picture left the slot: the next pass
    const xaml::Thickness veilMargin = veil.Margin();
    const xaml::Thickness margin{x - veilMargin.Left - kOverlap, y - veilMargin.Top - kOverlap, 0, 0};
    if (shade.Width() != width + 2 * kOverlap) shade.Width(width + 2 * kOverlap);
    if (shade.Height() != height + 2 * kOverlap) shade.Height(height + 2 * kOverlap);
    if (shade.HorizontalAlignment() != xaml::HorizontalAlignment::Left) {
        shade.HorizontalAlignment(xaml::HorizontalAlignment::Left);
    }
    if (shade.VerticalAlignment() != xaml::VerticalAlignment::Top) shade.VerticalAlignment(xaml::VerticalAlignment::Top);
    if (shade.Margin() != margin) shade.Margin(margin);
}

double g_veilStrength = 1.0;  // the theme's --artwork-veil, 1 when it sets none (Apple's opaque shade)
double g_veilWritten = 1.0;   // the last strength written to a shade

// Apple's shade hides the artwork under the pointer, and a theme's zoom with it: the theme's strength goes on the live
// shade, its icon (a sibling) untouched. A value Ampify did not write (Apple's own fade) stays.
// Opacity reads back as a float: 0.55 came back 0.5500000119, taken for Apple's own value, and a theme without the
// variable kept the last theme's strength.
bool SameOpacity(double a, double b) {
    return std::abs(a - b) < 0.001;
}

void KeepShadeStrength(const xaml::Shapes::Rectangle& shade) {
    const double now = shade.Opacity();
    if (!SameOpacity(now, 1.0) && !SameOpacity(now, g_veilWritten)) return;
    if (!SameOpacity(now, g_veilStrength)) shade.Opacity(g_veilStrength);
    g_veilWritten = g_veilStrength;
}

}  // namespace

void KeepArtworkVeil(const std::string* value) noexcept {
    double strength = 1.0;
    try {
        size_t used = 0;
        double parsed = value ? std::stod(*value, &used) : 1.0;
        if (used > 0 && std::isfinite(parsed)) strength = std::clamp(parsed, 0.0, 1.0);
    } catch (...) {
        // Not a number: Apple's shade stays as it is.
    }
    g_veilStrength = strength;
}

void KeepBarArtworkJournal(Journal& journal) noexcept {
    g_journal = &journal;
}

void NoteArtworkFilter(const std::string& line) noexcept {
    static std::string last;
    try {
        Note(last, "Artwork filter: " + line);
    } catch (...) {
        // The journal is only an aid: a line lost changes nothing.
    }
}

// Where the veil stands against the picture and its slot, each time that changes (the veil shows on hover): the cause
// of a veil cut square, or missing, reads from these lines.
void KeepVeilOnArtwork(const xaml::FrameworkElement& artwork) noexcept {
    try {
        if (!artwork) return;
        xaml::FrameworkElement picture = NamedBelow(artwork, L"ActualArtwork");
        xaml::FrameworkElement veil = NamedBelow(artwork, L"ArtworkHoverContent");
        if (!picture || !veil) return;
        std::string line = "Bar artwork: slot " + BoxText(artwork) + ", picture " + BoxText(picture) + " " +
                           Visible(picture) + PictureParts(picture, 3) + ", veil " + BoxText(veil) + " " + Visible(veil);
        int count = xaml::Media::VisualTreeHelper::GetChildrenCount(veil);
        for (int i = 0; i < count; i++) {
            auto child = xaml::Media::VisualTreeHelper::GetChild(veil, i).try_as<xaml::FrameworkElement>();
            if (!child) continue;
            line += ", child " + std::to_string(i) + " " + BoxText(child) + " " + Visible(child);
            if (auto shape = child.try_as<xaml::Shapes::Rectangle>()) {
                FitShadeToPicture(shape, veil, picture, artwork);
                KeepShadeStrength(shape);
                line += " radius " + std::to_string(shape.RadiusX()).substr(0, 4);
            }
        }
        if (artwork.Clip()) line += ", the slot clips";
        // A slot with rounded corners clips what grows past it: a theme's scale on its grids then shows no growth.
        if (auto grid = artwork.try_as<xaml::Controls::Grid>()) {
            line += ", slot radius " + std::to_string(std::lround(grid.CornerRadius().TopLeft));
        } else if (auto control = artwork.try_as<xaml::Controls::Control>()) {
            line += ", slot radius " + std::to_string(std::lround(control.CornerRadius().TopLeft));
        }
        const int parts = xaml::Media::VisualTreeHelper::GetChildrenCount(artwork);
        for (int i = 0; i < parts; i++) {
            auto part = xaml::Media::VisualTreeHelper::GetChild(artwork, i).try_as<xaml::UIElement>();
            if (!part) continue;
            auto transform = part.RenderTransform().try_as<xaml::Media::CompositeTransform>();
            line += ", part " + std::to_string(i) + " scale " + std::to_string(part.Scale().x).substr(0, 4) +
                    (transform ? " render " + std::to_string(transform.ScaleX()).substr(0, 4) : std::string());
        }
        Note(g_lastVeil, line);
    } catch (...) {
        // An element leaving the tree while it is read: the next pass looks again.
    }
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
