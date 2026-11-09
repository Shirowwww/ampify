// The button font of an Ampify dialog (dialog_fonts.hpp). Apple's template puts WebDings on its buttons.

#if defined(AMPIFY_WITH_XAML)

#include "dialog_fonts.hpp"
#include "xaml_chrome.hpp"

#include <string_view>

#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

namespace ampify::win {
namespace {

namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = xaml::Controls;
namespace media = xaml::Media;

// The dialog's template and its content are a few levels deep; the bound keeps a walk finite.
constexpr int kMaxDepth = 24;

// The template buttons of a ContentDialog: their font is always the text font.
bool IsTemplateButton(std::wstring_view name) {
    return name == L"PrimaryButton" || name == L"SecondaryButton" || name == L"CloseButton";
}

// The element named `name` under `node` in the visual tree, or null.
xaml::FrameworkElement FindNamed(const xaml::DependencyObject& node, std::wstring_view name, int depth) {
    if (auto element = node.try_as<xaml::FrameworkElement>(); element && element.Name() == name) return element;
    if (depth >= kMaxDepth) return nullptr;
    for (int i = 0, count = media::VisualTreeHelper::GetChildrenCount(node); i < count; ++i) {
        if (auto found = FindNamed(media::VisualTreeHelper::GetChild(node, i), name, depth + 1)) return found;
    }
    return nullptr;
}

// A content button that sets its own font (a glyph written as text) keeps it: only the template's is replaced.
bool KeepsOwnFont(const controls::Button& button) {
    return HasLocalValue(button, controls::Control::FontFamilyProperty());
}

// Writes the font on the template buttons and on the content buttons that have none of their own, under `node`.
void GiveButtonsFont(const xaml::DependencyObject& node, const media::FontFamily& font, int depth) {
    if (depth >= kMaxDepth) return;
    if (auto button = node.try_as<controls::Button>()) {
        try {
            if (IsTemplateButton(button.Name()) || !KeepsOwnFont(button)) button.FontFamily(font);
        } catch (...) {
            // A button that is going away refuses the value: the next one still gets it.
        }
    }
    for (int i = 0, count = media::VisualTreeHelper::GetChildrenCount(node); i < count; ++i) {
        GiveButtonsFont(media::VisualTreeHelper::GetChild(node, i), font, depth + 1);
    }
}

}  // namespace

void GiveDialogButtonsTextFont(const controls::ContentDialog& dialog) {
    // Later on the dispatcher: the buttons of new content exist in the visual tree only after a layout pass.
    auto queue = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
    if (!queue) return;
    // Low priority: the layout pass runs before it, so the new content's buttons are in the visual tree.
    queue.TryEnqueue(winrt::Microsoft::UI::Dispatching::DispatcherQueuePriority::Low, [dialog]() noexcept {
        try {
            // The text font is the one of the content, which is the dialog's body text.
            auto content = FindNamed(dialog, L"ContentScrollViewer", 0).try_as<controls::Control>();
            media::FontFamily font = content ? content.FontFamily() : dialog.FontFamily();
            GiveButtonsFont(dialog, font, 0);
        } catch (...) {
            // The dialog is gone or still building: it keeps the template's font until the next call.
        }
    });
}

}  // namespace ampify::win

#endif
