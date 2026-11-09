#include <windows.h>
#include <unknwn.h>
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include "image_effects.hpp"

#if defined(AMPIFY_WITH_XAML)

#include <windows.graphics.effects.interop.h>

#include <chrono>
#include <cwchar>
#include <string>
#include <utility>

#include <winrt/Windows.Foundation.Numerics.h>

namespace ampify::win::effects {

namespace abi = ABI::Windows::Graphics::Effects;
using winrt::Windows::Foundation::IInspectable;
using winrt::Windows::Foundation::PropertyValue;

namespace {

// The effects' class ids and property indices (d2d1effects.h), written here so the kernel links no D2D library.
constexpr GUID kGaussianBlurId{0x1feb6d69, 0x2fe6, 0x4ac9, {0x8c, 0x58, 0x1d, 0x7f, 0x93, 0xe7, 0xa6, 0xa5}};
constexpr GUID kColorMatrixId{0x921f03d6, 0x641c, 0x47df, {0x85, 0x2d, 0xb4, 0xbb, 0x61, 0x53, 0xae, 0x11}};
constexpr GUID kCompositeId{0x48fc9f51, 0xf6ac, 0x48f1, {0x8b, 0x58, 0x3b, 0x28, 0xac, 0x46, 0xf7, 0x6d}};
constexpr std::uint32_t kBlurBalanced = 1;      // D2D1_GAUSSIANBLUR_OPTIMIZATION_BALANCED
constexpr std::uint32_t kBorderHard = 1;        // D2D1_BORDER_MODE_HARD: the edges never fade out
constexpr std::uint32_t kPremultiplied = 1;     // D2D1_COLORMATRIX_ALPHA_MODE_PREMULTIPLIED
constexpr std::uint32_t kCompositeOver = 0;     // D2D1_COMPOSITE_MODE_SOURCE_OVER

struct Named {
    const wchar_t* name;
    UINT index;
};

// One Direct2D effect: its class id, its properties in index order, the names Composition may animate, its sources.
struct D2DEffect : winrt::implements<D2DEffect, Effect, Source, abi::IGraphicsEffectD2D1Interop> {
    D2DEffect(GUID id, const wchar_t* name, std::vector<IInspectable> properties, std::vector<Named> names,
              std::vector<Source> sources)
        : m_id(id), m_name(name), m_properties(std::move(properties)), m_names(std::move(names)),
          m_sources(std::move(sources)) {}

    winrt::hstring Name() const { return m_name; }
    void Name(const winrt::hstring& name) { m_name = name; }

    HRESULT __stdcall GetEffectId(GUID* id) noexcept override {
        *id = m_id;
        return S_OK;
    }

    HRESULT __stdcall GetNamedPropertyMapping(LPCWSTR name, UINT* index,
                                              abi::GRAPHICS_EFFECT_PROPERTY_MAPPING* mapping) noexcept override {
        for (const Named& named : m_names) {
            if (_wcsicmp(name, named.name) != 0) continue;
            *index = named.index;
            *mapping = abi::GRAPHICS_EFFECT_PROPERTY_MAPPING_DIRECT;
            return S_OK;
        }
        return E_INVALIDARG;
    }

    HRESULT __stdcall GetPropertyCount(UINT* count) noexcept override {
        *count = static_cast<UINT>(m_properties.size());
        return S_OK;
    }

    HRESULT __stdcall GetProperty(UINT index, ABI::Windows::Foundation::IPropertyValue** value) noexcept override {
        if (index >= m_properties.size()) return E_INVALIDARG;
        try {
            *value = m_properties[index].as<ABI::Windows::Foundation::IPropertyValue>().detach();
            return S_OK;
        } catch (...) {
            return winrt::to_hresult();
        }
    }

    HRESULT __stdcall GetSource(UINT index, abi::IGraphicsEffectSource** source) noexcept override {
        if (index >= m_sources.size()) return E_INVALIDARG;
        try {
            *source = m_sources[index].as<abi::IGraphicsEffectSource>().detach();
            return S_OK;
        } catch (...) {
            return winrt::to_hresult();
        }
    }

    HRESULT __stdcall GetSourceCount(UINT* count) noexcept override {
        *count = static_cast<UINT>(m_sources.size());
        return S_OK;
    }

private:
    GUID m_id;
    winrt::hstring m_name;
    std::vector<IInspectable> m_properties;
    std::vector<Named> m_names;
    std::vector<Source> m_sources;
};

}  // namespace

Effect GaussianBlur(Source source) {
    return winrt::make<D2DEffect>(kGaussianBlurId, L"Blur",
                                  std::vector<IInspectable>{PropertyValue::CreateSingle(0.0f),
                                                            PropertyValue::CreateUInt32(kBlurBalanced),
                                                            PropertyValue::CreateUInt32(kBorderHard)},
                                  std::vector<Named>{{L"BlurAmount", 0}}, std::vector<Source>{std::move(source)});
}

Effect ColorMatrixOf(Source source, const theme::ColorMatrix& matrix) {
    winrt::array_view<const float> numbers(matrix.data(), static_cast<std::uint32_t>(matrix.size()));
    return winrt::make<D2DEffect>(kColorMatrixId, L"Colours",
                                  std::vector<IInspectable>{PropertyValue::CreateSingleArray(numbers),
                                                            PropertyValue::CreateUInt32(kPremultiplied),
                                                            PropertyValue::CreateBoolean(true)},
                                  std::vector<Named>{}, std::vector<Source>{std::move(source)});
}

Effect SourceOver(std::vector<Source> sources) {
    return winrt::make<D2DEffect>(kCompositeId, L"Over",
                                  std::vector<IInspectable>{PropertyValue::CreateUInt32(kCompositeOver)},
                                  std::vector<Named>{}, std::move(sources));
}

winrt::Microsoft::UI::Composition::CompositionEasingFunction EaseOut(
    const winrt::Microsoft::UI::Composition::Compositor& compositor) {
    return compositor.CreateCubicBezierEasingFunction({0.1f, 0.9f}, {0.2f, 1.0f});
}

winrt::Windows::Foundation::TimeSpan Milliseconds(double ms) {
    return std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(std::chrono::duration<double, std::milli>(ms));
}

}  // namespace ampify::win::effects

#endif  // AMPIFY_WITH_XAML
