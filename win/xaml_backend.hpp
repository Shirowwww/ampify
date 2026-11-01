#pragma once

// The Windows backend of the theme engine: reads and writes the live XAML tree of Apple Music.
//
// It needs the Windows App SDK C++/WinRT headers, so it is built only when AMPIFY_WITH_XAML is defined (see
// CMakeLists.txt). Every member must be called on the interface thread, through PostToUiThread (ui_thread.hpp,
// DispatcherQueue of the main window).

#if defined(AMPIFY_WITH_XAML)

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.UI.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
// ComboBox inherits SelectedItem and SelectionChanged from Selector, declared in Primitives. Without this include,
// MSVC reported C3779 on both members (run 41071266204).
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

#include "../core/theme/hover.hpp"
#include "../core/theme/tree.hpp"

namespace ampify::win {

// The hover logic (theme::HoverTracker) reads and writes the element through ReadRaw and WriteRaw, the XAML access
// without the hover; Read and Write go through the tracker.
class XamlTreeBackend final : public theme::TreeBackend, private theme::HoverTracker::Access {
public:
    // Takes Apple Music's main window from AMP.Utils.dll (xaml_host.hpp: Window::Current() is null in a WinUI 3
    // desktop app). False when the window is not up yet. The caller retries on the next tick.
    bool Attach();

    std::vector<theme::NodeInfo> Snapshot() override;
    bool Alive(theme::NodeId id) override;
    std::optional<theme::Value> Read(theme::NodeId id, std::string_view property) override;
    bool Write(theme::NodeId id, std::string_view property, const theme::Value& value) override;
    std::optional<theme::Value> ReadOriginal(theme::NodeId id, std::string_view property) override;
    std::vector<std::string> ResourceKeys() override;
    std::optional<theme::Value> ReadResource(std::string_view key) override;
    bool WriteResource(std::string_view key, const theme::Value& value) override;
    bool SetDictionary(std::string_view xaml) override;
    // Follows the pointer with PointerEntered and PointerExited, attached once per element; the tokens are kept, and an
    // empty list removes both handlers. The handlers hold a weak pointer to m_alive: once the backend is destroyed
    // (maybe off the interface thread, so they are not removed then) they do nothing.
    bool SetHover(theme::NodeId id, const std::vector<theme::HoverValue>& values) override;
    // The main window's bounds. Window.SizeChanged asks for a pass when one of the watched queries changes its answer.
    std::optional<theme::ViewSize> WindowSize() override;
    void WatchSize(const std::vector<theme::MediaQuery>& queries) override;

    // Adds the theme section to the first GeneralSettingsPage that has none. Returns true when it was added.
    bool EnsureSettingsSection(const std::vector<std::string>& themeNames, const std::string& activeName,
                               std::function<void(const std::string&)> onChoice);

    // The light or dark mode of the app under the theme: RequestedTheme of the main window's content. Dark for a dark
    // scheme, Light for another scheme, the app's own value back with nullopt (no theme). After a resource change, the
    // mode goes to the other one and back, which makes the elements already on screen read the new values. True when
    // it did that switch.
    bool SetAppMode(std::optional<bool> dark);

    // True once the main window has closed: Apple Music is shutting down and its tree must not be touched any more.
    bool WindowClosed() const { return *m_closed; }
    // How many state values hold a theme's value now (HoldThroughStates), and how many refused it, for the log.
    std::size_t HeldStateCount() const;
    std::size_t RefusedStateCount() const { return m_refusedStates; }
    const std::string& FirstStateRefusal() const { return m_firstRefusal; }  // the property and the reason, once
    // What made the last SetAppMode read the resources again, for the log.
    const std::string& RefreshReason() const { return m_refreshReason; }
    // What the caption buttons were set to before the first theme, once, for the log; empty after.
    std::string TakeCaptionNote() { return std::exchange(m_captionNote, std::string()); }

    // Runs `pass` on the interface thread soon after the interface may hold new elements: a page navigated
    // (Frame.Navigated, then twice more while the page fills in), rows or cards realised while scrolling
    // (ListViewBase.ContainerContentChanging, ItemsRepeater.ElementPrepared), a flyout, menu or tooltip that opens. A
    // burst of events makes one pass. Snapshot follows each such element once, so a theme reaches new content without
    // waiting for the next tick.
    void WatchChanges(std::function<void()> pass);

private:
    winrt::Microsoft::UI::Xaml::DependencyObject Resolve(theme::NodeId id) const;

    std::optional<theme::Value> ReadRaw(theme::NodeId id, std::string_view property) override;
    bool WriteRaw(theme::NodeId id, std::string_view property, const theme::Value& value) override;

    // A value that is not text (a brush with an image, a control template) is kept as an object and given a token.
    // The same object always gets the same token, so a value read on each tick or each hover does not pile up.
    theme::Value Keep(winrt::Windows::Foundation::IInspectable object);
    winrt::Windows::Foundation::IInspectable Restore(const theme::Value& token) const;

    // An object Ampify made from a value (a gradient, an image or acrylic brush, a transform, a shadow) reads back as
    // that value while the element still holds it: the next tick then finds the value in place and writes nothing.
    void Remember(theme::NodeId id, std::string_view property, const theme::Value& value,
                  winrt::Windows::Foundation::IInspectable object);
    std::optional<theme::Value> Recognize(theme::NodeId id, std::string_view property,
                                          const winrt::Windows::Foundation::IInspectable& object) const;
    void Forget(theme::NodeId id, std::string_view property);

    winrt::Microsoft::UI::Xaml::Media::Brush ArtworkBrush(const std::string& text);
    void FollowArtwork(const winrt::Microsoft::UI::Xaml::Media::ImageSource& artwork);

    void OnState(theme::NodeId id, theme::Trigger trigger, bool on) noexcept;
    void OnFocusLost(theme::NodeId id) noexcept;
    void FollowStates(theme::NodeId id, const winrt::Microsoft::UI::Xaml::UIElement& element);
    void Watch(const winrt::Microsoft::UI::Xaml::DependencyObject& object, theme::NodeId id, const std::string& type);
    bool WriteTransition(theme::NodeId id, const winrt::Microsoft::UI::Xaml::DependencyObject& object,
                         const theme::Value& value);
    bool WriteAnimatedTransform(theme::NodeId id, const winrt::Microsoft::UI::Xaml::UIElement& element,
                                const theme::Value& value);
    void SchedulePass(int followUps) noexcept;
    void RunPass() noexcept;
    bool LearnPopups(const winrt::Microsoft::UI::Xaml::XamlRoot& root);
    void WatchPopups(const winrt::Microsoft::UI::Xaml::UIElement& content);
    void CheckPopups() noexcept;
    std::vector<bool> MediaAnswers();
    void OnSizeChanged() noexcept;
    void RemoveHoverHandlers(theme::NodeId id);
    bool FollowHover(theme::NodeId id, const std::vector<theme::HoverValue>& values);  // SetHover, unguarded
    // The PointerOver and Pressed states of a control template write some properties of its parts above any local
    // value: a theme's background on a button's Root vanished under the pointer and while pressed (greenroom's white
    // play button showed its black icon on black). The brushes those states read ({Control}{Property}PointerOver and
    // Pressed) get the theme's value in that control's own resources, the Pressed ones the :active value when the theme
    // gives one; a null value leaves that state Apple Music's. Keys a control reads at rest (a scroll bar's) get `plain`,
    // the written value. ReleaseStates gives Apple Music's back.
    void HoldThroughStates(theme::NodeId id, const winrt::Microsoft::UI::Xaml::DependencyObject& object,
                           std::string_view property, const winrt::Windows::Foundation::IInspectable& value,
                           const winrt::Windows::Foundation::IInspectable& pressed,
                           const winrt::Windows::Foundation::IInspectable& plain) noexcept;
    void ReleaseStates(theme::NodeId id, std::string_view property) noexcept;
    void UpdateStates(theme::NodeId id, const winrt::Microsoft::UI::Xaml::DependencyObject& object,
                      std::string_view property, winrt::Windows::Foundation::IInspectable shown) noexcept;
    std::optional<winrt::Windows::Foundation::IInspectable> KeyOriginal(
        void* holder, const std::wstring& key, const std::pair<theme::NodeId, std::string>& except) const;
    void MarkResourcesDirty(std::string why);
    // The window's caption buttons in the mode of the theme; nullopt gives them back the app's own colours.
    void SetCaptionMode(std::optional<bool> dark);
    struct CaptionColours;
    static void WriteCaption(const winrt::Microsoft::UI::Windowing::AppWindowTitleBar& bar, const CaptionColours& wanted);
    CaptionColours Restored(const winrt::Microsoft::UI::Windowing::AppWindowTitleBar& bar) const;

    winrt::weak_ref<winrt::Microsoft::UI::Xaml::Window> m_window;
    std::map<theme::NodeId, winrt::weak_ref<winrt::Microsoft::UI::Xaml::DependencyObject>> m_elements;
    std::map<theme::NodeId, std::string> m_templates;  // the XAML text of the template last written to each element
    std::map<std::uint64_t, winrt::Windows::Foundation::IInspectable> m_kept;
    std::map<void*, std::uint64_t> m_keptTokens;  // the token of each kept object, by its COM identity
    std::uint64_t m_nextToken = 1;
    struct Written {
        theme::Value value;
        winrt::Windows::Foundation::IInspectable object;  // may be null: box-shadow: none writes no shadow
        winrt::weak_ref<winrt::Microsoft::UI::Xaml::DependencyObject> element;  // to forget the entry once it is gone
    };
    std::map<std::pair<theme::NodeId, std::string>, Written> m_written;
    std::map<theme::NodeId, float> m_translations;  // the Z of UIElement.Translation before box-shadow raised it
    theme::HoverTracker m_hover;
    // The events followed for the states an element's values use (FollowStates).
    struct HoverHandlers {
        winrt::weak_ref<winrt::Microsoft::UI::Xaml::UIElement> element;
        bool hover = false;  // PointerEntered, PointerExited
        winrt::event_token entered;
        winrt::event_token exited;
        bool active = false;  // added with handledEventsToo: a button marks its pointer events handled
        winrt::Windows::Foundation::IInspectable pressed{nullptr};
        winrt::Windows::Foundation::IInspectable released{nullptr};
        bool focus = false;  // GotFocus, LostFocus, which bubble from the element's parts
        winrt::event_token gotFocus;
        winrt::event_token lostFocus;
    };
    std::map<theme::NodeId, HoverHandlers> m_hoverHandlers;
    struct HeldStates {
        winrt::weak_ref<winrt::Microsoft::UI::Xaml::DependencyObject> element;  // to forget the entry once it is gone
        winrt::weak_ref<winrt::Microsoft::UI::Xaml::Controls::Control> control;  // whose Resources hold the keys
        void* holder = nullptr;  // that control's identity, compared without resolving the weak reference
        // Each key written to the control's own resources, with the value it had there (null when it had none).
        std::vector<std::pair<std::wstring, winrt::Windows::Foundation::IInspectable>> keys;
    };
    std::map<std::pair<theme::NodeId, std::string>, HeldStates> m_heldStates;
    std::size_t m_refusedStates = 0;
    std::string m_firstRefusal;
    std::vector<theme::MediaQuery> m_mediaQueries;  // the theme's @media conditions (WatchSize)
    std::vector<bool> m_mediaAnswers;               // whether each matched at the last pass
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);  // the pointer handlers hold a weak pointer to it
    winrt::Microsoft::UI::Xaml::ResourceDictionary m_merged{nullptr};
    std::shared_ptr<bool> m_closed = std::make_shared<bool>(false);  // set by Window.Closed, on the interface thread
    winrt::weak_ref<winrt::Microsoft::UI::Xaml::FrameworkElement> m_modeRoot;  // the element whose mode was changed
    std::optional<winrt::Microsoft::UI::Xaml::ElementTheme> m_modeOriginal;    // its value before the theme
    struct CaptionColours {
        winrt::Windows::Foundation::IReference<winrt::Windows::UI::Color> foreground, hoverForeground, hoverBackground,
            pressedForeground, pressedBackground;
    };
    std::optional<CaptionColours> m_captionOriginal;  // the app's caption button colours, while a theme sets them
    std::optional<CaptionColours> m_captionWritten;   // what Ampify wrote last
    std::string m_captionNote;
    std::map<std::string, theme::Value> m_addedKeys;  // keys added on top of the theme dictionaries, with the value before
    bool m_resourcesDirty = false;  // a resource changed: {ThemeResource} references must be resolved again
    std::string m_dirtyReason;      // the first change since the last refresh, and how many followed it
    int m_dirtyCount = 0;
    std::string m_refreshReason;
    // `background: artwork`: the image Apple Music shows for the song playing (Image#StaticArtwork in its play bar),
    // found again on each snapshot, and the brushes Ampify made from it, which follow it when the song changes.
    winrt::Microsoft::UI::Xaml::Media::ImageSource m_artwork{nullptr};
    std::vector<winrt::Microsoft::UI::Xaml::Media::ImageBrush> m_artworkBrushes;
    // WatchChanges: the pass to run, the elements followed (frames and lists), the timer that groups their events, and
    // the passes still due after a navigation.
    std::function<void()> m_pass;
    std::map<theme::NodeId, winrt::weak_ref<winrt::Microsoft::UI::Xaml::DependencyObject>> m_watched;
    winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_passTimer{nullptr};
    int m_followUps = 0;
    // Flyouts, menus and tooltips open in popups outside the window's tree. Each popup seen open is walked from then on,
    // open or closed: a flyout keeps its content and the theme between two openings. A click or a key in the window
    // (m_inputRoot) looks for a popup that opened at once, a timer every quarter of a second (m_openPopups: the open
    // ones at the last look).
    std::vector<winrt::weak_ref<winrt::Microsoft::UI::Xaml::Controls::Primitives::Popup>> m_popups;
    std::vector<void*> m_openPopups;
    winrt::weak_ref<winrt::Microsoft::UI::Xaml::UIElement> m_inputRoot;
    winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_popupTimer{nullptr};
    // Every element written to, found again out of the snapshot (Alive): a menu's entries leave its tree when it
    // closes, and get their originals back when the theme comes off then.
    std::map<theme::NodeId, winrt::weak_ref<winrt::Microsoft::UI::Xaml::DependencyObject>> m_touched;
    // `transition`: the text written to each element, the transitions it had before (put back when the text goes
    // empty), and, for an element whose transform animates, the transform shown through UIElement.Scale, Rotation and
    // Translation (which XAML animates by itself) instead of RenderTransform.
    struct TransitionOriginals {
        winrt::Microsoft::UI::Xaml::ScalarTransition opacity{nullptr};
        winrt::Microsoft::UI::Xaml::BrushTransition background{nullptr};
        winrt::Microsoft::UI::Xaml::Vector3Transition scale{nullptr};
        winrt::Microsoft::UI::Xaml::ScalarTransition rotation{nullptr};
        winrt::Microsoft::UI::Xaml::Vector3Transition translation{nullptr};
    };
    std::map<theme::NodeId, std::string> m_transitions;
    std::map<theme::NodeId, TransitionOriginals> m_transitionOriginals;
    std::map<theme::NodeId, std::string> m_facadeTransforms;
    // The origins an element refused because its transform goes through Scale and Translation: the facade turns
    // around them instead (WriteAnimatedTransform).
    std::map<theme::NodeId, winrt::Windows::Foundation::Point> m_facadeOrigins;
};

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
