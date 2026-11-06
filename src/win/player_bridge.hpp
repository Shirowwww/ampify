#pragma once

// Ampify.player on Windows: reads the state from Apple's player (AMP.Services.Player, in AppleMusic.exe only), then from
// the Windows media controls (SMTC), and sends the commands along the route chosen by src/core/player_model.cpp (Apple's
// player when allowed, SMTC, then a media key). Also the queue capability (src/core/player_queue.hpp). Every method
// runs on the JavaScript thread. The log says once which source answers, and why a source is not used.
//
// Settings (settings.txt):
//   player.apple = true          read Apple's player in AppleMusic.exe (false: SMTC only, a kill switch)
//   player.appleCommands = false send play/pause/next/previous through Apple's player (off until the PC confirms the
//                                parameters of those methods; SMTC and the media keys answer meanwhile)
//
// The queue is Apple's (AMP.Services.PlayQueue, in AppleMusic.exe), read and changed on the interface thread, which
// owns its collections. Without Apple's player it holds the current track only.

#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "../core/host.hpp"
#include "../core/journal.hpp"
#include "../core/lyrics_host.hpp"
#include "../core/now_playing.hpp"
#include "../core/player_model.hpp"
#include "../core/player_queue.hpp"
#include "../core/settings.hpp"

namespace ampify::win {

class AmpPlayer;
class SmtcSession;

struct PlayerBridgeOptions {
    // Sends a media key (VK_MEDIA_PLAY_PAUSE, VK_MEDIA_NEXT_TRACK, VK_MEDIA_PREV_TRACK). Empty: SendInput. The tests
    // replace it.
    std::function<bool(unsigned short virtualKey)> keys;
    // Apple's player is first asked once the app shows a window, so that Ampify never makes Apple build its player
    // early. The tests, which have no window, turn it off.
    bool waitForWindow = true;
    unsigned retryMs = 5000;  // between two activation attempts
    int maxAttempts = 24;     // 2 minutes, then SMTC for good (until the player answers again)
    // Runs work on Apple Music's interface thread and waits for it, at most `timeoutMs`. Empty:
    // RunOnUiThreadAndWait (src/win/ui_thread.hpp). The tests run it in place.
    std::function<bool(std::function<void()> work, unsigned timeoutMs)> onInterfaceThread;
    // The start in ms of the line Apple's lyrics view lights up, or nullopt. Empty: the reader that
    // InstallLyricsLineStart installed (src/win/player_buttons.hpp), run on the interface thread. The tests replace it.
    std::function<std::optional<int64_t>()> lyricsLineStart;
};

class PlayerBridge : public QueueHost, public LyricsHost {
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

    // Apple's lyrics of the current track, from Apple's player only (SMTC has none).
    std::optional<RawLyrics> Lyrics() override;

    // For the tests: how many activation attempts of Apple's player were made.
    int AppleAttempts() const { return m_appleAttempts; }

private:
    void TryApple();
    void TrySmtc();
    void SayOnce(const std::string& key, const std::string& line);
    bool OnInterfaceThread(std::function<void()> work);
    std::optional<int64_t> LyricsLineStart();
    void FollowLyricsLine(PlayerSnapshot& snapshot, int64_t nowMs);
    void PublishFeed(const PlayerSnapshot& snapshot);
    // Apple's whole queue, or nullopt with `failure`.
    std::optional<std::vector<AmpQueueEntry>> AppleQueue(std::string& failure, bool& cut);

    std::string m_process;
    Settings& m_settings;
    Journal& m_journal;
    Options m_options;
    std::set<std::string> m_said;  // log lines written once

    std::unique_ptr<AmpPlayer> m_apple;
    std::unique_ptr<SmtcSession> m_smtc;

    int m_appleAttempts = 0;
    bool m_appleGivenUp = false;
    unsigned long long m_appleNextTry = 0;
    unsigned long long m_smtcNextTry = 0;
    std::string m_lastSource = "?";
    AmpTimeUnit m_unit = AmpTimeUnit::Unknown;
    PositionClock m_clock;  // Apple's position, renewed about once a second, counted on in between
    int64_t m_lineStartMs = -1;  // the start of the lyrics line Apple's view lit up last, -1 without one
    std::string m_feedKey;       // the song the published lyrics belong to (src/core/now_playing.hpp)
    std::shared_ptr<const ampify::Lyrics> m_feedLyrics;
    uint64_t m_coverLoggedItem = 0;  // the queue item whose raw cover went to the log
    int m_coverLogs = 0;
    PlayerSnapshot m_last;
};

}  // namespace ampify::win
