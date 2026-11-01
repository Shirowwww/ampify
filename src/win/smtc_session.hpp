#pragma once

// The Apple Music session of the Windows media controls (Windows.Media.Control,
// GlobalSystemMediaTransportControlsSessionManager): a public, documented API, used when Apple's player object is not
// reachable (AMPLibraryAgent.exe, a new Apple Music version, a refusal). It reads the state, the title, artist and
// album, the position and the length, and sends play, pause, next, previous and seek, as the volume flyout of Windows
// does. It never changes the volume.
//
// Vtable slots and interface identifiers: Windows SDK, windows.media.control.h (the same values as the windows-rs
// bindings, crates/libs/windows/src/Windows/Media/Control/mod.rs). Every call runs on the JavaScript thread and never
// blocks it for long: the asynchronous reads are started on one poll and collected on a later one.

#include <cstdint>
#include <optional>
#include <string>

#include "../core/host.hpp"
#include "../core/player_model.hpp"
#include "winrt_abi.hpp"

namespace ampify::win {

class SmtcSession {
public:
    // Starts the request for the session manager. False when the API is missing (Windows before 10 1809).
    bool Start(std::string& failure);
    bool Started() const { return m_started; }
    // True once the Apple Music session was found.
    bool Found() const { return static_cast<bool>(m_session); }
    const std::string& AppId() const { return m_appId; }

    // Advances the pending operations, finds the session again every 2 s while it is missing, and reads the state.
    // nullopt while there is no Apple Music session.
    std::optional<SmtcReading> Read();

    // TryPlayAsync, TryPauseAsync, TrySkipNextAsync, TrySkipPreviousAsync, TryChangePlaybackPositionAsync. True when
    // Apple Music accepted the command within 3 s.
    bool Command(PlayerCommand command, int64_t seekMs);

private:
    void FindSession();

    bool m_started = false;
    rt::Ref m_request;  // IAsyncOperation<GlobalSystemMediaTransportControlsSessionManager>
    rt::Ref m_manager;
    rt::Ref m_session;
    std::string m_appId;
    unsigned long long m_searchedAt = 0;

    rt::Ref m_properties;  // IAsyncOperation<...MediaProperties>, pending
    unsigned long long m_propertiesAt = 0;
    std::string m_title, m_artist, m_album;
};

}  // namespace ampify::win
