// Written against the C++/WinRT projection of Windows App SDK 1.8.

#if defined(AMPIFY_WITH_XAML)

#include "xaml_chrome.hpp"

#include <atomic>
#include <cstdint>
#include <cstdio>

#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/Microsoft.UI.Windowing.h>

// After the projection: its macros (GetClassName, GetCurrentTime) would rename members of WinRT types.
#include "win_common.hpp"

namespace ampify::win {

namespace {

namespace xaml = winrt::Microsoft::UI::Xaml;
using winrt::Windows::Foundation::IInspectable;

// Window::Current() is null in a WinUI 3 desktop app. Apple Music keeps its windows as "app properties" of
// AMP.Utils.dll; its export SharedUtils::GetAppProperty gives them by name. Way found by AMExclusive (MIT,
// https://github.com/AntiOblivionis/AMExclusive, mod/src/UiAdapter.cpp:315-364 at 91f589d), adapted here.
// The decorated name of `winrt::Windows::Foundation::IInspectable __cdecl SharedUtils::GetAppProperty(winrt::hstring
// const&)` in AMP.Utils.dll (x64 MSVC).
constexpr char kGetAppPropertyExport[] =
    "?GetAppProperty@SharedUtils@@YA?AUIInspectable@Foundation@Windows@winrt@@AEBUhstring@5@@Z";

using GetAppPropertyFn = IInspectable(__cdecl*)(winrt::hstring const&);

GetAppPropertyFn GetAppPropertyFunction() noexcept {
    static std::atomic<GetAppPropertyFn> cached{nullptr};
    if (GetAppPropertyFn function = cached.load(std::memory_order_acquire)) return function;
    HMODULE utils = GetModuleHandleW(L"AMP.Utils.dll");
    if (!utils) return nullptr;
    auto function = reinterpret_cast<GetAppPropertyFn>(GetProcAddress(utils, kGetAppPropertyExport));
    if (function) cached.store(function, std::memory_order_release);
    return function;
}

std::atomic<Journal*> g_journal{nullptr};

}  // namespace

std::wstring_view AppPropertyName(WindowRole role) noexcept {
    switch (role) {
    case WindowRole::Main: return L"MainWindow";
    case WindowRole::Mini: return L"MiniPlayerWindow";
    case WindowRole::Player: return L"FullScreenPlayerWindow";
    }
    return L"MainWindow";
}

xaml::Window AppWindowOf(WindowRole role) noexcept {
    GetAppPropertyFn function = GetAppPropertyFunction();
    if (!function) return nullptr;
    try {
        IInspectable value = function(winrt::hstring(AppPropertyName(role)));
        return value ? value.try_as<xaml::Window>() : nullptr;
    } catch (const winrt::hresult_error& error) {
        // The mini player's and the full-screen player's windows are absent while closed: E_BOUNDS, not a problem.
        if (!(role != WindowRole::Main && error.code() == E_BOUNDS)) {
            static std::atomic<int> logged{0};
            Journal* journal = g_journal.load();
            if (journal && logged++ < 3) journal->Write("Interface: GetAppProperty failed (" + HresultText(error.code()) + ")");
        }
        return nullptr;
    } catch (...) {
        return nullptr;
    }
}

bool AppPropertyAvailable() noexcept {
    return GetAppPropertyFunction() != nullptr;
}

void SetChromeJournal(Journal* journal) noexcept {
    g_journal.store(journal);
}

HWND WindowHandle(const xaml::Window& window) noexcept {
    try {
        return window ? reinterpret_cast<HWND>(static_cast<std::uintptr_t>(window.AppWindow().Id().Value)) : nullptr;
    } catch (...) {
        return nullptr;  // a window being closed
    }
}

bool WindowOnScreen(HWND window) noexcept {
    return IsWindowVisible(window) && !IsIconic(window);
}

std::optional<CursorSpot> CursorOver(HWND window) noexcept {
    POINT cursor{};
    if (!GetCursorPos(&cursor)) return std::nullopt;
    CursorSpot spot;
    HWND under = WindowFromPoint(cursor);
    spot.root = GetAncestor(under, GA_ROOT);
    spot.rootOwner = GetAncestor(under, GA_ROOTOWNER);
    DWORD process = 0;
    if (spot.root) GetWindowThreadProcessId(spot.root, &process);
    spot.ourProcess = process == GetCurrentProcessId();
    if (window && ScreenToClient(window, &cursor)) spot.client = ClientPixel{cursor.x, cursor.y};
    return spot;
}

bool MotionAllowed() noexcept {
    try {
        // Made once on the interface thread and never released: a static destructor would run after XAML shut down.
        struct Held {
            winrt::Windows::UI::ViewManagement::UISettings settings;
        };
        static auto* held = new Held{};
        return held->settings.AnimationsEnabled();
    } catch (...) {
        return false;
    }
}

void PutBackLocal(const xaml::DependencyObject& element, const xaml::DependencyProperty& property,
                  const IInspectable& before) {
    if (!before || before == xaml::DependencyProperty::UnsetValue()) element.ClearValue(property);
    else element.SetValue(property, before);
}

bool HasLocalValue(const xaml::DependencyObject& element, const xaml::DependencyProperty& property) {
    return element.ReadLocalValue(property) != xaml::DependencyProperty::UnsetValue();
}

void PropertyLedger::Write(const xaml::DependencyObject& element, const xaml::DependencyProperty& property,
                           const IInspectable& value) {
    m_saved.push_back({element, property, element.ReadLocalValue(property)});
    element.SetValue(property, value);
}

void PropertyLedger::PutBackAll() noexcept {
    for (auto it = m_saved.rbegin(); it != m_saved.rend(); ++it) {
        try {
            PutBackLocal(it->element, it->property, it->before);
        } catch (...) {  // an element gone from the tree: nothing left to give back
        }
    }
    m_saved.clear();
}

std::string ColourText(winrt::Windows::UI::Color colour) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X%02X", colour.A, colour.R, colour.G, colour.B);
    return buffer;
}

bool SameColour(const winrt::Windows::Foundation::IReference<winrt::Windows::UI::Color>& a,
                const winrt::Windows::Foundation::IReference<winrt::Windows::UI::Color>& b) {
    if (!a || !b) return !a && !b;
    return a.Value() == b.Value();
}

// The fills are colours, not Fluent's SubtleFillColor keys: read through StaticResource in a button made by XamlReader,
// those gave no fill at all on Glass's light strip (seen on screen). The values are Fluent's, a little stronger.
std::wstring SubtleButtonResources() {
    auto brushes = [](const wchar_t* over, const wchar_t* pressed) {
        return std::wstring(L"<SolidColorBrush x:Key='ButtonBackground' Color='Transparent'/>"
                            L"<SolidColorBrush x:Key='ButtonBackgroundPointerOver' Color='") + over + L"'/>"
               L"<SolidColorBrush x:Key='ButtonBackgroundPressed' Color='" + pressed + L"'/>"
               L"<SolidColorBrush x:Key='ButtonBorderBrush' Color='Transparent'/>"
               L"<SolidColorBrush x:Key='ButtonBorderBrushPointerOver' Color='Transparent'/>"
               L"<SolidColorBrush x:Key='ButtonBorderBrushPressed' Color='Transparent'/>"
               L"<StaticResource x:Key='ButtonForegroundPointerOver' ResourceKey='TextFillColorPrimaryBrush'/>"
               L"<StaticResource x:Key='ButtonForegroundPressed' ResourceKey='TextFillColorSecondaryBrush'/>";
    };
    return L"<Button.Resources><ResourceDictionary><ResourceDictionary.ThemeDictionaries>"
           L"<ResourceDictionary x:Key='Default'>" + brushes(L"#14FFFFFF", L"#0AFFFFFF") + L"</ResourceDictionary>"
           L"<ResourceDictionary x:Key='Light'>" + brushes(L"#0F000000", L"#08000000") + L"</ResourceDictionary>"
           L"</ResourceDictionary.ThemeDictionaries></ResourceDictionary></Button.Resources>";
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
