#include "artwork_cap.hpp"

#if defined(AMPIFY_WITH_XAML)

#include <winrt/Windows.Foundation.h>

#include <unordered_map>

#include <inspectable.h>

#include "../core/theme/glyph_rows.hpp"
#include "xaml_chrome.hpp"

namespace ampify::win {

namespace {

namespace th = ampify::theme;
namespace xaml = winrt::Microsoft::UI::Xaml;
using winrt::Windows::Foundation::Size;

// AMP.UI.Views.IArtworkImage of AMP.UI.Views.winmd 1.1792 (GuidAttribute 8c023886-c0f0-52d8-9570-80bc6e9977c2), its
// slots after IInspectable up to ArtworkMaxSize, a Windows.Foundation.Size. Only the last two are called.
struct IArtworkImageAbi : ::IInspectable {
    virtual HRESULT __stdcall get_Artwork(void** value) = 0;
    virtual HRESULT __stdcall put_Artwork(void* value) = 0;
    virtual HRESULT __stdcall get_VideoArtwork(void** value) = 0;
    virtual HRESULT __stdcall put_VideoArtwork(void* value) = 0;
    virtual HRESULT __stdcall get_IsRound(unsigned char* value) = 0;
    virtual HRESULT __stdcall put_IsRound(unsigned char value) = 0;
    virtual HRESULT __stdcall get_ArtworkMaxSize(Size* value) = 0;
    virtual HRESULT __stdcall put_ArtworkMaxSize(Size value) = 0;
};
constexpr GUID kArtworkImageIid = {0x8c023886, 0xc0f0, 0x52d8, {0x95, 0x70, 0x80, 0xbc, 0x6e, 0x99, 0x77, 0xc2}};

struct Original {
    Size cap{};
    double maxWidth = 0;   // the element's own MaxWidth and MaxHeight: Apple also sets MaxHeight to 270
    double maxHeight = 0;
};

// Apple's caps of the artworks whose cap is lifted. Interface thread only; never destroyed (xaml-lessons.md).
ElementMap<xaml::FrameworkElement, Original>& Originals() {
    static auto* originals = new ElementMap<xaml::FrameworkElement, Original>();
    return *originals;
}

// The element's own bounds follow the cap: lifted with it, Apple's again once it is back.
void FollowBounds(const xaml::FrameworkElement& element, const Original& original, Size target, bool lifted) {
    double width = lifted ? th::LiftedBound(original.maxWidth, target.Width) : original.maxWidth;
    double height = lifted ? th::LiftedBound(original.maxHeight, target.Height) : original.maxHeight;
    if (element.MaxWidth() != width) element.MaxWidth(width);
    if (element.MaxHeight() != height) element.MaxHeight(height);
}

}  // namespace

void FollowArtworkCap(const xaml::FrameworkElement& element) noexcept {
    try {
        winrt::com_ptr<IArtworkImageAbi> artwork;
        auto* unknown = static_cast<::IUnknown*>(winrt::get_abi(element));
        if (!unknown || FAILED(unknown->QueryInterface(kArtworkImageIid, artwork.put_void()))) return;
        Size current{};
        if (FAILED(artwork->get_ArtworkMaxSize(&current))) return;

        auto& originals = Originals();
        originals.Sweep();
        Original* found = originals.Find(element);
        Original apple = found ? *found : Original{current, element.MaxWidth(), element.MaxHeight()};
        th::BoxSize wanted = th::LiftedMaxSize({apple.cap.Width, apple.cap.Height}, element.Width(), element.Height());
        Size target{static_cast<float>(wanted.width), static_cast<float>(wanted.height)};
        bool lifted = target.Width != apple.cap.Width || target.Height != apple.cap.Height;
        bool known = found != nullptr;
        if (lifted && !known) originals.Add(element, apple);
        if (!lifted && known) originals.Erase(element);
        if (lifted || known) FollowBounds(element, apple, target, lifted);
        if (target.Width == current.Width && target.Height == current.Height) return;
        if (SUCCEEDED(artwork->put_ArtworkMaxSize(target))) element.InvalidateMeasure();
    } catch (...) {
    }
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
