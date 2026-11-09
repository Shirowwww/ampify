#include <windows.h>
#include <unknwn.h>
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include "glass_brush.hpp"

#if defined(AMPIFY_WITH_XAML)

#include <map>
#include <utility>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Xaml.h>

#include "../core/theme/color_matrix.hpp"
#include "../core/theme/properties.hpp"
#include "image_effects.hpp"

namespace ampify::win {

namespace media = winrt::Microsoft::UI::Xaml::Media;
namespace comp = winrt::Microsoft::UI::Composition;

namespace {

constexpr wchar_t kBackdrop[] = L"backdrop";
constexpr wchar_t kTint[] = L"tint";
constexpr wchar_t kSheen[] = L"sheen";
constexpr double kEaseInMs = 320;  // a pane turning to glass, like Ampify's other entrances
constexpr size_t kMaxFactories = 16;

// What a liquid glass brush says it is: the states compare two brushes by it (xaml_states.cpp).
struct __declspec(uuid("6b0f3c52-6d3e-4a8e-9b51-2f6f1c9e7a14")) IGlassText : ::IUnknown {
    virtual const std::string& Text() const noexcept = 0;
};

// The effect graphs, one per saturation and compositor; a theme uses one or two.
std::map<std::string, comp::CompositionEffectFactory>& Factories() {
    thread_local std::map<std::string, comp::CompositionEffectFactory> factories;
    return factories;
}

comp::CompositionEffectFactory GlassFactory(const comp::Compositor& compositor, double saturation) {
    thread_local comp::Compositor owner{nullptr};
    auto& factories = Factories();
    if (owner != compositor || factories.size() >= kMaxFactories) {
        factories.clear();
        owner = compositor;
    }
    const std::string key = theme::FormatNumber(saturation);
    if (auto found = factories.find(key); found != factories.end()) return found->second;
    effects::Source behind = comp::CompositionEffectSourceParameter(kBackdrop);
    effects::Source vivid = effects::ColorMatrixOf(behind, theme::SaturateMatrix(saturation)).as<effects::Source>();
    effects::Effect graph = effects::SourceOver({effects::GaussianBlur(vivid).as<effects::Source>(),
                                                 comp::CompositionEffectSourceParameter(kTint),
                                                 comp::CompositionEffectSourceParameter(kSheen)});
    comp::CompositionEffectFactory factory = compositor.CreateEffectFactory(graph, {effects::kBlurAmount});
    factories.emplace(key, factory);
    return factory;
}

// Light from above: bright along the top edge, gone by the middle, a faint rim at the bottom.
comp::CompositionLinearGradientBrush Sheen(const comp::Compositor& compositor) {
    comp::CompositionLinearGradientBrush sheen = compositor.CreateLinearGradientBrush();
    sheen.StartPoint({0, 0});
    sheen.EndPoint({0, 1});
    auto white = [](std::uint8_t alpha) { return winrt::Windows::UI::Color{alpha, 255, 255, 255}; };
    for (auto [offset, colour] : {std::pair{0.0f, white(56)}, std::pair{0.06f, white(20)}, std::pair{0.5f, white(0)},
                                  std::pair{0.94f, white(0)}, std::pair{1.0f, white(14)}}) {
        sheen.ColorStops().Append(compositor.CreateColorGradientStop(offset, colour));
    }
    return sheen;
}

struct GlassBrush : media::XamlCompositionBrushBaseT<GlassBrush, IGlassText> {
    GlassBrush(GlassLook look, std::string text) : m_look(look), m_text(std::move(text)) {
        winrt::Windows::UI::Color fallback = look.tint;
        fallback.A = static_cast<std::uint8_t>(fallback.A * look.opacity);
        FallbackColor(fallback);  // shown while it has no composition brush
    }

    const std::string& Text() const noexcept override { return m_text; }

    void OnConnected() {
        if (CompositionBrush()) return;
        try {
            comp::Compositor compositor = media::CompositionTarget::GetCompositorForCurrentThread();
            comp::CompositionEffectBrush brush = GlassFactory(compositor, m_look.saturation).CreateBrush();
            brush.SetSourceParameter(kBackdrop, compositor.CreateBackdropBrush());
            winrt::Windows::UI::Color tint = m_look.tint;
            tint.A = static_cast<std::uint8_t>(tint.A * m_look.opacity);
            winrt::Windows::UI::Color clear = tint;
            clear.A = 0;
            comp::CompositionColorBrush tinted = compositor.CreateColorBrush(clear);
            brush.SetSourceParameter(kTint, tinted);
            brush.SetSourceParameter(kSheen, Sheen(compositor));
            brush.Properties().InsertScalar(effects::kBlurAmount, 0.0f);
            Ease(compositor, brush, tinted, tint);
            CompositionBrush(brush);
        } catch (...) {
            // No compositor on this thread, or the effect refused: the fallback colour paints the element.
        }
    }

    void OnDisconnected() {
        if (auto brush = CompositionBrush()) {
            CompositionBrush(nullptr);
            brush.Close();
        }
    }

private:
    void Ease(const comp::Compositor& compositor, const comp::CompositionEffectBrush& brush,
              const comp::CompositionColorBrush& tinted, winrt::Windows::UI::Color tint) const {
        comp::ScalarKeyFrameAnimation blur = compositor.CreateScalarKeyFrameAnimation();
        blur.InsertKeyFrame(1.0f, static_cast<float>(m_look.blur), effects::EaseOut(compositor));
        blur.Duration(effects::Milliseconds(kEaseInMs));
        brush.StartAnimation(effects::kBlurAmount, blur);
        comp::ColorKeyFrameAnimation colour = compositor.CreateColorKeyFrameAnimation();
        colour.InsertKeyFrame(1.0f, tint, effects::EaseOut(compositor));
        colour.Duration(effects::Milliseconds(kEaseInMs));
        tinted.StartAnimation(L"Color", colour);
    }

    GlassLook m_look;
    std::string m_text;
};

}  // namespace

media::Brush LiquidGlassBrush(const GlassLook& look, const std::string& text) {
    return winrt::make<GlassBrush>(look, text);
}

std::optional<std::string> LiquidGlassText(const media::Brush& brush) {
    if (!brush) return std::nullopt;
    winrt::com_ptr<IGlassText> glass = brush.try_as<IGlassText>();
    if (!glass) return std::nullopt;
    return glass->Text();
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
