#include "window_backdrop.hpp"

#if defined(AMPIFY_WITH_XAML)

#include "xaml_host.hpp"

#include "../core/journal.hpp"

#ifdef GetCurrentTime
#undef GetCurrentTime  // windows.h macro that breaks Storyboard::GetCurrentTime in the projection
#endif

#include <winrt/Microsoft.UI.Composition.SystemBackdrops.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

#include <string>

namespace ampify::win {

namespace {

namespace xaml = winrt::Microsoft::UI::Xaml;
namespace media = winrt::Microsoft::UI::Xaml::Media;
using theme::WindowMaterial;

struct Backdrop {
    winrt::weak_ref<xaml::Window> window;            // Apple's own window: a weak reference resolves
    media::SystemBackdrop original{nullptr};         // what Apple Music had, given back when the theme asks for none
    bool saved = false;
    std::optional<WindowMaterial> applied;
};

Backdrop& State() {
    static Backdrop* state = new Backdrop();  // never destroyed: it holds XAML references, released with the process
    return *state;
}

media::SystemBackdrop Make(WindowMaterial material) {
    if (material == WindowMaterial::Acrylic) return media::DesktopAcrylicBackdrop();
    media::MicaBackdrop mica;
    mica.Kind(material == WindowMaterial::MicaAlt ? winrt::Microsoft::UI::Composition::SystemBackdrops::MicaKind::BaseAlt
                                                  : winrt::Microsoft::UI::Composition::SystemBackdrops::MicaKind::Base);
    return mica;
}

const char* NameOf(WindowMaterial material) {
    switch (material) {
        case WindowMaterial::Mica:
            return "Mica";
        case WindowMaterial::MicaAlt:
            return "Mica Alt";
        case WindowMaterial::Acrylic:
            return "acrylic";
    }
    return "?";
}

}  // namespace

void KeepWindowMaterial(std::optional<WindowMaterial> material, Journal& journal) noexcept {
    try {
        Backdrop& state = State();
        xaml::Window window = AppWindow(L"MainWindow");
        if (!window) return;
        if (window != state.window.get()) {
            if (!material) return;  // a new window keeps Apple's background until a theme asks for a material
            state = Backdrop{};
            state.window = winrt::make_weak(window);
        }
        if (material == state.applied) return;
        if (!state.saved) {
            state.original = window.SystemBackdrop();
            state.saved = true;
        }
        window.SystemBackdrop(material ? Make(*material) : state.original);
        state.applied = material;
        journal.Write(material ? std::string("Window: ") + NameOf(*material) + " behind the theme"
                               : std::string("Window: Apple Music's own background is back"));
    } catch (const winrt::hresult_error& error) {
        State().applied = material;  // tried once: no retry, and no line in the log, until the theme asks for another
        journal.Write("Window: the material could not be set (" + winrt::to_string(error.message()) + ")");
    } catch (...) {
        State().applied = material;
    }
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
