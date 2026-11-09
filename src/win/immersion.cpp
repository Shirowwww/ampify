#include "immersion.hpp"

#if defined(AMPIFY_WITH_XAML)

#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Windows.Foundation.h>

#include <inspectable.h>

#include <memory>

namespace ampify::win {

namespace {

namespace xaml = winrt::Microsoft::UI::Xaml;

constexpr wchar_t kImmersionClass[] = L"AMP.UI.Controls.ImmersionLockup";

// AMP.UI.Controls.IImmersionLockup of AMP.UI.Controls.winmd 1.1792 (GuidAttribute 3e747a87-6f6a-5f04-9576-46923101dc0f),
// its slots after IInspectable. Apple's full-screen player gives it the library's cover file and Animated true.
struct IImmersionLockupAbi : ::IInspectable {
    virtual HRESULT __stdcall Connect() = 0;
    virtual HRESULT __stdcall Render() = 0;
    virtual HRESULT __stdcall get_ImageLocation(HSTRING* value) = 0;
    virtual HRESULT __stdcall put_ImageLocation(HSTRING value) = 0;
    virtual HRESULT __stdcall get_Animated(boolean* value) = 0;
    virtual HRESULT __stdcall put_Animated(boolean value) = 0;
};
constexpr GUID kImmersionLockupIid = {0x3e747a87, 0x6f6a, 0x5f04, {0x95, 0x76, 0x46, 0x92, 0x31, 0x01, 0xdc, 0x0f}};

winrt::com_ptr<IImmersionLockupAbi> AbiOf(const xaml::FrameworkElement& lockup) {
    winrt::com_ptr<IImmersionLockupAbi> abi;
    if (lockup) static_cast<::IUnknown*>(winrt::get_abi(lockup))->QueryInterface(kImmersionLockupIid, abi.put_void());
    return abi;
}

void Show(const xaml::FrameworkElement& lockup, const std::string& address) {
    auto abi = AbiOf(lockup);
    if (!abi || address.empty()) return;
    winrt::hstring location = winrt::to_hstring(address);
    abi->put_ImageLocation(static_cast<HSTRING>(winrt::get_abi(location)));
}

}  // namespace

xaml::FrameworkElement ImmersionBackgrounds::Make(const wchar_t* name) {
    xaml::FrameworkElement lockup{nullptr};
    try {
        lockup = winrt::get_activation_factory(kImmersionClass).ActivateInstance<xaml::FrameworkElement>();
    } catch (...) {
        return nullptr;  // not in this Apple Music
    }
    auto abi = AbiOf(lockup);
    if (!abi) return nullptr;
    lockup.Name(name);
    lockup.IsHitTestVisible(false);
    xaml::Automation::AutomationProperties::SetAccessibilityView(lockup, xaml::Automation::Peers::AccessibilityView::Raw);
    abi->put_Animated(true);
    Show(lockup, m_address);
    // Apple's view connects its lockup once it is in the window: the swap chain needs the panel's size and scale. Once:
    // a decor moved back into place (KeepDecorInPlace) is loaded again, and its swap chain stays.
    auto connected = std::make_shared<bool>(false);
    lockup.Loaded([connected](winrt::Windows::Foundation::IInspectable const& sender,
                              xaml::RoutedEventArgs const&) noexcept {
        try {
            if (*connected) return;
            auto loaded = AbiOf(sender.as<xaml::FrameworkElement>());
            if (loaded && SUCCEEDED(loaded->Connect())) *connected = true;
        } catch (...) {
        }
    });
    m_lockups.push_back(lockup);
    return lockup;
}

void ImmersionBackgrounds::Follow(const std::string& address) {
    if (address.empty() || address == m_address) return;  // between two songs the address may be missing: keep the last
    m_address = address;
    for (const xaml::FrameworkElement& lockup : m_lockups) {
        try {
            Show(lockup, address);
        } catch (...) {
        }
    }
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
