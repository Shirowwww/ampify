#include "player_bridge.hpp"

#include <windows.h>

#include "amp_player.hpp"
#include "smtc_session.hpp"
#include "winrt_abi.hpp"

namespace ampify::win {

namespace {

constexpr unsigned long long kSmtcRetryMs = 30'000;

// A media key is handled by the system and by Apple Music, as on a keyboard. It never changes the volume.
bool SendMediaKey(unsigned short key) {
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = key;
    inputs[0].ki.dwFlags = KEYEVENTF_EXTENDEDKEY;
    inputs[1] = inputs[0];
    inputs[1].ki.dwFlags = KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP;
    return SendInput(2, inputs, sizeof(INPUT)) == 2;
}

BOOL CALLBACK FindVisibleWindow(HWND window, LPARAM found) {
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (process == GetCurrentProcessId() && IsWindowVisible(window)) {
        *reinterpret_cast<bool*>(found) = true;
        return FALSE;
    }
    return TRUE;
}

bool ProcessShowsAWindow() {
    bool found = false;
    EnumWindows(FindVisibleWindow, reinterpret_cast<LPARAM>(&found));
    return found;
}

std::string YesNo(bool value) { return value ? "yes" : "no"; }

std::string Describe(const PlayerSnapshot& snapshot) {
    std::string track = "none";
    if (snapshot.track) track = "\"" + snapshot.track->title + "\" (" + snapshot.track->variantId + ")";
    return "playing=" + YesNo(snapshot.playing) + ", position=" + std::to_string(snapshot.positionMs) +
           " ms, duration=" + std::to_string(snapshot.durationMs) + " ms, track=" + track;
}

}  // namespace

PlayerBridge::PlayerBridge(std::string process, Settings& settings, Journal& journal, Options options)
    : m_process(std::move(process)), m_settings(settings), m_journal(journal), m_options(std::move(options)),
      m_apple(std::make_unique<AmpPlayer>()), m_smtc(std::make_unique<SmtcSession>()) {
    if (!m_options.keys) m_options.keys = SendMediaKey;
}

PlayerBridge::~PlayerBridge() = default;

void PlayerBridge::SayOnce(const std::string& key, const std::string& line) {
    if (m_said.insert(key).second) m_journal.Write(line);
}

void PlayerBridge::TryApple() {
    if (m_process != "ui") {
        SayOnce("apple-engine", "Player: AMP.Services.Player lives in AppleMusic.exe, it is not used here; "
                                "state and commands go through SMTC, then the media keys");
        return;
    }
    if (!m_settings.GetBool("player.apple", true)) {
        SayOnce("apple-off", "Player: AMP.Services.Player turned off by the player.apple setting; SMTC instead");
        return;
    }
    if (m_apple->Active() || m_appleGivenUp) return;
    unsigned long long now = GetTickCount64();
    if (now < m_appleNextTry) return;
    if (m_options.waitForWindow && !ProcessShowsAWindow()) return;

    if (!rt::EnsureMultithreaded()) {
        SayOnce("apple-sta", "Player: the JavaScript thread is already in an STA apartment; calls are made from "
                             "this thread");
    }
    ++m_appleAttempts;
    std::string failure;
    if (!m_apple->Activate(failure)) {
        m_appleNextTry = now + m_options.retryMs;
        if (m_appleAttempts == 1) {
            m_journal.Write("Player: AMP.Services.Player not reachable yet (" + failure +
                            "), trying again every " + std::to_string(m_options.retryMs / 1000) + " s");
        }
        if (m_appleAttempts >= m_options.maxAttempts) {
            m_appleGivenUp = true;
            m_journal.Write("Player: AMP.Services.Player not activated (" + failure +
                            "); falling back to Windows media controls (SMTC), then to the media "
                            "keys");
        }
        return;
    }

    m_journal.Write("Player: AMP.Services.Player activated (" + m_apple->ActivationPath() + ", thread " +
                    (rt::EnsureMultithreaded() ? "MTA" : "STA") + ", PropertyChanged notifications: " +
                    YesNo(m_apple->Subscribed()) + ")");
    if (std::optional<AmpReading> first = m_apple->Read()) {
        m_journal.Write("Player: first state read by AMP.Services.Player: " +
                        Describe(SnapshotFromAmp(*first, DetectAmpTimeUnit(first->durationRaw, std::nullopt))));
    }
    if (!m_probed && m_settings.GetBool("player.queueProbe", true)) {
        m_probed = true;
        m_journal.Write(m_apple->ProbeQueue());
    }
}

void PlayerBridge::TrySmtc() {
    if (m_smtc->Started()) return;
    unsigned long long now = GetTickCount64();
    if (now < m_smtcNextTry) return;
    std::string failure;
    if (!m_smtc->Start(failure)) {
        m_smtcNextTry = now + kSmtcRetryMs;
        SayOnce("smtc-missing", "Player: " + failure + "; the media keys still work");
    }
}

PlayerSnapshot PlayerBridge::Snapshot() {
    rt::EnsureMultithreaded();
    TryApple();
    TrySmtc();

    std::optional<SmtcReading> smtc = m_smtc->Read();
    if (m_smtc->Found()) {
        SayOnce("smtc-found", "Player: Apple Music SMTC session found (" + m_smtc->AppId() + ")");
    }

    PlayerSnapshot snapshot;
    const bool appleWasActive = m_apple->Active();
    std::optional<AmpReading> apple = appleWasActive ? m_apple->Read() : std::nullopt;
    if (appleWasActive && !apple) {
        // The object stopped answering (Apple Music rebuilt its player): SMTC meanwhile, and a new activation later.
        m_journal.Write("Player: AMP.Services.Player no longer answers; falling back to SMTC, trying again in " +
                        std::to_string(m_options.retryMs / 1000) + " s");
        m_appleAttempts = 0;
        m_appleNextTry = GetTickCount64() + m_options.retryMs;
    }
    if (apple) {
        // The unit of Apple's position is not written in the winmd: SMTC's length of the same track settles it.
        std::optional<int64_t> smtcMs;
        if (smtc && !smtc->title.empty() && smtc->title == apple->title && smtc->endTicks > smtc->startTicks) {
            smtcMs = (smtc->endTicks - smtc->startTicks) / 10'000;
        }
        AmpTimeUnit unit = DetectAmpTimeUnit(apple->durationRaw, smtcMs);
        if (unit != AmpTimeUnit::Unknown && (m_unit == AmpTimeUnit::Unknown || smtcMs)) {
            if (unit != m_unit) {
                m_journal.Write(std::string("Player: time unit of AMP.Services.Player: ") +
                                AmpTimeUnitName(unit) +
                                (smtcMs ? " (from the SMTC duration)" : " (from the duration alone)"));
            }
            m_unit = unit;
        }
        snapshot = SnapshotFromAmp(*apple, m_unit);
        if (snapshot.track && !snapshot.track->artwork.empty()) {
            SayOnce("apple-artwork", "Player: cover read from Apple's player (" + snapshot.track->artwork + ")");
        }
        if (snapshot.lyricsAvailable) SayOnce("apple-lyrics", "Player: lyrics read from Apple's player");
        if (m_apple->Notifications() > 0) {
            SayOnce("apple-notified", "Player: Apple sends its PropertyChanged notifications (PlayerState, Player)");
        }
    } else if (smtc) {
        snapshot = SnapshotFromSmtc(*smtc);
    }

    if (snapshot.source != m_lastSource) {
        m_journal.Write("Player: state read from " +
                        (snapshot.source.empty() ? std::string("no source") : snapshot.source));
        m_lastSource = snapshot.source;
    }
    m_last = snapshot;
    return snapshot;
}

bool PlayerBridge::Control(PlayerCommand command, int64_t seekMs) {
    rt::EnsureMultithreaded();
    RouteAvailability available;
    available.apple = m_apple->Active() && m_settings.GetBool("player.appleCommands", false);
    available.smtc = m_smtc->Found();
    available.mediaKeys = true;
    CommandPlan plan = PlanCommand(command, seekMs, m_last, available);
    if (plan.alreadyDone) return true;

    for (const CommandStep& step : plan.steps) {
        bool done = false;
        switch (step.route) {
            case CommandRoute::Apple: done = m_apple->Command(command); break;
            case CommandRoute::Smtc: done = m_smtc->Command(command, plan.seekMs); break;
            case CommandRoute::MediaKey:
                switch (step.key) {
                    case CommandStep::Key::PlayPause: done = m_options.keys(VK_MEDIA_PLAY_PAUSE); break;
                    case CommandStep::Key::Next: done = m_options.keys(VK_MEDIA_NEXT_TRACK); break;
                    case CommandStep::Key::Previous: done = m_options.keys(VK_MEDIA_PREV_TRACK); break;
                    case CommandStep::Key::None: break;
                }
                break;
        }
        std::string what = std::string(CommandName(command)) + " via " + RouteName(step.route);
        if (done) {
            SayOnce("ok " + what, "Player: command " + what + " accepted");
            return true;
        }
        SayOnce("ko " + what, "Player: command " + what + " refused, next route");
    }
    SayOnce(std::string("none ") + CommandName(command),
            std::string("Player: command ") + CommandName(command) +
                " not possible now (no route accepted it)");
    return false;
}

std::optional<RawLyrics> PlayerBridge::Lyrics() {
    if (!m_apple->Active()) return std::nullopt;
    return m_apple->Lyrics();
}

QueueSnapshot PlayerBridge::Queue() {
    QueueSnapshot queue;
    PlayerSnapshot snapshot = Snapshot();
    queue.source = snapshot.source;
    queue.complete = false;
    queue.reason = "only the current track is known: the interfaces of Apple's queue (AMP.Services.PlayQueue) "
                   "are not mapped yet";
    if (snapshot.track) {
        QueueItem item;
        item.id = snapshot.track->queueItemId.empty() ? snapshot.track->id : snapshot.track->queueItemId;
        item.title = snapshot.track->title;
        item.artist = snapshot.track->artist;
        item.durationMs = snapshot.durationMs;
        item.section = QueueSection::Current;
        queue.items.push_back(std::move(item));
    }
    return queue;
}

QueueResult PlayerBridge::AddToQueue(const std::string&, QueuePosition) {
    return {false, "adding to the queue is not possible yet: the parameters of "
                   "IPlayer.StartOrQueueLibraryPlaybackAsync are not mapped"};
}

QueueResult PlayerBridge::RemoveFromQueue(const std::string&) {
    return {false, "removing from the queue is not possible yet: the parameters of "
                   "IPlayQueue.DeletePlayQueueItemsAsync are not mapped"};
}

}  // namespace ampify::win
