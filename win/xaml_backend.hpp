#pragma once

// The Windows backend of the theme engine: reads and writes the live XAML tree of Apple Music.
//
// NOT COMPILED IN THE CURRENT BUILD. It needs the Windows App SDK C++/WinRT headers, so it is built only when
// AMK_WITH_XAML is defined (see CMakeLists.txt). Every member must be called on the interface thread, through
// PostToUiThread (ui_thread.hpp, DispatcherQueue of the main window). Nothing here has run against Apple Music yet: see docs/tests/M8-themes.md.

#if defined(AMK_WITH_XAML)

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
// ComboBox inherits SelectedItem and SelectionChanged from Selector, declared in Primitives. Without this include,
// MSVC reported C3779 on both members (run 41071266204).
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

#include "../core/theme/tree.hpp"

namespace amk::win {

class XamlTreeBackend final : public theme::TreeBackend {
public:
    // Takes Apple Music's main window from AMP.Utils.dll (xaml_host.hpp: Window::Current() is null in a WinUI 3
    // desktop app). False when the window is not up yet. The caller retries on the next tick.
    bool Attach();

    std::vector<theme::NodeInfo> Snapshot() override;
    std::optional<theme::Value> Read(theme::NodeId id, std::string_view property) override;
    bool Write(theme::NodeId id, std::string_view property, const theme::Value& value) override;
    std::vector<std::string> ResourceKeys() override;
    std::optional<theme::Value> ReadResource(std::string_view key) override;
    bool WriteResource(std::string_view key, const theme::Value& value) override;
    bool SetDictionary(std::string_view xaml) override;

    // Adds the theme section to the first GeneralSettingsPage that has none. Returns true when it was added.
    bool EnsureSettingsSection(const std::vector<std::string>& themeNames, const std::string& activeName,
                               std::function<void(const std::string&)> onChoice);

    // The light or dark mode of the app under the theme: RequestedTheme of the main window's content. Dark for a dark
    // scheme, Light for another scheme, the app's own value back with nullopt (no theme). After a resource change, the
    // mode goes to the other one and back, which makes the elements already on screen read the new values.
    void SetAppMode(std::optional<bool> dark);

    // True once the main window has closed: Apple Music is shutting down and its tree must not be touched any more.
    bool WindowClosed() const { return *m_closed; }

private:
    winrt::Microsoft::UI::Xaml::DependencyObject Resolve(theme::NodeId id) const;

    // A value that is not text (a brush with an image, a control template) is kept as an object and given a token.
    theme::Value Keep(winrt::Windows::Foundation::IInspectable object);
    winrt::Windows::Foundation::IInspectable Restore(const theme::Value& token) const;

    winrt::weak_ref<winrt::Microsoft::UI::Xaml::Window> m_window;
    std::map<theme::NodeId, winrt::weak_ref<winrt::Microsoft::UI::Xaml::DependencyObject>> m_elements;
    std::map<theme::NodeId, std::string> m_templates;  // the XAML text of the template last written to each element
    std::map<std::uint64_t, winrt::Windows::Foundation::IInspectable> m_kept;
    std::uint64_t m_nextToken = 1;
    winrt::Microsoft::UI::Xaml::ResourceDictionary m_merged{nullptr};
    std::shared_ptr<bool> m_closed = std::make_shared<bool>(false);  // set by Window.Closed, on the interface thread
    winrt::weak_ref<winrt::Microsoft::UI::Xaml::FrameworkElement> m_modeRoot;  // the element whose mode was changed
    std::optional<winrt::Microsoft::UI::Xaml::ElementTheme> m_modeOriginal;    // its value before the theme
    std::map<std::string, theme::Value> m_addedKeys;  // keys added on top of the theme dictionaries, with the value before
    bool m_resourcesDirty = false;  // a resource changed: {ThemeResource} references must be resolved again
};

}  // namespace amk::win

#endif  // AMK_WITH_XAML
