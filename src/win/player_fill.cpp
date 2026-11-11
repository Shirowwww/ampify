#include "player_fill.hpp"

#if defined(AMPIFY_WITH_XAML)

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "../core/i18n.hpp"

namespace ampify::win {

namespace {

namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = xaml::Controls;
namespace media = xaml::Media;

constexpr wchar_t kName[] = L"AmpifyFillButton";
constexpr wchar_t kShrinkName[] = L"ExitFullScreenButton";  // Apple's, beside its close button
constexpr wchar_t kFillGlyph[] = L"";                 // Segoe Fluent Icons: FullScreen
constexpr wchar_t kWindowGlyph[] = L"";               // BackToWindow
constexpr wchar_t kIconFont[] = L"Segoe Fluent Icons,Segoe MDL2 Assets";
constexpr std::size_t kSearchLimit = 4000;

std::function<void()>& Press() {
    static auto* press = new std::function<void()>();  // never destroyed, as the XAML it serves
    return *press;
}

xaml::FrameworkElement FindNamed(const xaml::DependencyObject& root, std::wstring_view name) {
    std::vector<xaml::DependencyObject> queue{root};
    for (std::size_t i = 0; i < queue.size() && i < kSearchLimit; i++) {
        if (auto element = queue[i].try_as<xaml::FrameworkElement>(); element && element.Name() == name) return element;
        int count = media::VisualTreeHelper::GetChildrenCount(queue[i]);
        for (int child = 0; child < count; child++) queue.push_back(media::VisualTreeHelper::GetChild(queue[i], child));
    }
    return nullptr;
}

template <typename T>
T FindFirst(const xaml::DependencyObject& root) {
    std::vector<xaml::DependencyObject> queue{root};
    for (std::size_t i = 0; i < queue.size() && i < 64; i++) {
        if (auto found = queue[i].try_as<T>()) return found;
        int count = media::VisualTreeHelper::GetChildrenCount(queue[i]);
        for (int child = 0; child < count; child++) queue.push_back(media::VisualTreeHelper::GetChild(queue[i], child));
    }
    return nullptr;
}

// A button of the shrink button's type, style, size and place: a style refuses another type.
controls::Button MakeButton(const xaml::FrameworkElement& shrink) {
    auto bar = shrink.try_as<controls::AppBarButton>();
    controls::Button button = bar ? controls::AppBarButton() : controls::Button();
    button.Name(kName);
    if (auto model = shrink.try_as<controls::Button>(); model && model.Style()) button.Style(model.Style());
    if (auto model = shrink.try_as<controls::Control>()) button.Padding(model.Padding());
    if (!std::isnan(shrink.Width())) button.Width(shrink.Width());
    if (!std::isnan(shrink.Height())) button.Height(shrink.Height());
    button.Margin(shrink.Margin());
    button.HorizontalAlignment(shrink.HorizontalAlignment());
    button.VerticalAlignment(shrink.VerticalAlignment());
    controls::Grid::SetRow(button, controls::Grid::GetRow(shrink));
    controls::Grid::SetColumn(button, controls::Grid::GetColumn(shrink));
    controls::FontIcon icon;
    icon.FontFamily(media::FontFamily(kIconFont));
    auto model = FindFirst<controls::FontIcon>(shrink);
    auto drawn = model ? FindFirst<controls::TextBlock>(model) : nullptr;
    double size = drawn ? drawn.FontSize() : model ? model.FontSize() : 0;
    if (size > 0) icon.FontSize(size);
    if (bar) {
        button.as<controls::AppBarButton>().Icon(icon);
    } else {
        button.Content(icon);
    }
    button.Click([](auto const&, auto const&) noexcept {
        try {
            if (Press()) Press()();
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });
    return button;
}

void Describe(const controls::Button& button, bool filled) {
    auto bar = button.try_as<controls::AppBarButton>();
    auto icon = bar ? bar.Icon().try_as<controls::FontIcon>() : button.Content().try_as<controls::FontIcon>();
    if (icon) icon.Glyph(filled ? kWindowGlyph : kFillGlyph);
    winrt::hstring label = winrt::to_hstring(i18n::Text(filled ? "player.window" : "player.fill"));
    xaml::Automation::AutomationProperties::SetName(button, label);
    controls::ToolTipService::SetToolTip(button, winrt::box_value(label));
}

}  // namespace

void ShowFillButton(const xaml::UIElement& root, bool filled, std::function<void()> press) noexcept {
    try {
        if (!root) return;
        Press() = std::move(press);
        auto button = FindNamed(root, kName).try_as<controls::Button>();
        if (!button) {
            xaml::FrameworkElement shrink = FindNamed(root, kShrinkName);
            auto panel = shrink ? media::VisualTreeHelper::GetParent(shrink).try_as<controls::Panel>() : nullptr;
            uint32_t index = 0;
            if (!panel || !panel.Children().IndexOf(shrink, index)) return;
            button = MakeButton(shrink);
            panel.Children().InsertAt(index + 1, button);
        }
        button.Visibility(xaml::Visibility::Visible);
        Describe(button, filled);
    } catch (...) {
        // The player keeps Apple's buttons.
    }
}

void RemoveFillButton(const xaml::UIElement& root) noexcept {
    try {
        auto button = root ? FindNamed(root, kName) : nullptr;
        auto panel = button ? media::VisualTreeHelper::GetParent(button).try_as<controls::Panel>() : nullptr;
        uint32_t index = 0;
        if (panel && panel.Children().IndexOf(button, index)) panel.Children().RemoveAt(index);
    } catch (...) {
    }
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
