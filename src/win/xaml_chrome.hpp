#pragma once

// One way for every tweak to touch Apple Music's windows and elements: which window, its handle, the real cursor over
// it, Windows' motion setting, a local value of Apple's to give back, a map of Apple's elements that forgets the dead
// ones, Ampify's own elements held strongly, colours as text (HRESULTs: HresultText in win_common.hpp). Compiled only
// with AMPIFY_WITH_XAML. Interface thread unless a function says otherwise. Without windows.h, whose macros
// (GetClassName, GetCurrentTime) rename members of projection headers that an includer may take after this one.

#if defined(AMPIFY_WITH_XAML)

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.h>
#include <winrt/Microsoft.UI.Xaml.h>

#include "../core/journal.hpp"

struct HWND__;  // windows.h's HWND

namespace ampify::win {

// The windows Apple Music keeps as app properties of AMP.Utils.dll.
enum class WindowRole { Main, Mini, Player };

// The name Apple gave the window ("MainWindow", "MiniPlayerWindow", "FullScreenPlayerWindow").
std::wstring_view AppPropertyName(WindowRole role) noexcept;

// The window of `role`, or null: AMP.Utils.dll not loaded yet, export missing (another build of Apple Music), window
// absent (the mini and full-screen players throw E_BOUNDS while closed), or not a Window. Any thread. Never throws.
winrt::Microsoft::UI::Xaml::Window AppWindowOf(WindowRole role) noexcept;

// True once AMP.Utils.dll is loaded and exports GetAppProperty. Any thread.
bool AppPropertyAvailable() noexcept;

// Where AppWindowOf writes its few failures. Set once, before the first call.
void SetChromeJournal(Journal* journal) noexcept;

// The Win32 handle of a XAML window, or null.
HWND__* WindowHandle(const winrt::Microsoft::UI::Xaml::Window& window) noexcept;

// Shown and not minimized.
bool WindowOnScreen(HWND__* window) noexcept;

// The real cursor, for what PointerExited does not tell (it bubbles from children, and a pointer that leaves in one
// jump or a window brought over Apple Music sends none). Read every kCursorPollEvery while something follows it.
inline constexpr auto kCursorPollEvery = std::chrono::milliseconds(200);
struct ClientPixel {
    long x = 0;
    long y = 0;
};
struct CursorSpot {
    HWND__* root = nullptr;       // the top-level window under the cursor
    HWND__* rootOwner = nullptr;  // the window that owns it (WinUI draws popups and the access-key layer in their own)
    bool ourProcess = false;      // that window is Apple Music's
    std::optional<ClientPixel> client;  // the cursor in `window`'s client area
};
std::optional<CursorSpot> CursorOver(HWND__* window) noexcept;

// False when "Animation effects" is off in Windows' settings (Accessibility > Visual effects), and when Windows cannot
// say: a person who turned motion off does not get Ampify's either.
bool MotionAllowed() noexcept;

// Gives a property back as Apple had it: its saved local value, or none (unset or null), so the template's or the
// style's value applies again.
void PutBackLocal(const winrt::Microsoft::UI::Xaml::DependencyObject& element,
                  const winrt::Microsoft::UI::Xaml::DependencyProperty& property,
                  const winrt::Windows::Foundation::IInspectable& before);
bool HasLocalValue(const winrt::Microsoft::UI::Xaml::DependencyObject& element,
                   const winrt::Microsoft::UI::Xaml::DependencyProperty& property);

// Apple's local values that Ampify replaced, given back last written first, so a property written twice ends as Apple
// had it.
class PropertyLedger {
public:
    void Write(const winrt::Microsoft::UI::Xaml::DependencyObject& element,
               const winrt::Microsoft::UI::Xaml::DependencyProperty& property,
               const winrt::Windows::Foundation::IInspectable& value);
    void PutBackAll() noexcept;
    bool empty() const { return m_saved.empty(); }

private:
    struct Saved {
        winrt::Microsoft::UI::Xaml::DependencyObject element{nullptr};
        winrt::Microsoft::UI::Xaml::DependencyProperty property{nullptr};
        winrt::Windows::Foundation::IInspectable before{nullptr};
    };
    std::vector<Saved> m_saved;
};

// What Ampify keeps about some of Apple's elements, keyed by the element and dropped once it is gone. Weak references
// resolve for Apple's elements only (xaml-lessons.md): never key an element Ampify made here.
template <class Element, class Value>
class ElementMap {
public:
    struct Entry {
        winrt::weak_ref<Element> element;
        Value value;
    };
    using Key = void*;

    static Key KeyOf(const Element& element) { return winrt::get_abi(element); }

    // The entry of a live element.
    Value* Find(const Element& element) {
        Entry* entry = FindKey(KeyOf(element));
        return entry && entry->element.get() == element ? &entry->value : nullptr;
    }
    Entry* FindKey(Key key) {
        auto found = m_entries.find(key);
        return found == m_entries.end() ? nullptr : &found->second;
    }
    Value& Add(const Element& element, Value value) {
        Entry entry{winrt::make_weak(element), std::move(value)};
        return m_entries.insert_or_assign(KeyOf(element), std::move(entry)).first->second.value;
    }
    void Erase(const Element& element) { m_entries.erase(KeyOf(element)); }
    void Sweep() {
        std::erase_if(m_entries, [](const auto& pair) { return !pair.second.element.get(); });
    }

private:
    std::unordered_map<Key, Entry> m_entries;
};

// An element held for as long as Ampify works on it: weakly when it is Apple's, strongly when Ampify made it, since a
// weak reference to an element Ampify made never resolves inside Apple Music. Release on the interface thread.
template <class Element>
class HeldElement {
public:
    HeldElement() = default;
    explicit HeldElement(const Element& element) : m_weak(element) {
        if (element && !m_weak.get()) m_strong = element;
    }
    Element get() const { return m_strong ? m_strong : m_weak.get(); }
    bool OwnedByAmpify() const { return static_cast<bool>(m_strong); }

private:
    winrt::weak_ref<Element> m_weak;
    Element m_strong{nullptr};
};

// A colour as #AARRGGBB.
std::string ColourText(winrt::Windows::UI::Color colour);
// Both empty, or both the same colour.
bool SameColour(const winrt::Windows::Foundation::IReference<winrt::Windows::UI::Color>& a,
                const winrt::Windows::Foundation::IReference<winrt::Windows::UI::Color>& b);

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
