#include "player_bridge.hpp"

#include <windows.h>

#include <atomic>
#include <memory>
#include <tuple>

#include "../core/settings_catalog.hpp"
#include "../core/ui_watchdog.hpp"
#include "amp_player.hpp"
#include "player_buttons.hpp"
#include "smtc_session.hpp"
#include "ui_thread.hpp"
#include "winrt_abi.hpp"

namespace ampify::win {

namespace {

std::atomic<ReadLyricsLineStartFn> g_readLineStart{nullptr};

constexpr unsigned long long kSmtcRetryMs = 30'000;
constexpr unsigned kInterfaceTimeoutMs = 2000;
constexpr DWORD kQueueChangeTimeoutMs = 3000;

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
    if (!ToggleOf(m_settings, kPlayerAppleKey)) {
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
        snapshot = FromApple(*apple, smtc ? &*smtc : nullptr);
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

// The unit of Apple's times is not written in the winmd: SMTC's length of the same track settles it, and a duration
// alone only guesses it until then.
void PlayerBridge::LearnTimeUnit(const AmpReading& apple, const SmtcReading* smtc) {
    std::optional<int64_t> smtcMs = smtc ? SmtcLengthMsOf(*smtc, apple.title) : std::nullopt;
    AmpTimeUnit unit = DetectAmpTimeUnit(apple.durationRaw, smtcMs);
    if (unit == AmpTimeUnit::Unknown || (m_unit != AmpTimeUnit::Unknown && !smtcMs)) return;
    if (unit != m_unit) {
        m_journal.Write(std::string("Player: time unit of AMP.Services.Player: ") + AmpTimeUnitName(unit) +
                        (smtcMs ? " (from the SMTC duration)" : " (from the duration alone)"));
    }
    m_unit = unit;
}

PlayerSnapshot PlayerBridge::FromApple(const AmpReading& apple, const SmtcReading* smtc) {
    LearnTimeUnit(apple, smtc);
    PlayerSnapshot snapshot = SnapshotFromAmp(apple, m_unit);
    if (snapshot.track) {
        const bool appleGave = snapshot.positionMs > 0;
        const int64_t now = static_cast<int64_t>(GetTickCount64());
        snapshot.positionMs =
            m_clock.Next(TrackKey(snapshot), snapshot.playing, snapshot.positionMs, snapshot.durationMs, now);
        if (!appleGave) FollowLyricsLine(snapshot, now);
        if (smtc) FillArtistAlbumFromSmtc(*snapshot.track, *smtc);
    }
    LogCover(apple);
    if (snapshot.track && !snapshot.track->artwork.empty()) {
        SayOnce("apple-artwork", "Player: cover read from Apple's player (" + snapshot.track->artwork + ")");
    }
    if (snapshot.lyricsAvailable) SayOnce("apple-lyrics", "Player: lyrics read from Apple's player");
    if (m_apple->Notifications() > 0) {
        SayOnce("apple-notified", "Player: Apple sends its PropertyChanged notifications (PlayerState, Player)");
    }
    return snapshot;
}

// The raw cover of the first tracks, since its address format may change with Apple Music.
void PlayerBridge::LogCover(const AmpReading& apple) {
    if (!apple.hasItem || apple.queueItemId == m_coverLoggedItem || m_coverLogs >= 3) return;
    m_coverLoggedItem = apple.queueItemId;
    m_coverLogs++;
    const ArtworkReading& cover = apple.artwork;
    m_journal.Write("Player: cover of \"" + apple.title + "\": address \"" + cover.uri + "\", format \"" +
                    cover.format + "\", crop \"" + cover.cropCode + "\", " +
                    std::to_string(static_cast<int>(cover.width)) + "x" +
                    std::to_string(static_cast<int>(cover.height)) + ", colour " +
                    (cover.hasColor ? ArtworkColorHex(cover) : std::string("none")) + ", lyrics " +
                    (apple.lyricsAvailable ? "yes" : "not yet"));
}

// For the interface thread's karaoke (the `lyrics` node): the position as just read, and the song's lyrics parsed once.
void PlayerBridge::PublishFeed(const PlayerSnapshot& snapshot) {
    NowPlaying now;
    now.trackKey = TrackKey(snapshot);
    now.playing = snapshot.playing;
    now.positionMs = snapshot.positionMs;
    now.atMs = static_cast<int64_t>(GetTickCount64());
    now.moving = snapshot.playing && (snapshot.source != "apple" || m_clock.Moving(now.atMs));
    if (now.trackKey != m_feedKey) {
        m_feedKey = now.trackKey;
        m_feedLyrics.reset();
    }
    // A new text replaces the parsed copy: the word-timed lyrics may come after those timed by line.
    if (std::optional<RawLyrics> raw = snapshot.lyricsAvailable ? Lyrics() : std::nullopt) {
        if (std::shared_ptr<const ampify::Lyrics> parsed = SharedLyrics(*raw)) m_feedLyrics = std::move(parsed);
    }
    now.lyrics = m_feedLyrics;
    PublishNowPlaying(std::move(now));
}

bool PlayerBridge::Control(PlayerCommand command, int64_t seekMs) {
    rt::EnsureMultithreaded();
    RouteAvailability available;
    // Seek needs no opt-in: SMTC is the only other route, and Apple Music ignores it there.
    available.apple = m_apple->Active() &&
                      (command == PlayerCommand::Seek || ToggleOf(m_settings, kPlayerAppleCommandsKey));
    available.smtc = m_smtc->Found();
    available.mediaKeys = true;
    CommandPlan plan = PlanCommand(command, seekMs, m_last, available);
    if (plan.alreadyDone) return true;

    for (const CommandStep& step : plan.steps) {
        bool done = false;
        switch (step.route) {
            case CommandRoute::Apple: done = m_apple->Command(command, plan.seekMs); break;
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
            if (command == PlayerCommand::Seek) m_clock.Seeked(plan.seekMs, static_cast<int64_t>(GetTickCount64()));
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

void InstallLyricsLineStart(ReadLyricsLineStartFn read) {
    g_readLineStart = read;
}

std::optional<int64_t> PlayerBridge::LyricsLineStart() {
    if (m_options.lyricsLineStart) return m_options.lyricsLineStart();
    ReadLyricsLineStartFn read = g_readLineStart.load();
    if (!read) return std::nullopt;
    auto ms = std::make_shared<long long>(-1);  // shared: the work may outlive a timed-out wait
    if (!OnInterfaceThread([read, ms] { *ms = read(); }) || *ms < 0) return std::nullopt;
    return static_cast<int64_t>(*ms);
}

// When Apple's lyrics view lights up another line, the song is at that line's start: the clock starts again from
// there, so a jump made in Apple Music, a song repeated or a slow start are caught up at the next line.
void PlayerBridge::FollowLyricsLine(PlayerSnapshot& snapshot, int64_t nowMs) {
    std::optional<int64_t> start = LyricsLineStart();
    if (!start) {
        m_lineStartMs = -1;  // the view closed: its next line is taken as it comes
        return;
    }
    if (*start == m_lineStartMs) return;
    m_lineStartMs = *start;
    if (snapshot.durationMs > 0 && *start > snapshot.durationMs) return;  // a line of the song before
    m_clock.Seeked(*start, nowMs);
    snapshot.positionMs = *start;
    SayOnce("lyrics-line", "Player: position set by the line Apple's lyrics view lights up");
}

bool PlayerBridge::OnInterfaceThread(std::function<void()> work) {
    // Named for the freeze log: a slow player command on the interface thread is then not "no Ampify work".
    std::function<void()> named = [work = std::move(work)] {
        ampify::ActivityScope activity("player command");
        work();
    };
    if (m_options.onInterfaceThread) return m_options.onInterfaceThread(std::move(named), kInterfaceTimeoutMs);
    return RunOnUiThreadAndWait(std::move(named), kInterfaceTimeoutMs);
}

std::optional<std::vector<AmpQueueEntry>> PlayerBridge::AppleQueue(std::string& failure, bool& cut) {
    cut = false;
    rt::EnsureMultithreaded();
    TryApple();
    if (!m_apple->Active()) {
        failure = "Apple's player is not reached";
        return std::nullopt;
    }
    // Shared with the work, which may still run after a timeout.
    struct Read {
        std::optional<std::vector<AmpQueueEntry>> entries;
        std::string failure;
        bool cut = false;
    };
    auto read = std::make_shared<Read>();
    if (!OnInterfaceThread([read] { read->entries = ReadAppleQueue(read->failure, read->cut); })) {
        failure = "Apple Music's interface thread did not answer";
        return std::nullopt;
    }
    cut = read->cut;
    // Checked in Apple Music 1.1792: PlayQueue.Sections stays empty until the queue panel first shows.
    if (read->entries && read->entries->empty()) {
        read->entries.reset();
        read->failure = "Apple Music builds its queue the first time its queue panel opens";
    }
    if (!read->entries) failure = read->failure;
    else SayOnce("queue-apple", "Player: queue read through AMP.Services.PlayQueue (" +
                                    std::to_string(read->entries->size()) + " entries)");
    return read->entries;
}

QueueSnapshot PlayerBridge::Queue() {
    QueueSnapshot queue;
    PlayerSnapshot snapshot = Snapshot();
    queue.source = snapshot.source;
    std::string failure;
    bool cut = false;
    if (std::optional<std::vector<AmpQueueEntry>> entries = AppleQueue(failure, cut)) {
        queue.complete = !cut;
        if (cut) queue.reason = "the first " + std::to_string(kMaxQueueEntries) + " entries of a longer queue";
        queue.source = "apple";
        for (const AmpQueueEntry& entry : *entries) queue.items.push_back(QueueItemFromAmp(entry));
        return queue;
    }
    queue.complete = false;
    queue.reason = "only the current track is known: " + failure;
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

QueueResult PlayerBridge::AddToQueue(const std::string& trackId, QueuePosition) {
    if (!IsCatalogId(trackId)) return {false, "not an Apple Music catalog id (digits only): " + trackId};
    // Tried in Apple Music 1.1792 (knowledge/apple-music-queue.md): IPlayer.StartOrQueueLibraryPlaybackAsync completes
    // without queueing a catalog song, whatever the action, so nothing is called.
    return {false, "adding to the queue is not possible: Apple Music 1.1792 ignores the songs Ampify gives its player"};
}

QueueResult PlayerBridge::RemoveFromQueue(const std::string& itemId) {
    std::string failure;
    bool cut = false;
    std::optional<std::vector<AmpQueueEntry>> entries = AppleQueue(failure, cut);
    if (!entries) return {false, "the queue cannot be changed: " + failure};
    const AmpQueueEntry* entry = nullptr;
    for (const AmpQueueEntry& candidate : *entries) {
        if (std::to_string(candidate.id) == itemId) entry = &candidate;
    }
    if (!entry) {
        std::string where = cut ? "the first " + std::to_string(kMaxQueueEntries) + " entries of the queue" : "the queue";
        return {false, "no entry " + itemId + " in " + where};
    }
    QueueSection section = QueueSectionFromKind(entry->sectionKind);
    if (section != QueueSection::NextAdded && section != QueueSection::Next && section != QueueSection::Autoplay) {
        return {false, "only an entry still to come can be removed, not one in \"" +
                           std::string(QueueSectionName(section)) + "\""};
    }

    struct Start {
        rt::Ref operation;
        std::string failure;
    };
    auto start = std::make_shared<Start>();
    uint64_t id = entry->id;
    if (!OnInterfaceThread([start, id] { start->operation = StartAppleQueueRemove(id, start->failure); })) {
        return {false, "Apple Music's interface thread did not answer"};
    }
    if (!start->operation) return {false, start->failure};
    if (!rt::Wait(start->operation, kQueueChangeTimeoutMs)) {
        return {false, "Apple's queue did not confirm the removal within 3 s"};
    }
    return {true, ""};
}

}  // namespace ampify::win
