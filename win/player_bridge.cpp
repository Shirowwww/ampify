#include "player_bridge.hpp"

#include <windows.h>

#include "amp_player.hpp"
#include "smtc_session.hpp"
#include "winrt_abi.hpp"

namespace amk::win {

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

std::string YesNo(bool value) { return value ? "oui" : "non"; }

std::string Describe(const PlayerSnapshot& snapshot) {
    std::string track = "aucun";
    if (snapshot.track) track = "« " + snapshot.track->title + " » (" + snapshot.track->variantId + ")";
    return "en lecture=" + YesNo(snapshot.playing) + ", position=" + std::to_string(snapshot.positionMs) +
           " ms, durée=" + std::to_string(snapshot.durationMs) + " ms, morceau=" + track;
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
        SayOnce("apple-engine", "Lecteur : AMP.Services.Player vit dans AppleMusic.exe, il n'est pas utilisé ici ; "
                                "état et commandes par SMTC, puis par les touches multimédias");
        return;
    }
    if (!m_settings.GetBool("player.apple", true)) {
        SayOnce("apple-off", "Lecteur : AMP.Services.Player désactivé par le réglage player.apple ; SMTC à la place");
        return;
    }
    if (m_apple->Active() || m_appleGivenUp) return;
    unsigned long long now = GetTickCount64();
    if (now < m_appleNextTry) return;
    if (m_options.waitForWindow && !ProcessShowsAWindow()) return;

    if (!rt::EnsureMultithreaded()) {
        SayOnce("apple-sta", "Lecteur : le fil JavaScript est déjà dans un appartement STA ; appels faits depuis "
                             "ce fil");
    }
    ++m_appleAttempts;
    std::string failure;
    if (!m_apple->Activate(failure)) {
        m_appleNextTry = now + m_options.retryMs;
        if (m_appleAttempts == 1) {
            m_journal.Write("Lecteur : AMP.Services.Player pas encore joignable (" + failure +
                            "), nouvel essai toutes les " + std::to_string(m_options.retryMs / 1000) + " s");
        }
        if (m_appleAttempts >= m_options.maxAttempts) {
            m_appleGivenUp = true;
            m_journal.Write("Lecteur : AMP.Services.Player non activé (" + failure +
                            ") ; repli sur les contrôles multimédias de Windows (SMTC), puis sur les touches "
                            "multimédias");
        }
        return;
    }

    m_journal.Write("Lecteur : AMP.Services.Player activé (" + m_apple->ActivationPath() + ", fil " +
                    (rt::EnsureMultithreaded() ? "MTA" : "STA") + ", notifications PropertyChanged : " +
                    YesNo(m_apple->Subscribed()) + ")");
    if (std::optional<AmpReading> first = m_apple->Read()) {
        m_journal.Write("Lecteur : premier état lu par AMP.Services.Player : " +
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
        SayOnce("smtc-missing", "Lecteur : " + failure + " ; les touches multimédias restent possibles");
    }
}

PlayerSnapshot PlayerBridge::Snapshot() {
    rt::EnsureMultithreaded();
    TryApple();
    TrySmtc();

    std::optional<SmtcReading> smtc = m_smtc->Read();
    if (m_smtc->Found()) {
        SayOnce("smtc-found", "Lecteur : session SMTC d'Apple Music trouvée (" + m_smtc->AppId() + ")");
    }

    PlayerSnapshot snapshot;
    const bool appleWasActive = m_apple->Active();
    std::optional<AmpReading> apple = appleWasActive ? m_apple->Read() : std::nullopt;
    if (appleWasActive && !apple) {
        // The object stopped answering (Apple Music rebuilt its player): SMTC meanwhile, and a new activation later.
        m_journal.Write("Lecteur : AMP.Services.Player ne répond plus ; repli sur SMTC, nouvel essai dans " +
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
                m_journal.Write(std::string("Lecteur : unité de temps d'AMP.Services.Player : ") +
                                AmpTimeUnitName(unit) +
                                (smtcMs ? " (d'après la durée SMTC)" : " (d'après la durée seule)"));
            }
            m_unit = unit;
        }
        snapshot = SnapshotFromAmp(*apple, m_unit);
        if (m_apple->Notifications() > 0) {
            SayOnce("apple-notified", "Lecteur : Apple envoie ses notifications PropertyChanged (PlayerState, Player)");
        }
    } else if (smtc) {
        snapshot = SnapshotFromSmtc(*smtc);
    }

    if (snapshot.source != m_lastSource) {
        m_journal.Write("Lecteur : état lu par " +
                        (snapshot.source.empty() ? std::string("aucune source") : snapshot.source));
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
        std::string what = std::string(CommandName(command)) + " par " + RouteName(step.route);
        if (done) {
            SayOnce("ok " + what, "Lecteur : commande " + what + " acceptée");
            return true;
        }
        SayOnce("ko " + what, "Lecteur : commande " + what + " refusée, voie suivante");
    }
    SayOnce(std::string("none ") + CommandName(command),
            std::string("Lecteur : commande ") + CommandName(command) +
                " impossible maintenant (aucune voie ne l'a acceptée)");
    return false;
}

QueueSnapshot PlayerBridge::Queue() {
    QueueSnapshot queue;
    PlayerSnapshot snapshot = Snapshot();
    queue.source = snapshot.source;
    queue.complete = false;
    queue.reason = "seule la piste en cours est connue : les interfaces de la file d'Apple (AMP.Services.PlayQueue) "
                   "ne sont pas encore relevées";
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
    return {false, "ajouter à la file n'est pas encore possible : les paramètres de "
                   "IPlayer.StartOrQueueLibraryPlaybackAsync ne sont pas relevés"};
}

QueueResult PlayerBridge::RemoveFromQueue(const std::string&) {
    return {false, "retirer de la file n'est pas encore possible : les paramètres de "
                   "IPlayQueue.DeletePlayQueueItemsAsync ne sont pas relevés"};
}

}  // namespace amk::win
