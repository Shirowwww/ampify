#pragma once

// Apple's own player object, AMP.Services.Player, reached from inside AppleMusic.exe through its private WinRT
// interfaces (slots and identifiers in core/amp_abi.hpp, with their evidence). Read: playing or paused, position,
// duration, title, artist, album, audio format, queue entry, cover and its colour, lyrics. Commands: PlayAsync, PauseAsync, SkipNextAsync,
// SkipPreviousAsync, only when the setting player.appleCommands allows them (see win/player_bridge.cpp). Never the
// volume, never the DRM.
//
// Threads: every method runs on the JavaScript thread, in the multithreaded apartment. AMExclusive reads the same
// objects on the interface thread; Ampify reads them from its own thread first, as the plan of record asks, and
// keeps the reads short: scalars on every poll, strings only when the track changes or every 5 s. If the objects
// refuse a thread other than the interface's, activation fails with a clear log line and SMTC takes over.
//
// Not used in AMPLibraryAgent.exe: the player object lives in the interface process, and activating it elsewhere
// could build a second player.

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../core/host.hpp"
#include "../core/lyrics.hpp"
#include "../core/player_model.hpp"
#include "winrt_abi.hpp"

namespace ampify::win {

class AmpPlayer {
public:
    AmpPlayer();
    ~AmpPlayer();
    AmpPlayer(const AmpPlayer&) = delete;
    AmpPlayer& operator=(const AmpPlayer&) = delete;

    // Reaches Player.Current. True once it answers; `failure` says why not, in simple French.
    bool Activate(std::string& failure);
    bool Active() const { return static_cast<bool>(m_player); }
    const std::string& ActivationPath() const { return m_how; }
    // True when the player accepted our PropertyChanged handlers (PlayerState and Player).
    bool Subscribed() const { return m_playerToken.has_value() || m_stateToken.has_value(); }
    // How many property change notifications Apple sent so far.
    unsigned Notifications() const { return m_changes->load(); }

    // The current state. nullopt when the player stopped answering: the object is dropped, and Active() is false.
    std::optional<AmpReading> Read();

    // The lyrics of the current track as Apple holds them, read when the track changes and again every few seconds
    // while absent (a bounded number of times). nullopt when there are none (yet).
    const std::optional<RawLyrics>& Lyrics() const { return m_lyrics; }

    // Play, Pause, Next, Previous through IPlayer. False for Seek, or when the call fails or does not complete
    // within 3 s.
    bool Command(PlayerCommand command);

private:
    void Subscribe(const rt::Ref& object, std::optional<int64_t>& token, rt::Ref& source);
    void Unsubscribe(std::optional<int64_t>& token, rt::Ref& source);
    void Drop();

    rt::Ref m_player;  // AMP.Services.IPlayer
    std::string m_how;
    std::shared_ptr<std::atomic<unsigned>> m_changes = std::make_shared<std::atomic<unsigned>>(0);

    rt::Ref m_playerNotify;  // INotifyPropertyChanged of the player
    std::optional<int64_t> m_playerToken;
    rt::Ref m_stateObject;  // the PlayerState object we subscribed to
    rt::Ref m_stateNotify;
    std::optional<int64_t> m_stateToken;

    rt::Ref m_item;  // the NowPlayingItem whose strings are cached
    uint64_t m_itemQueueId = 0;
    unsigned long long m_stringsAt = 0;
    std::string m_title, m_artist, m_album, m_subtitle;
    ArtworkReading m_artwork;

    std::optional<RawLyrics> m_lyrics;  // of the item in m_item
    int m_lyricsTries = 0;
    unsigned long long m_lyricsNextTry = 0;
};

// Apple's play queue (AMP.Services.PlayQueue). Its collections feed Apple's queue panel and belong to the interface
// thread: call these there (win/ui_thread.hpp). Each fails cleanly, `failure` saying why in plain English.
inline constexpr size_t kMaxQueueEntries = 500;

// Every entry, section by section (history, current, added, next, autoplay), at most kMaxQueueEntries; `cut` is set
// when the queue holds more.
std::optional<std::vector<AmpQueueEntry>> ReadAppleQueue(std::string& failure, bool& cut);
// Starts removing one entry (IPlayQueue.DeletePlayQueueItemsAsync): its IAsyncAction, empty on failure.
rt::Ref StartAppleQueueRemove(uint64_t itemId, std::string& failure);

}  // namespace ampify::win
