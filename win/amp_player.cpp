// The ABI of AMP.Services.Player follows AMExclusive (https://github.com/AntiOblivionis/AMExclusive, MIT,
// src/ApplePlayback.h), reused with its owner's permission; the slots are checked against knowledge/winmd.

#include "amp_player.hpp"

#include <cstdio>
#include <vector>

#include "../core/amp_abi.hpp"
#include "win_common.hpp"

namespace ampify::win {

namespace {

namespace slot = amp::slot;

const GUID kIidPlayerStatics = rt::GuidOf(amp::kIidPlayerStatics);
const GUID kIidPlayer = rt::GuidOf(amp::kIidPlayer);
const GUID kIidPlayerState = rt::GuidOf(amp::kIidPlayerState);
const GUID kIidNowPlayingItem = rt::GuidOf(amp::kIidNowPlayingItem);
const GUID kIidNotifyPropertyChanged = rt::GuidOf(amp::kIidNotifyPropertyChanged);
const GUID kIidPropertyChangedHandler = rt::GuidOf(amp::kIidPropertyChangedHandler);

// INotifyPropertyChanged (Microsoft.UI.Xaml.Data): add_PropertyChanged 6 (handler, token*), remove_PropertyChanged 7.
constexpr int kAddPropertyChangedSlot = 6;
constexpr int kRemovePropertyChangedSlot = 7;

constexpr DWORD kCommandTimeoutMs = 3000;
constexpr unsigned long long kStringsEveryMs = 5000;

// Interfaces any WinRT object may list in GetIids without telling anything about Apple's types. The queue probe
// ignores them; what is left must be exactly the one interface it looks for.
const char* const kWellKnownIids[] = {
    "00000000-0000-0000-C000-000000000046",  // IUnknown
    "AF86E2E0-B12D-4C6A-9C5A-D7AA65101E90",  // IInspectable
    "00000035-0000-0000-C000-000000000046",  // IActivationFactory
    "00000003-0000-0000-C000-000000000046",  // IMarshal
    "00000038-0000-0000-C000-000000000046",  // IWeakReferenceSource
    "94EA2B94-E9CC-49E0-C0FF-EE64CA8F5B90",  // IAgileObject
    "96369F54-8EB6-48F0-ABCE-C1B211E627C3",  // IStringable
    "30D5A829-7FA4-4026-83BB-D75BAE4EA99E",  // IClosable
    "90B17601-B065-586E-83D9-9ADC3A695284",  // Microsoft.UI.Xaml.Data.INotifyPropertyChanged
    "CF75D69C-F2F4-486B-B302-BB4C09BAEBFA",  // Windows.UI.Xaml.Data.INotifyPropertyChanged
};

std::vector<GUID> UnknownIids(const rt::Ref& object) {
    std::vector<GUID> unknown;
    for (const GUID& iid : rt::Iids(object)) {
        bool known = false;
        for (const char* text : kWellKnownIids) known = known || IsEqualGUID(iid, rt::GuidOf(text));
        if (!known) unknown.push_back(iid);
    }
    return unknown;
}

std::string ListIids(const std::vector<GUID>& iids) {
    std::string out;
    for (const GUID& iid : iids) out += (out.empty() ? "" : ", ") + rt::GuidText(iid);
    return out.empty() ? "aucune" : out;
}

std::string Hex(HRESULT hr) {
    char text[16];
    std::snprintf(text, sizeof text, "0x%08lX", static_cast<unsigned long>(hr));
    return text;
}

}  // namespace

AmpPlayer::AmpPlayer() = default;

AmpPlayer::~AmpPlayer() { Drop(); }

void AmpPlayer::Drop() {
    Unsubscribe(m_playerToken, m_playerNotify);
    Unsubscribe(m_stateToken, m_stateNotify);
    m_stateObject.Reset();
    m_item.Reset();
    m_player.Reset();
}

bool AmpPlayer::Activate(std::string& failure) {
    if (m_player) return true;
    rt::Ref statics;
    rt::Activation activation = rt::GetActivationFactory(L"AMP.Services.Player", kIidPlayerStatics, statics);
    if (FAILED(activation.result)) {
        failure = "fabrique introuvable (" + activation.how + ", code " + Hex(activation.result) + ")";
        return false;
    }
    // IPlayerStatics.get_Current (slot 6): Apple's own player, the one the interface uses. It is never constructed.
    rt::Ref current;
    HRESULT hr = rt::CallSlot(statics, slot::kPlayerStaticsCurrent, current.Put());
    if (FAILED(hr) || !current) {
        failure = FAILED(hr) ? "Player.Current a échoué (code " + Hex(hr) + ")" : "Player.Current est encore vide";
        return false;
    }
    // The identifier of IPlayer is a hash of its methods (core/amp_abi.hpp): if Apple changed them, this fails.
    if (!rt::Query(current, kIidPlayer, m_player)) {
        failure = "IPlayer absent : cette version d'Apple Music a changé l'interface du lecteur";
        return false;
    }
    m_how = activation.how;
    Subscribe(m_player, m_playerToken, m_playerNotify);
    return true;
}

void AmpPlayer::Subscribe(const rt::Ref& object, std::optional<int64_t>& token, rt::Ref& source) {
    Unsubscribe(token, source);
    rt::Ref notify;
    if (!rt::Query(object, kIidNotifyPropertyChanged, notify)) return;
    auto* handler = new rt::ChangeCounter(kIidPropertyChangedHandler, m_changes);
    int64_t value = 0;
    HRESULT hr = rt::CallSlot(notify, kAddPropertyChangedSlot, static_cast<IUnknown*>(handler), &value);
    handler->Release();  // the event source keeps its own reference
    if (SUCCEEDED(hr)) {
        token = value;
        source = notify;
    }
}

void AmpPlayer::Unsubscribe(std::optional<int64_t>& token, rt::Ref& source) {
    if (token && source) rt::CallSlot(source, kRemovePropertyChangedSlot, *token);
    token.reset();
    source.Reset();
}

std::optional<AmpReading> AmpPlayer::Read() {
    if (!m_player) return std::nullopt;
    AmpReading reading;

    // IPlayer.get_PlayerState (slot 7), then IPlayerState.get_IsPlaying (25) and get_IsPaused (24).
    rt::Ref stateObject;
    HRESULT hr = rt::CallSlot(m_player, slot::kPlayerPlayerState, stateObject.Put());
    if (FAILED(hr)) {
        Drop();
        return std::nullopt;
    }
    rt::Ref state;
    if (rt::Query(stateObject, kIidPlayerState, state)) {
        unsigned char playing = 0, paused = 0;  // WinRT Boolean: one byte
        rt::CallSlot(state, slot::kStateIsPlaying, &playing);
        rt::CallSlot(state, slot::kStateIsPaused, &paused);
        reading.playing = playing != 0;
        reading.paused = paused != 0;
        if (!stateObject.SameObject(m_stateObject)) {
            m_stateObject = stateObject;
            Subscribe(stateObject, m_stateToken, m_stateNotify);
        }
    }

    // IPlayer.get_NowPlayingItem (slot 6). Null while nothing is loaded.
    rt::Ref itemObject;
    rt::Ref item;
    if (SUCCEEDED(rt::CallSlot(m_player, slot::kPlayerNowPlayingItem, itemObject.Put())) &&
        rt::Query(itemObject, kIidNowPlayingItem, item)) {
        reading.hasItem = true;
        unsigned char placeholder = 0;
        rt::CallSlot(item, slot::kItemIsPlaceholder, &placeholder);
        reading.placeholder = placeholder != 0;
        rt::CallSlot(item, slot::kItemCurrentPosition, &reading.positionRaw);
        rt::CallSlot(item, slot::kItemDuration, &reading.durationRaw);
        rt::CallSlot(item, slot::kItemAudioFormat, &reading.audioFormat);
        rt::CallSlot(item, slot::kItemPlayQueueItemId, &reading.queueItemId);

        // The strings are copied by Apple's getters while the interface thread may replace them: they are read
        // only when the item changes, and every 5 s for late metadata.
        unsigned long long now = GetTickCount64();
        bool changed = !itemObject.SameObject(m_item) || reading.queueItemId != m_itemQueueId;
        if (changed || now - m_stringsAt >= kStringsEveryMs) {
            m_title = rt::ReadString(item, slot::kItemName);
            m_artist = rt::ReadString(item, slot::kItemArtist);
            m_album = rt::ReadString(item, slot::kItemAlbum);
            m_item = itemObject;
            m_itemQueueId = reading.queueItemId;
            m_stringsAt = now;
        }
        reading.title = m_title;
        reading.artist = m_artist;
        reading.album = m_album;
    } else {
        m_item.Reset();
    }
    return reading;
}

bool AmpPlayer::Command(PlayerCommand command) {
    if (!m_player) return false;
    int method = 0;
    switch (command) {
        case PlayerCommand::Play: method = slot::kPlayerPlayAsync; break;               // IPlayer.PlayAsync
        case PlayerCommand::Pause: method = slot::kPlayerPauseAsync; break;             // IPlayer.PauseAsync
        case PlayerCommand::Next: method = slot::kPlayerSkipNextAsync; break;           // IPlayer.SkipNextAsync
        case PlayerCommand::Previous: method = slot::kPlayerSkipPreviousAsync; break;   // IPlayer.SkipPreviousAsync
        case PlayerCommand::Seek: return false;  // SetPositionAsync: the type of its position is not known
    }
    // The winmd list gives the names, not the parameters: these four take none and return an asynchronous action,
    // which is why player.appleCommands is off by default until the PC confirms them.
    rt::Ref operation;
    HRESULT hr = rt::CallSlot(m_player, method, operation.Put());
    if (FAILED(hr)) return false;
    return !operation || rt::Wait(operation, kCommandTimeoutMs);
}

std::string AmpPlayer::ProbeQueue() {
    const std::string prefix = "File d'attente (sonde, lecture seule) : ";
    const GUID activationFactory = rt::GuidOf("00000035-0000-0000-C000-000000000046");
    rt::Ref factory;
    rt::Activation activation = rt::GetActivationFactory(L"AMP.Services.PlayQueue", activationFactory, factory);
    if (FAILED(activation.result)) {
        return prefix + "fabrique de AMP.Services.PlayQueue introuvable (code " + Hex(activation.result) + ")";
    }

    std::vector<GUID> staticsIids = UnknownIids(factory);
    if (staticsIids.size() != 1) {
        return prefix + "IPlayQueueStatics pas identifiée sans doute possible (interfaces inconnues de la fabrique : " +
               ListIids(staticsIids) + ")";
    }
    rt::Ref statics, queue;
    // IPlayQueueStatics.get_Current (slot 6), its only method.
    if (!rt::Query(factory, staticsIids[0], statics) ||
        FAILED(rt::CallSlot(statics, slot::kPlayQueueStaticsCurrent, queue.Put())) || !queue) {
        return prefix + "PlayQueue.Current n'a rien rendu";
    }
    std::string className = rt::ClassName(queue);
    if (className != amp::kPlayQueueClass) return prefix + "Current n'est pas une PlayQueue (" + className + ")";

    std::vector<GUID> queueIids = UnknownIids(queue);
    std::string found = prefix + "IPlayQueueStatics = " + rt::GuidText(staticsIids[0]);
    if (queueIids.size() != 1) return found + " ; IPlayQueue pas identifiée (inconnues : " + ListIids(queueIids) + ")";
    found += " ; IPlayQueue = " + rt::GuidText(queueIids[0]);

    // IPlayQueue.get_Sections (slot 9): a collection whose element type the probe describes, without reading it.
    rt::Ref playQueue, sections;
    if (!rt::Query(queue, queueIids[0], playQueue) ||
        FAILED(rt::CallSlot(playQueue, slot::kPlayQueueSections, sections.Put())) || !sections) {
        return found + " ; Sections vide";
    }
    return found + " ; Sections = " + rt::ClassName(sections) + " (interfaces : " + ListIids(rt::Iids(sections)) + ")";
}

}  // namespace ampify::win
