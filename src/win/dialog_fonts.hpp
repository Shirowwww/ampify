#pragma once

// The font of the buttons in an Ampify dialog. Apple's dialog template sets WebDings on its buttons, and no style beats
// that, so the dialog's text font is written on every button of the dialog, its content included (ui_service_pages.cpp).
// Compiled only with AMPIFY_WITH_XAML, like ui_renderer.hpp.

#if defined(AMPIFY_WITH_XAML)

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>

namespace ampify::win {

// Gives every button under `dialog` the font of the dialog's text. Call it when the dialog opens and after its content
// changes. The work runs on the dispatcher's next turn, once the layout exists; a button that refuses it is skipped.
void GiveDialogButtonsTextFont(const winrt::Microsoft::UI::Xaml::Controls::ContentDialog& dialog);

}  // namespace ampify::win

#endif
