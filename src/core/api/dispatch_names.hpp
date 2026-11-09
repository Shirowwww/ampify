#pragma once
// The entries of the prelude's `dispatch` object that the kernel calls, one name each: the C++ side (the extension
// runtime, the natives, the Windows services and the interface) and the JavaScript side (`dispatch.<name> = ...` in
// src/core/prelude/) must spell them the same, which tests/api_module_tests.cpp checks against the prelude.

namespace ampify::dispatch {

inline constexpr char kReady[] = "ready";              // once, on the first pump: Ampify.ready resolves
inline constexpr char kStop[] = "stop";                // the element stops: its onStop handlers run
inline constexpr char kPlayer[] = "player";            // a playback event and its state
inline constexpr char kKey[] = "key";                  // a global shortcut, by its id
inline constexpr char kClick[] = "click";              // a control of Ampify.ui, by its id
inline constexpr char kTimer[] = "timer";              // a timer, by its id
inline constexpr char kHttp[] = "http";                // an answer of the off-thread request channel
inline constexpr char kAudioFrame[] = "audioFrame";    // a frame of Ampify.audio's analysis is ready
inline constexpr char kKitDone[] = "kitDone";          // a run of the ampify command line ended
inline constexpr char kLink[] = "link";                // a message from another element
inline constexpr char kChange[] = "change";            // a control's value changed (toggle, slider, field, lyrics)
inline constexpr char kMenuClick[] = "menuClick";      // an entry of a menu or a submenu
inline constexpr char kPopupClose[] = "popupClose";    // a popup was closed by the user
inline constexpr char kPanelClose[] = "panelClose";    // a panel was closed by the user
inline constexpr char kFullscreenClose[] = "fullscreenClose";  // the full-screen view of a session closed
inline constexpr char kTray[] = "tray";                // the notification-area icon or its menu
inline constexpr char kMidi[] = "midi";                // a MIDI message an element subscribed to
inline constexpr char kPageShow[] = "pageShow";        // the kernel opened one of the element's pages, by its path

inline constexpr const char* kKernelEntries[] = {
    kReady, kStop, kPlayer, kKey, kClick, kTimer, kHttp, kAudioFrame, kKitDone,
    kLink,  kChange, kMenuClick, kPopupClose, kPanelClose, kFullscreenClose, kTray, kMidi, kPageShow,
};

}  // namespace ampify::dispatch
