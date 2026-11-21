// The application's resources: the theme's colour keys and resources.xaml.

#if defined(AMPIFY_WITH_XAML)

#include "xaml_backend.hpp"

#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <set>

#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
#include <winrt/Microsoft.UI.Xaml.Navigation.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.Foundation.Numerics.h>

#include "../core/color_extract.hpp"
#include "../core/theme/colour_keys.hpp"
#include "../core/theme/properties.hpp"
#include "../core/theme/selector.hpp"
#include "../core/text_util.hpp"
#include "../core/ui_render/render_tree.hpp"
#include "sidebar_hover.hpp"
#include "xaml_host.hpp"
#include "xaml_backend_detail.hpp"

namespace ampify::win {

using namespace xaml_detail;

// An icon or a text usually shows its button's colour and font, a template part its control's values: none of them
// its own. Those read as kNoOwnValue, so that removing the theme clears them instead of freezing what they showed.
std::optional<theme::Value> XamlTreeBackend::ReadOriginal(theme::NodeId id, std::string_view property) {
    try {
        xaml::DependencyObject object = Resolve(id);
        if (!object) return std::nullopt;
        if (property == "Margin" && CopiesControlMargin(object)) return kControlMarginCopy;
        if (!m_hover.Has(id)) {
            xaml::DependencyProperty own = OwnProperty(object, property);
            IInspectable local = own ? object.ReadLocalValue(own) : nullptr;
            if (own && local == xaml::DependencyProperty::UnsetValue()) return theme::kNoOwnValue;
            // Apple painted it in code with a brush of Ampify's resources: that brush comes back, recoloured by then
            // (OwnBrush), not the theme's colour it showed when it was read.
            if (local && IsOwnBrush(local)) return Keep(local);
        }
        return Read(id, property);
    } catch (...) {
        return std::nullopt;
    }
}

bool XamlTreeBackend::IsOwnBrush(const IInspectable& value) const {
    if (!value.try_as<media::SolidColorBrush>()) return false;
    return std::any_of(m_ownBrushes.begin(), m_ownBrushes.end(),
                       [&](const auto& own) { return own.second && own.second == value; });
}

// One brush per key for the life of the process, recoloured in place: Apple Music paints some texts in code (an
// album's artist) with the brush it looked up then, which kept a theme's accent after the theme until a restart.
media::SolidColorBrush XamlTreeBackend::OwnBrush(const std::string& key, ColorValue color) {
    media::SolidColorBrush& brush = m_ownBrushes.try_emplace(key, nullptr).first->second;
    if (!brush) brush = media::SolidColorBrush(color);
    else if (brush.Color() != color) brush.Color(color);
    return brush;
}

// Apple Music's accent shades (its own blue, not Windows'), as they were before a theme wrote any of them.
std::vector<ColorValue> XamlTreeBackend::AppAccentColours() {
    static constexpr std::string_view kKeys[] = {
        "AccentFillColorDefault",  "AccentFillColorDefaultBrush", "SystemAccentColor",
        "SystemAccentColorLight1", "SystemAccentColorLight2",     "SystemAccentColorLight3",
        "SystemAccentColorDark1",  "SystemAccentColorDark2",      "SystemAccentColorDark3"};
    std::vector<ColorValue> colours;
    for (std::string_view key : kKeys) {
        auto added = m_addedKeys.find(std::string(key));
        std::optional<theme::Value> value = added != m_addedKeys.end() ? std::optional{added->second} : ReadResource(key);
        if (!value || value->kind != theme::ValueKind::Color) continue;
        if (std::optional<ColorValue> colour = ColorFromText(value->text)) colours.push_back(*colour);
    }
    return colours;
}

// WinUI draws a focused text box's border with a gradient (accent on the bottom two pixels, grey above) whose stops
// it read once, at load: a plain colour in its place would paint the whole frame, so the copy keeps the gradient.
std::optional<media::LinearGradientBrush> XamlTreeBackend::OwnGradient(const std::string& key, const IInspectable& current,
                                                                       ColorValue color) {
    if (auto own = m_ownGradients.find(key); own != m_ownGradients.end()) {
        for (media::GradientStop& stop : own->second.accentStops) {
            if (stop.Color() != color) stop.Color(color);
        }
        own->second.color = color;
        return own->second.brush;
    }
    auto apple = current ? current.try_as<media::LinearGradientBrush>() : nullptr;
    if (!apple) return std::nullopt;
    media::LinearGradientBrush copy;
    copy.MappingMode(apple.MappingMode());
    copy.StartPoint(apple.StartPoint());
    copy.EndPoint(apple.EndPoint());
    copy.SpreadMethod(apple.SpreadMethod());
    copy.ColorInterpolationMode(apple.ColorInterpolationMode());
    copy.Opacity(apple.Opacity());
    if (media::Transform transform = apple.RelativeTransform()) {
        auto scale = transform.try_as<media::ScaleTransform>();
        if (!scale) return std::nullopt;  // a shape this copy would not keep
        media::ScaleTransform flip;
        flip.ScaleX(scale.ScaleX());
        flip.ScaleY(scale.ScaleY());
        flip.CenterX(scale.CenterX());
        flip.CenterY(scale.CenterY());
        copy.RelativeTransform(flip);
    }
    std::vector<ColorValue> accents = AppAccentColours();
    OwnGradientBrush own{copy, {}, color};
    for (const media::GradientStop& stop : apple.GradientStops()) {
        const bool accent = std::find(accents.begin(), accents.end(), stop.Color()) != accents.end();
        media::GradientStop mine;
        mine.Offset(stop.Offset());
        mine.Color(accent ? color : stop.Color());
        if (accent) own.accentStops.push_back(mine);
        copy.GradientStops().Append(mine);
    }
    if (own.accentStops.empty()) return std::nullopt;
    m_ownGradients.emplace(key, std::move(own));
    return copy;
}

// The known colour keys the application holds. Its dictionaries are not enumerated: that realised every deferred
// resource out of its context, and Apple Music crashed at the next theme change (0xc000027b, ERROR_NOT_FOUND stowed in
// Microsoft.UI.Xaml.dll; 4 launches out of 4 with the inspector on, 31/10). Looking up a known key is safe.
std::vector<std::string> XamlTreeBackend::ResourceKeys() {
    std::vector<std::string> keys;
    if (m_sharedResources) return keys;  // the main window's backend reads them (SharesMainResources)
    xaml::ResourceDictionary resources = xaml::Application::Current().Resources();
    for (std::string_view key : theme::ColourResourceKeys()) {
        try {
            if (resources.HasKey(winrt::box_value(winrt::to_hstring(std::string(key))))) keys.emplace_back(key);
        } catch (...) {
        }
    }
    return keys;
}

std::optional<theme::Value> XamlTreeBackend::ReadResource(std::string_view key) {
    if (m_sharedResources) return std::nullopt;
    xaml::ResourceDictionary resources = xaml::Application::Current().Resources();
    winrt::hstring name = winrt::to_hstring(std::string(key));
    IInspectable value{nullptr};
    try {
        if (!resources.HasKey(winrt::box_value(name))) return std::nullopt;
        value = resources.Lookup(winrt::box_value(name));
    } catch (...) {
        return std::nullopt;  // a resource Apple Music cannot give out of its context: read as absent
    }
    // Our copy of a gradient reads as the colour written into it, like our brushes: nothing to write on the next tick.
    if (auto own = m_ownGradients.find(std::string(key));
        own != m_ownGradients.end() && ComIdentity(own->second.brush) == ComIdentity(value)) {
        return theme::Value{theme::ValueKind::Color, ColourText(own->second.color)};
    }
    if (auto solid = value.try_as<media::SolidColorBrush>()) return theme::Value{theme::ValueKind::Color, ColourText(solid.Color())};
    // A Color resource reads as a colour too: an opaque handle would differ on every tick and be written again.
    if (auto color = value.try_as<winrt::Windows::Foundation::IReference<ColorValue>>()) {
        return theme::Value{theme::ValueKind::Color, ColourText(color.Value())};
    }
    return Keep(value);
}

bool XamlTreeBackend::WriteResource(std::string_view key, const theme::Value& value) {
    if (m_sharedResources) return false;  // written by the main window's backend, once
    std::string keyText(key);
    xaml::ResourceDictionary resources = xaml::Application::Current().Resources();
    IInspectable name = winrt::box_value(winrt::to_hstring(keyText));
    // The value Apple Music had comes back: a key Ampify added on top of the theme dictionaries is removed, so the
    // app's light and dark values work again, instead of the one read under the theme staying for good.
    if (auto added = m_addedKeys.find(keyText); added != m_addedKeys.end() && added->second == value) {
        m_addedKeys.erase(added);
        std::optional<ColorValue> before = value.kind == theme::ValueKind::Color ? ColorFromText(value.text) : std::nullopt;
        if (auto own = m_ownBrushes.find(keyText); own != m_ownBrushes.end() && own->second && before) {
            own->second.Color(*before);  // what Apple painted with it takes Apple's colour back
        }
        if (OwnsKey(resources, name)) resources.Remove(name);
        MarkResourcesDirty("resource " + keyText + " removed");
        return true;
    }
    if (!OwnsKey(resources, name) && !m_addedKeys.count(keyText)) {
        if (std::optional<theme::Value> before = ReadResource(key)) m_addedKeys.emplace(keyText, *before);
    }

    std::optional<IInspectable> object;
    if (value.kind == theme::ValueKind::Color) {
        std::optional<ColorValue> color = ColorFromText(value.text);
        if (!color) return false;
        IInspectable current{nullptr};
        try {
            if (resources.HasKey(name)) current = resources.Lookup(name);
        } catch (...) {
        }
        if (m_ownGradients.count(keyText) || (current && current.try_as<media::LinearGradientBrush>())) {
            std::optional<media::LinearGradientBrush> gradient = OwnGradient(keyText, current, *color);
            if (!gradient) return false;  // left as Apple drew it rather than a frame filled with the accent
            object = *gradient;
        } else {
            object = OwnBrush(keyText, *color);
        }
    } else if (value.kind == theme::ValueKind::Opaque) {
        object = Restore(value);
    }
    if (!object) return false;

    // Fluent names a Color without the Brush suffix (SolidBackgroundFillColorBase) and the brush with it: a brush in
    // place of a Color breaks every brush built on it, so the value keeps the kind already there.
    if (value.kind == theme::ValueKind::Color && resources.HasKey(name)) {
        if (std::optional<ColorValue> color = ColorFromText(value.text);
            color && resources.Lookup(name).try_as<winrt::Windows::Foundation::IReference<ColorValue>>()) {
            object = winrt::box_value(*color);
        }
    }
    // Our brush already holds the key: recoloured above, everything drawn with it follows, nothing to read again.
    if (OwnsKey(resources, name) && resources.Lookup(name) == *object) return true;
    resources.Insert(name, *object);
    MarkResourcesDirty("resource " + keyText);
    return true;
}

void XamlTreeBackend::MarkResourcesDirty(std::string why) {
    if (!m_resourcesDirty) {
        m_dirtyReason = std::move(why);
        m_dirtyCount = 0;
    }
    m_resourcesDirty = true;
    m_dirtyCount++;
}

bool XamlTreeBackend::SetDictionary(std::string_view xaml) {
    if (m_sharedResources) return true;  // merged once, by the main window's backend
    xaml::ResourceDictionary resources = xaml::Application::Current().Resources();
    auto merged = resources.MergedDictionaries();

    if (m_merged) {
        uint32_t index = 0;
        if (merged.IndexOf(m_merged, index)) merged.RemoveAt(index);
        m_merged = nullptr;
    }
    MarkResourcesDirty("dictionary");
    if (xaml.empty()) return true;

    xaml::ResourceDictionary dictionary = xaml::Markup::XamlReader::Load(winrt::to_hstring(std::string(xaml))).try_as<xaml::ResourceDictionary>();
    if (!dictionary) return false;
    merged.Append(dictionary);
    m_merged = dictionary;
    return true;
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
