#pragma once

// AMK.player on Windows: reads the state from Apple's player (AMP.Services.Player, in AppleMusic.exe only), then from
// the Windows media controls (SMTC), and sends the commands along the route chosen by core/player_model.cpp (Apple's
// player when allowed, SMTC, then a media key). Also the queue capability (core/player_queue.hpp). Every method
// runs on the JavaScript thread. The journal says once which source answers, and why a source is not used.
//
// Settings (settings.txt):
//   player.apple = true          read Apple's player in AppleMusic.exe (false: SMTC only, a kill switch)
//   player.appleCommands = false send play/pause/next/previous through Apple's player (off until the PC confirms the
//                                parameters of those methods; SMTC and the media keys answer meanwhile)
//   player.queueProbe = true     describe AMP.Services.PlayQueue once in the journal (read only)

#include <functional>
#include <memory>
#include <set>
#include <string>

#include "../core/host.hpp"
#include "../core/journal.hpp"
#include "../core/player_model.hpp"
#include "../core/player_queue.hpp"
#include "../core/settings.hpp"

namespace amk::win {

class AmpPlayer;
class SmtcSession;

struct PlayerBridgeOptions {
    // Sends a media key (VK_MEDIA_PLAY_PAUSE, VK_MEDIA_NEXT_TRACK, VK_MEDIA_PREV_TRACK). Empty: SendInput. The tests
    // replace it.
    std::function<bool(unsigned short virtualKey)> keys;
    // Apple's player is first asked once the app shows a window, so that the kit never makes Apple build its player
    // early. The tests, which have no window, turn it off.
    bool waitForWindow = true;
    unsigned retryMs = 5000;  // between two activation attempts
    int maxAttempts = 24;     // 2 minutes, then SMTC for good (until the player answers again)
};

class PlayerBridge : public QueueHost {
public:
    using Options = PlayerBridgeOptions;

    PlayerBridge(std::string process, Settings& settings, Journal& journal, Options options = {});
    ~PlayerBridge() override;
    PlayerBridge(const PlayerBridge&) = delete;
    PlayerBridge& operator=(const PlayerBridge&) = delete;

    PlayerSnapshot Snapshot();
    bool Control(PlayerCommand command, int64_t seekMs);

    QueueSnapshot Queue() override;
    QueueResult AddToQueue(const std::string& trackId, QueuePosition position) override;
    QueueResult RemoveFromQueue(const std::string& itemId) override;

    // For the tests: how many activation attempts of Apple's player were made.
    int AppleAttempts() const { return m_appleAttempts; }

private:
    void TryApple();
    void TrySmtc();
    void SayOnce(const std::string& key, const std::string& line);

    std::string m_process;
    Settings& m_settings;
    Journal& m_journal;
    Options m_options;
    std::set<std::string> m_said;  // journal lines written once

    std::unique_ptr<AmpPlayer> m_apple;
    std::unique_ptr<SmtcSession> m_smtc;

    int m_appleAttempts = 0;
    bool m_appleGivenUp = false;
    unsigned long long m_appleNextTry = 0;
    unsigned long long m_smtcNextTry = 0;
    bool m_probed = false;
    std::string m_lastSource = "?";
    AmpTimeUnit m_unit = AmpTimeUnit::Unknown;
    PlayerSnapshot m_last;
};

}  // namespace amk::win
