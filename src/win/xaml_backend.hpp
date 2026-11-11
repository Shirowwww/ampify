#pragma once

// The Windows backend of the theme engine: reads and writes the live XAML tree of Apple Music.
//
// It needs the Windows App SDK C++/WinRT headers, so it is built only when AMPIFY_WITH_XAML is defined (see
// CMakeLists.txt). Every member must be called on the interface thread, through PostToUiThread (ui_thread.hpp,
// DispatcherQueue of the main window).

#if defined(AMPIFY_WITH_XAML)

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef GetCurrentTime
#undef GetCurrentTime  // windows.h macro that breaks Storyboard::GetCurrentTime in the projection
#endif
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.UI.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
// ComboBox inherits SelectedItem and SelectionChanged from Selector, declared in Primitives. Without this include,
// MSVC reported C3779 on both members.
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>

#include "../core/theme/hover.hpp"
#include "../core/theme/tree.hpp"
#include "artwork_fade.hpp"
#include "artwork_large.hpp"
#include "moving_artwork.hpp"
#include "audio_bars.hpp"
#include "element_blur.hpp"
#include "gradient_fit.hpp"
#include "pane_corners.hpp"
#include "width_glide.hpp"
#include "theme_animations.hpp"
#include "xaml_chrome.hpp"

namespace ampify::win {

// The colours of the artworks themes read (art-1... of color_extract.hpp): the song playing, from the play bar's image,
// and the album or playlist on screen as page-art-1..., from its header. Read on the interface thread and taken by
// the theme service's own thread, hence the lock.
struct ArtworkColours {
    std::mutex lock;
    std::condition_variable changed;  // notified when `variables` change, so the service reloads at once
    std::map<std::string, std::string> variables;
    std::map<std::string, std::string> keys;  // by prefix ("" or "page-"): the image its colours come from
    std::set<std::string> busy;               // the prefixes whose reading is under way
};

// The prefix of the colours of the album or playlist on screen (page-art-1...).
inline constexpr char kPageArtworkPrefix[] = "page-";

// The hover logic (theme::HoverTracker) reads and writes the element through ReadRaw and WriteRaw, the XAML access
// without the hover; Read and Write go through the tracker.
class XamlTreeBackend final : public theme::TreeBackend, private theme::HoverTracker::Access {
public:
    // Takes one of Apple Music's windows from AMP.Utils.dll (xaml_host.hpp: Window::Current() is null in a WinUI 3
    // desktop app), the main one unless `role` says otherwise. False when the window is not up yet; the caller retries
    // on the next tick. Runs on that window's own interface thread: the mini player has a thread of its own, so it
    // gets a backend of its own (mini_player_theme.hpp).
    bool Attach(WindowRole role = WindowRole::Main);
    // From this thread, the application's resources are the very object the main window's backend writes: this
    // backend then leaves them alone (a mini player backend can only find that out; Attach logs nothing itself).
    bool SharesMainResources() const { return m_sharedResources; }

    std::vector<theme::NodeInfo> Snapshot() override;
    bool Alive(theme::NodeId id) override;
    bool Reparent(theme::NodeId id, theme::NodeId parent, int index) override;
    std::optional<theme::Value> Read(theme::NodeId id, std::string_view property) override;
    bool Write(theme::NodeId id, std::string_view property, const theme::Value& value) override;
    std::optional<theme::Value> ReadOriginal(theme::NodeId id, std::string_view property) override;
    void GaveBack(theme::NodeId id, std::string_view property) override;
    std::vector<std::string> ResourceKeys() override;
    std::optional<theme::Value> ReadResource(std::string_view key) override;
    bool WriteResource(std::string_view key, const theme::Value& value) override;
    bool SetDictionary(std::string_view xaml) override;
    // Follows the pointer with PointerEntered and PointerExited, attached once per element; the tokens are kept, and an
    // empty list removes both handlers. The handlers hold a weak pointer to m_alive: once the backend is destroyed
    // (maybe off the interface thread, so they are not removed then) they do nothing.
    bool SetHover(theme::NodeId id, const std::vector<theme::HoverValue>& values) override;
    // An Image named AmpifyDecorBefore or AmpifyDecorAfter in the panel's Children, which Snapshot and the Order
    // positions leave out. It is made again when the decor changes, and put back in place where it was.
    theme::DecorChange SetDecor(theme::NodeId id, theme::PseudoElement slot, const theme::Decor* decor) override;
    // The main window's bounds. Window.SizeChanged asks for a pass when one of the watched queries changes its answer.
    std::optional<theme::ViewSize> WindowSize() override;
    void WatchSize(const std::vector<theme::MediaQuery>& queries) override;
    void WatchSidebarOverPages(bool watch) override;
    void WatchStates(bool watch) override {
        if (watch && !m_watchStates) SchedulePass(0);  // this pass's snapshot was taken without them
        m_watchStates = watch;
    }
    // The element's place against its window's root (TransformToVisual) and its actual size, for the inspector.
    std::optional<theme::NodeBox> Box(theme::NodeId id) override;
    // Asks for a pass when a watched query changed its answer without the window changing size (the sidebar folded
    // or given back by apple.sidebar). Cheap: the periodic tick calls it.
    void RecheckMedia() noexcept { OnSizeChanged(); }

    // The light or dark mode of the app under the theme: RequestedTheme of the main window's content. Dark for a dark
    // scheme, Light for another scheme, the app's own value back with nullopt (no theme). After a resource change, the
    // mode goes to the other one and back, which makes the elements already on screen read the new values. True when
    // it did that switch.
    bool SetAppMode(std::optional<bool> dark);

    // True once the main window has closed: Apple Music is shutting down and its tree must not be touched any more.
    bool WindowClosed() const { return *m_closed; }
    // False while the main window is hidden (in the notification area) or minimized: nothing of it shows, so the
    // periodic work waits. True when it cannot be told.
    bool WindowShown() const;
    // The colours of the artwork playing; any thread may read them under their lock.
    std::shared_ptr<ArtworkColours> ArtworkSample() const { return m_artworkColours; }
    // How many state values hold a theme's value now (HoldThroughStates), and how many refused it, for the log.
    std::size_t HeldStateCount() const;
    std::size_t RefusedStateCount() const { return m_refusedStates; }
    const std::string& FirstStateRefusal() const { return m_firstRefusal; }  // the property and the reason, once
    // What made the last SetAppMode read the resources again, for the log.
    const std::string& RefreshReason() const { return m_refreshReason; }
    // What the caption buttons were set to before the first theme, once, for the log; empty after.
    std::string TakeCaptionNote() { return std::exchange(m_captionNote, std::string()); }
    std::string TakeArtworkNote() { return m_largeArtwork.TakeNote(); }

    // Runs `pass` on the interface thread soon after new elements may exist: a page navigated (Frame.Navigated, then
    // twice more as it fills in), rows realised while scrolling (ContainerContentChanging, ElementPrepared), a flyout,
    // menu or tooltip opening. A burst makes one pass, so a theme reaches new content without waiting for the tick.
    void WatchChanges(std::function<void()> pass);
    // An element entered the tree (XAML Diagnostics, xaml.watcher): the same pass, grouped with the other changes.
    void ElementAdded() noexcept { SchedulePass(0); }

private:
    winrt::Microsoft::UI::Xaml::DependencyObject Resolve(theme::NodeId id) const;

    std::optional<theme::Value> ReadRaw(theme::NodeId id, std::string_view property) override;
    // ReadRaw's readers, tried in this order: each knows its own property names (xaml_read.cpp).
    std::optional<theme::Value> ReadBrush(theme::NodeId id, const winrt::Microsoft::UI::Xaml::DependencyObject& object,
                                          std::string_view property);
    std::optional<theme::Value> ReadFont(theme::NodeId id, const winrt::Microsoft::UI::Xaml::DependencyObject& object,
                                         std::string_view property);
    std::optional<theme::Value> ReadThickness(theme::NodeId id, const winrt::Microsoft::UI::Xaml::DependencyObject& object,
                                              std::string_view property);
    std::optional<theme::Value> ReadSize(theme::NodeId id, const winrt::Microsoft::UI::Xaml::DependencyObject& object,
                                         std::string_view property);
    std::optional<theme::Value> ReadKeyword(theme::NodeId id, const winrt::Microsoft::UI::Xaml::DependencyObject& object,
                                            std::string_view property);
    std::optional<theme::Value> ReadPlace(theme::NodeId id, const winrt::Microsoft::UI::Xaml::DependencyObject& object,
                                          std::string_view property);
    std::optional<theme::Value> ReadMotion(theme::NodeId id, const winrt::Microsoft::UI::Xaml::DependencyObject& object,
                                           std::string_view property);
    std::optional<theme::Value> ReadOwn(theme::NodeId id, const winrt::Microsoft::UI::Xaml::DependencyObject& object,
                                        std::string_view property);
    theme::Value BrushValue(theme::NodeId id, std::string_view property,
                            const winrt::Microsoft::UI::Xaml::Media::Brush& value);
    theme::Value KeptValue(theme::NodeId id, std::string_view property,
                           const winrt::Windows::Foundation::IInspectable& value);
    bool WriteRaw(theme::NodeId id, std::string_view property, const theme::Value& value) override;
    bool ClearOwnValue(theme::NodeId id, const winrt::Microsoft::UI::Xaml::DependencyObject& object, std::string_view property);
    // WriteRaw's writers, tried in this order; nullopt: not this writer's property (xaml_properties.cpp).
    std::optional<bool> WriteBrush(theme::NodeId id, const winrt::Microsoft::UI::Xaml::DependencyObject& object,
                                   std::string_view property, const theme::Value& value);
    std::optional<bool> WriteFont(theme::NodeId id, const winrt::Microsoft::UI::Xaml::DependencyObject& object,
                                  std::string_view property, const theme::Value& value);
    std::optional<bool> WriteThickness(theme::NodeId id, const winrt::Microsoft::UI::Xaml::DependencyObject& object,
                                       std::string_view property, const theme::Value& value);
    std::optional<bool> WriteCorner(theme::NodeId id, const winrt::Microsoft::UI::Xaml::DependencyObject& object,
                                    std::string_view property, const theme::Value& value);
    std::optional<bool> WriteSize(theme::NodeId id, const winrt::Microsoft::UI::Xaml::DependencyObject& object,
                                  std::string_view property, const theme::Value& value);
    std::optional<bool> WriteShown(theme::NodeId id, const winrt::Microsoft::UI::Xaml::DependencyObject& object,
                                   std::string_view property, const theme::Value& value);
    std::optional<bool> WriteKeyword(theme::NodeId id, const winrt::Microsoft::UI::Xaml::DependencyObject& object,
                                     std::string_view property, const theme::Value& value);
    std::optional<bool> WritePlace(theme::NodeId id, const winrt::Microsoft::UI::Xaml::DependencyObject& object,
                                   std::string_view property, const theme::Value& value);
    std::optional<bool> WriteMotion(theme::NodeId id, const winrt::Microsoft::UI::Xaml::DependencyObject& object,
                                    std::string_view property, const theme::Value& value);
    std::optional<bool> WriteTransform(theme::NodeId id, const winrt::Microsoft::UI::Xaml::DependencyObject& object,
                                       std::string_view property, const theme::Value& value);
    std::optional<bool> WriteEffect(theme::NodeId id, const winrt::Microsoft::UI::Xaml::DependencyObject& object,
                                    std::string_view property, const theme::Value& value);
    std::optional<bool> WriteClip(theme::NodeId id, const winrt::Microsoft::UI::Xaml::DependencyObject& object,
                                  std::string_view property, const theme::Value& value);
    std::optional<bool> WriteTemplate(theme::NodeId id, const winrt::Microsoft::UI::Xaml::DependencyObject& object,
                                      std::string_view property, const theme::Value& value);

    // A value that is not text (a brush with an image, a control template) is kept as an object and given a token.
    // The same object always gets the same token, so a value read on each tick or each hover does not pile up.
    theme::Value Keep(winrt::Windows::Foundation::IInspectable object);
    winrt::Windows::Foundation::IInspectable Restore(const theme::Value& token) const;
    // The brush Ampify keeps for an application resource, recoloured to `color`; IsOwnBrush tells one of them.
    winrt::Microsoft::UI::Xaml::Media::SolidColorBrush OwnBrush(const std::string& key, winrt::Windows::UI::Color color);
    bool IsOwnBrush(const winrt::Windows::Foundation::IInspectable& value) const;

    // An object Ampify made from a value (a gradient, an image or acrylic brush, a transform, a shadow) reads back as
    // that value while the element still holds it: the next tick then finds the value in place and writes nothing.
    void Remember(theme::NodeId id, std::string_view property, const theme::Value& value,
                  winrt::Windows::Foundation::IInspectable object);
    std::optional<theme::Value> Recognize(theme::NodeId id, std::string_view property,
                                          const winrt::Windows::Foundation::IInspectable& object) const;
    void Forget(theme::NodeId id, std::string_view property);

    winrt::Microsoft::UI::Xaml::Media::Brush ArtworkBrush(const std::string& text);
    winrt::Microsoft::UI::Xaml::Controls::Border ArtworkDecor(const std::string& text, const wchar_t* name);
    bool DecorHolds(const winrt::Microsoft::UI::Xaml::FrameworkElement& owner) const;
    void FollowArtwork(const winrt::Microsoft::UI::Xaml::Controls::Image& image,
                       const winrt::Microsoft::UI::Xaml::Media::ImageSource& artwork);
    void SampleArtwork(const winrt::Microsoft::UI::Xaml::Controls::Image& image, const std::string& prefix = {});
    void FollowArtworkImage(const winrt::Microsoft::UI::Xaml::Controls::Image& image);
    void OnArtworkChanged() noexcept;

    void OnState(theme::NodeId id, theme::Trigger trigger, bool on) noexcept;
    void OnFocusLost(theme::NodeId id) noexcept;
    void FollowStates(theme::NodeId id, const winrt::Microsoft::UI::Xaml::UIElement& element);
    void Watch(const winrt::Microsoft::UI::Xaml::DependencyObject& object, theme::NodeId id, const std::string& type);
    bool WriteTransition(theme::NodeId id, const winrt::Microsoft::UI::Xaml::DependencyObject& object,
                         const theme::Value& value);
    bool WriteStoryTransform(theme::NodeId id, const winrt::Microsoft::UI::Xaml::UIElement& element,
                             const theme::Value& value, double ms);
    void CentreOn(theme::NodeId id, const winrt::Microsoft::UI::Xaml::UIElement& element,
                  const winrt::Microsoft::UI::Xaml::Media::CompositeTransform& transform) const;
    bool WriteAnimatedTransform(theme::NodeId id, const winrt::Microsoft::UI::Xaml::UIElement& element,
                                const theme::Value& value);
    // The theme's transform-origin of the element: RenderTransformOrigin, or the origin it refused (m_facadeOrigins).
    winrt::Windows::Foundation::Point OriginOf(theme::NodeId id, const winrt::Microsoft::UI::Xaml::UIElement& element) const;
    theme::DecorChange PlaceDecor(theme::NodeId id, theme::PseudoElement slot, const theme::Decor& decor);
    theme::DecorChange RemoveDecor(theme::NodeId id, theme::PseudoElement slot);
    void ForgetDecorMotion(theme::NodeId imageId);
    void WriteDecor(theme::NodeId imageId, const winrt::Microsoft::UI::Xaml::Controls::Panel& panel,
                    const theme::Decor& decor);
    void SchedulePass(int followUps) noexcept;
    // A value Apple sets again in code (a lyrics line's TextAlignment) asks for a pass at once, not at the next tick.
    void FollowAppleRewrites(theme::NodeId id, const winrt::Microsoft::UI::Xaml::DependencyObject& object,
                             std::string_view property);
    void RunPass() noexcept;
    bool LearnPopups(const winrt::Microsoft::UI::Xaml::XamlRoot& root);
    void WatchPopups(const winrt::Microsoft::UI::Xaml::UIElement& content);
    void CheckPopups() noexcept;
    std::vector<bool> MediaAnswers();
    void OnSizeChanged() noexcept;
    void RemoveHoverHandlers(theme::NodeId id);
    bool FollowHover(theme::NodeId id, const std::vector<theme::HoverValue>& values);  // SetHover, unguarded
    // Follows the states some value shows in, the element's own or those of the elements that watch it (a song's row
    // for `Grid#Row:hover Button`), and stops following the element once none does.
    void Refollow(theme::NodeId id);
    struct HoverHandlers;
    void FollowPointer(theme::NodeId id, const winrt::Microsoft::UI::Xaml::UIElement& element, HoverHandlers& handlers);
    // Looks at the cursor every 100 ms, for 5 s at most, and ends the element's :hover once it is out of its box.
    void WatchHoverEnd(theme::NodeId id);
    static void StopFollowingPointer(const winrt::Microsoft::UI::Xaml::UIElement& element, HoverHandlers& handlers);
    // The PointerOver and Pressed states of a control template write properties of its parts above any local value (a
    // theme's button background vanished under the pointer: greenroom's play button showed a black icon on black). The
    // brushes they read ({Control}{Property}PointerOver and Pressed) get the theme's value in that control's own
    // resources (Pressed: the :active value; null leaves Apple Music's); at-rest keys get `plain`. ReleaseStates undoes.
    void HoldThroughStates(theme::NodeId id, const winrt::Microsoft::UI::Xaml::DependencyObject& object,
                           std::string_view property, const winrt::Windows::Foundation::IInspectable& value,
                           const winrt::Windows::Foundation::IInspectable& pressed,
                           const winrt::Windows::Foundation::IInspectable& plain) noexcept;
    void ReleaseStates(theme::NodeId id, std::string_view property) noexcept;
    // A moved element leaves the visual states of the template it came from, and finds them again back in place.
    void HandStatesOver(const winrt::Microsoft::UI::Xaml::UIElement& element,
                        const winrt::Microsoft::UI::Xaml::Controls::Panel& from, std::uint32_t at,
                        const winrt::Microsoft::UI::Xaml::Controls::Panel& to) noexcept;
    void WithdrawStandIn(const winrt::Microsoft::UI::Xaml::UIElement& element,
                         const winrt::Microsoft::UI::Xaml::Controls::Panel& panel) noexcept;
    winrt::Microsoft::UI::Xaml::FrameworkElement StandIn(const winrt::Microsoft::UI::Xaml::FrameworkElement& element);
    bool IsStandIn(const winrt::Microsoft::UI::Xaml::DependencyObject& object) const;
    // `visibility: visible` held over Apple's own value, binding and visual states (xaml_states.cpp); ReleaseVisible lets
    // them decide again, and says whether a hold was there on `element` (any element when null): an id is an address,
    // so a new element may get the id of one that died holding. DropDeadHolds lets go of elements gone from the tree.
    void HoldVisible(theme::NodeId id, const winrt::Microsoft::UI::Xaml::UIElement& element, bool restart = true) noexcept;
    void OnHeldVisibility(theme::NodeId id) noexcept;
    void OnHeldStateChanged(theme::NodeId id) noexcept;
    bool ReleaseVisible(theme::NodeId id, const winrt::Microsoft::UI::Xaml::UIElement& element = nullptr) noexcept;
    void DropDeadHolds() noexcept;
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
    WindowRole m_role = WindowRole::Main;
    bool m_sharedResources = false;
    std::map<theme::NodeId, winrt::weak_ref<winrt::Microsoft::UI::Xaml::DependencyObject>> m_elements;
    // Elements of the last snapshot whose weak reference is dead as soon as taken (the Rectangle of the veil over the
    // bar's artwork): held until the next snapshot, or no value ever reached them.
    std::map<theme::NodeId, winrt::Microsoft::UI::Xaml::DependencyObject> m_held;
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
    // `overflow` and `clip-path`: the handler that keeps each clip rectangle the size of its element.
    std::map<theme::NodeId, winrt::Microsoft::UI::Xaml::FrameworkElement::SizeChanged_revoker> m_clipSizes;
    // `checked-color`: the colour written to each control, and what its ToggleButtonForegroundChecked keys held before
    // (null: no such key of its own), given back when the theme goes.
    struct CheckedKeys {
        std::string colour;
        std::vector<std::pair<std::wstring, winrt::Windows::Foundation::IInspectable>> originals;
        // A rebuilt control can take a gone one's address, so its id: the entry is only that control's.
        winrt::weak_ref<winrt::Microsoft::UI::Xaml::Controls::Control> control;
    };
    CheckedKeys* LiveChecked(theme::NodeId id, const winrt::Microsoft::UI::Xaml::Controls::Control& control);
    std::map<theme::NodeId, CheckedKeys> m_checked;
    bool WriteCheckedForeground(theme::NodeId id, const winrt::Microsoft::UI::Xaml::DependencyObject& object,
                                const theme::Value& value);
    theme::HoverTracker m_hover;
    // The events followed for the states an element's values use (FollowStates).
    struct HoverHandlers {
        winrt::weak_ref<winrt::Microsoft::UI::Xaml::UIElement> element;
        bool hover = false;  // PointerEntered, PointerExited
        winrt::event_token entered;
        winrt::event_token exited;
        winrt::Windows::Foundation::IInspectable moved{nullptr};  // a navigation entry's: off over its sub-entries
        winrt::Windows::Foundation::IInspectable lost{nullptr};   // PointerCanceled, PointerCaptureLost
        // An end the element's box still holds (a round artwork's corner): the cursor is looked at again until it is
        // out, as no other event may come.
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer exitCheck{nullptr};
        int exitChecks = 0;
        bool active = false;  // added with handledEventsToo: a button marks its pointer events handled
        winrt::Windows::Foundation::IInspectable pressed{nullptr};
        winrt::Windows::Foundation::IInspectable released{nullptr};
        bool focus = false;  // GotFocus, LostFocus, which bubble from the element's parts
        winrt::event_token gotFocus;
        winrt::event_token lostFocus;
        bool current = false;  // the visual states with a "Current" one (CurrentStates)
        winrt::weak_ref<winrt::Microsoft::UI::Xaml::VisualStateGroup> currentGroup;
        winrt::event_token currentChanged;
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
    struct VisibleHold {
        winrt::weak_ref<winrt::Microsoft::UI::Xaml::UIElement> element;  // Apple's: a weak reference resolves
        winrt::Microsoft::UI::Xaml::Media::Animation::Storyboard story{nullptr};  // Ampify's: held strongly
        std::int64_t token = 0;  // the Visibility callback, removed by token
        std::chrono::steady_clock::time_point begun{};
        // The state groups above it whose storyboards fade or collapse it: each change of theirs begins the hold again.
        std::vector<winrt::Microsoft::UI::Xaml::VisualStateGroup::CurrentStateChanged_revoker> stateChanges;
    };
    std::map<theme::NodeId, VisibleHold> m_visibleHolds;
    std::size_t m_refusedStates = 0;
    std::string m_firstRefusal;
    std::vector<theme::MediaQuery> m_mediaQueries;  // the theme's @media conditions (WatchSize)
    std::vector<bool> m_mediaAnswers;               // whether each matched at the last pass
    bool m_watchOverPages = false;                  // WatchSidebarOverPages: the theme dims the sidebar's pane
    bool m_watchStates = false;                     // WatchStates: the theme has :checked, :selected or :disabled
    bool m_idle = false;                            // :idle on this pass: nothing in the player
    // The controls whose IsChecked, IsOn or IsSelected asks for a pass when it changes (TakeStates).
    std::map<theme::NodeId, winrt::weak_ref<winrt::Microsoft::UI::Xaml::DependencyObject>> m_stateWatched;
    void TakeStates(const winrt::Microsoft::UI::Xaml::DependencyObject& object, theme::NodeInfo& node);
    void WatchDot(const winrt::Microsoft::UI::Xaml::FrameworkElement& dot, theme::NodeId id);
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
    std::map<std::string, winrt::Microsoft::UI::Xaml::Media::SolidColorBrush> m_ownBrushes;  // OwnBrush, by key
    std::map<std::wstring, winrt::Microsoft::UI::Xaml::FrameworkElement> m_standIns;  // StandIn, by class and name
    std::set<void*> m_standInIds;  // the same, for the walk to leave them out
    bool m_resourcesDirty = false;  // a resource changed: {ThemeResource} references must be resolved again
    std::string m_dirtyReason;      // the first change since the last refresh, and how many followed it
    int m_dirtyCount = 0;
    std::string m_refreshReason;
    // `background: artwork`: the image Apple Music shows for the song playing (Image#StaticArtwork in its play bar),
    // found again on each snapshot, and the brushes Ampify made from it, which follow it when the song changes.
    // A `content: artwork` decor also keeps its Border (`owner`, while m_decors holds it) and the layer that fades the
    // previous cover out; a `background: artwork` brush lives while a written value holds it, and changes at once.
    struct ArtworkLayer {
        winrt::Microsoft::UI::Xaml::Media::ImageBrush brush{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::Border owner{nullptr};
        ArtworkFade fade;
    };
    winrt::Microsoft::UI::Xaml::Media::ImageSource m_artwork{nullptr};
    std::vector<ArtworkLayer> m_artworkLayers;
    LargeArtwork m_largeArtwork;  // what those brushes paint: the cover decoded large, not Apple's thumbnail
    MovingArtworks m_immersion;  // the decors of content: immersion, the song's artwork as a moving wash
    std::shared_ptr<ArtworkColours> m_artworkColours = std::make_shared<ArtworkColours>();
    // That image is followed between passes: a new song's artwork is read as soon as it opens, not at the next tick. A
    // reading asked while another runs is tried again shortly (m_artworkRetry).
    winrt::weak_ref<winrt::Microsoft::UI::Xaml::Controls::Image> m_artworkImage;
    winrt::event_token m_artworkOpened{};
    std::int64_t m_artworkSourceToken = 0;
    winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_artworkRetry{nullptr};
    // WatchChanges: the pass to run, the elements followed (frames and lists), the timer that groups their events, and
    // the passes still due after a navigation.
    std::function<void()> m_pass;
    std::map<theme::NodeId, winrt::weak_ref<winrt::Microsoft::UI::Xaml::DependencyObject>> m_watched;
    struct RewriteFollow {
        winrt::weak_ref<winrt::Microsoft::UI::Xaml::Controls::TextBlock> text;  // Apple's: a weak reference resolves
        std::int64_t token = 0;  // the TextAlignment callback, removed by token
    };
    std::map<theme::NodeId, RewriteFollow> m_rewritesFollowed;
    void StopFollowingRewrites(theme::NodeId id) noexcept;
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
    // Elements Apple Music animates itself refuse the facade: their transform transition (ms) eases the element's
    // CompositeTransform instead, through a Storyboard (WriteStoryTransform). Both objects are Ampify's, held strongly.
    std::map<theme::NodeId, double> m_storyDurations;
    std::map<theme::NodeId, winrt::Microsoft::UI::Xaml::Media::CompositeTransform> m_storyTransforms;
    std::map<theme::NodeId, winrt::Microsoft::UI::Xaml::Media::Animation::Storyboard> m_stories;
    ThemeAnimations m_animations;  // `animation`
    // The ::before and ::after images, by panel and place. The panel is Apple Music's (a weak reference resolves), the
    // image Ampify's: held strongly, and released here, on the interface thread (docs/development/xaml-lessons.md).
    struct DecorImage {
        winrt::weak_ref<winrt::Microsoft::UI::Xaml::Controls::Panel> panel;
        winrt::Microsoft::UI::Xaml::FrameworkElement image{nullptr};  // an Image, or the Grid of bars(n)
        theme::NodeId imageId = 0;  // what Resolve answers to, so WriteRaw sets the image's size, place and animation
        theme::Decor decor;
    };
    std::map<std::pair<theme::NodeId, theme::PseudoElement>, DecorImage> m_decors;
    AudioBars m_bars;  // the decors made by `content: bars(n)`, moved with the music
    PaneCorners m_paneCorners;  // what a rounded SplitView pane holds, clipped to its corners
    GradientFit m_gradientFit;  // the theme's gradients, fitted to the shape of what they paint
    ElementBlur m_blur;         // the elements a theme blurs (`filter: blur()`)
    WidthGlides m_widthGlides;  // the elements whose widths glide (`transition: width`)
};

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
