#pragma once

// Private to the files of XamlTreeBackend (xaml_backend.cpp, xaml_properties.cpp, xaml_states.cpp): the namespace
// names they share and the value conversions of xaml_values.cpp. Each of those files opens it with
// `using namespace xaml_detail;` inside ampify::win.

#if defined(AMPIFY_WITH_XAML)

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>

#include "xaml_backend.hpp"

namespace ampify::win::xaml_detail {

namespace th = ampify::theme;
namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = xaml::Controls;
namespace media = xaml::Media;
namespace animation = xaml::Media::Animation;
namespace primitives = controls::Primitives;
using winrt::Windows::Foundation::IInspectable;
using ColorValue = winrt::Windows::UI::Color;

std::optional<ColorValue> ColorFromText(const std::string& text);
bool IsIconText(const winrt::hstring& text);
std::string ColorText(ColorValue color);
std::vector<double> Numbers(const std::string& text, char separator);
std::vector<std::string> Split(const std::string& text, char separator);
std::string FractionText(double number);
std::string ThicknessText(const xaml::Thickness& value);
xaml::DependencyProperty OwnProperty(const xaml::DependencyObject& object, std::string_view property);
// CharacterSpacing, FontStyle, LineHeight, TextAlignment, TextTrimming and TextWrapping: read and written by
// ReadTextProperty and WriteTextProperty, on a TextBlock (CharacterSpacing and FontStyle on a Control too).
bool IsTextProperty(std::string_view property);
std::optional<th::Value> ReadTextProperty(const xaml::DependencyObject& object, std::string_view property);
bool WriteTextProperty(const xaml::DependencyObject& object, std::string_view property, const th::Value& value);
// SelectedBackground, SelectedPointerOverBackground or SelectedPressedBackground: a list row's selection colours.
inline bool IsSelectionBrush(std::string_view property) {
    return property == "SelectedBackground" || property == "SelectedPointerOverBackground" ||
           property == "SelectedPressedBackground";
}
// The brush property of a list row behind one of those names.
media::Brush SelectionBrush(const primitives::ListViewItemPresenter& row, std::string_view property);
void SetSelectionBrush(const primitives::ListViewItemPresenter& row, std::string_view property, const media::Brush& brush);
std::string CornerText(const xaml::CornerRadius& value);
bool NoNegativeSide(const xaml::Thickness& sides);
media::Brush GradientFrom(const std::string& text);
media::Brush AcrylicFrom(const std::string& text);
media::Brush ImageFrom(const std::string& text);
std::string AlignmentText(xaml::HorizontalAlignment value);
std::string AlignmentText(xaml::VerticalAlignment value);
double ParseLength(const std::string& text);
std::string ShortName(const std::string& fullType);
// COM identity: two projections of one object compare equal only through their IUnknown.
inline void* ComIdentity(const winrt::Windows::Foundation::IUnknown& object) {
    return object ? winrt::get_abi(object.as<winrt::Windows::Foundation::IUnknown>()) : nullptr;
}
std::optional<std::uint32_t> ItemPosition(const xaml::DependencyObject& object, std::optional<std::uint32_t> target);
// A ::before or ::after image of a theme (named AmpifyDecorBefore or AmpifyDecorAfter), which Snapshot and the Order
// positions leave out.
bool IsDecor(const xaml::DependencyObject& object);
// A panel whose Children Ampify may add to: not the items host of a list, nor an ItemsRepeater.
bool TakesDecor(const controls::Panel& panel);
// The Image of a decor from "path|Stretch" (theme::Decor::image), named for its place.
controls::Image DecorImageFrom(const std::string& text, th::PseudoElement slot);
// True when the image is still in the panel, moved back first or last if the app's children moved around it.
bool KeepDecorInPlace(const controls::UIElementCollection& children, const xaml::UIElement& image,
                      th::PseudoElement slot);
bool OwnsKey(const xaml::ResourceDictionary& resources, const IInspectable& name);
std::string DescribeStates(const xaml::FrameworkElement& element);
void ReadThemeAgain(const xaml::FrameworkElement& element);
void ReadPopupsAgain(const xaml::XamlRoot& root) noexcept;

}  // namespace ampify::win::xaml_detail

#endif  // AMPIFY_WITH_XAML
