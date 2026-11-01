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
const GUID kIidPlayerState = rt::GuidOf(amp::kIidPlayerState);
const GUID kIidNotifyPropertyChanged = rt::GuidOf(amp::kIidNotifyPropertyChanged);
const GUID kIidPropertyChangedHandler = rt::GuidOf(amp::kIidPropertyChangedHandler);

// INotifyPropertyChanged (Microsoft.UI.Xaml.Data): add_PropertyChanged 6 (handler, token*), remove_PropertyChanged 7.
constexpr int kAddPropertyChangedSlot = 6;
constexpr int kRemovePropertyChangedSlot = 7;

// The first identifier of `iids` that `object` answers to: one per Apple Music version (src/core/amp_abi.hpp).
template <size_t N>
bool QueryAny(const rt::Ref& object, const char* const (&iids)[N], rt::Ref& out) {
    for (const char* iid : iids) {
        if (rt::Query(object, rt::GuidOf(iid), out)) return true;
    }
    return false;
}

constexpr DWORD kCommandTimeoutMs = 3000;
constexpr unsigned long long kStringsEveryMs = 5000;
// Apple may fetch the lyrics some seconds after a track starts: asked again every 3 s for about a minute.
constexpr unsigned long long kLyricsRetryMs = 3000;
// Apple Music may fetch the lyrics late (when its lyrics panel opens, for example): look for them for 10 minutes.
constexpr int kLyricsMaxTries = 200;

// Windows.UI.Color, which IArtwork.get_BGColor writes through its out pointer (four bytes, by value).
struct WinRtColor {
    uint8_t a = 0, r = 0, g = 0, b = 0;
};

// The cover of an INowPlayingItem (1.1792). Every call survives a null object, a refused QueryInterface or a failed
// getter: the field stays empty. Getters only, never a put_*.
ArtworkReading ReadArtwork(const rt::Ref& item) {
    ArtworkReading out;
    rt::Ref object, artwork;
    // INowPlayingItem.get_Artwork (slot 11), then IArtwork.
    if (FAILED(rt::CallSlot(item, slot::kItemArtwork, object.Put())) || !object) return out;
    if (!rt::Query(object, rt::GuidOf(amp::kIidArtwork), artwork)) return out;

    WinRtColor color;
    out.hasColor = SUCCEEDED(rt::CallSlot(artwork, slot::kArtworkBgColor, &color));  // slot 6
    out.alpha = color.a;
    out.red = color.r;
    out.green = color.g;
    out.blue = color.b;
    out.format = rt::ReadString(artwork, slot::kArtworkFormat);      // slot 10
    out.cropCode = rt::ReadString(artwork, slot::kArtworkCropCode);  // slot 17
    double height = 0, width = 0;
    if (SUCCEEDED(rt::CallSlot(artwork, slot::kArtworkHeight, &height))) out.height = height;  // slot 12
    if (SUCCEEDED(rt::CallSlot(artwork, slot::kArtworkWidth, &width))) out.width = width;      // slot 14

    // get_Uri (slot 16) gives a Windows.Foundation.Uri; IUriRuntimeClass.get_AbsoluteUri is its slot 6.
    rt::Ref uriObject, uri;
    if (SUCCEEDED(rt::CallSlot(artwork, slot::kArtworkUri, uriObject.Put())) && uriObject &&
        rt::Query(uriObject, rt::GuidOf(amp::kIidUriRuntimeClass), uri)) {
        out.uri = rt::ReadString(uri, slot::kUriAbsoluteUri);
    }
    return out;
}

// The lyrics of an INowPlayingItem (1.1792), or nullopt while there are none.
std::optional<RawLyrics> ReadLyrics(const rt::Ref& item) {
    rt::Ref object, info;
    // INowPlayingItem.get_LyricsInfo (slot 46), then ILyricsInfo.
    if (FAILED(rt::CallSlot(item, slot::kItemLyricsInfo, object.Put())) || !object) return std::nullopt;
    if (!rt::Query(object, rt::GuidOf(amp::kIidLyricsInfo), info)) return std::nullopt;
    unsigned char have = 0;  // HaveLyrics (slot 12): a WinRT Boolean, one byte
    if (FAILED(rt::CallSlot(info, slot::kLyricsHaveLyrics, &have)) || have == 0) return std::nullopt;

    RawLyrics raw;
    raw.ttml = rt::ReadString(info, slot::kLyricsTtml);          // GetTTML, slot 14
    raw.staticText = rt::ReadString(info, slot::kLyricsStatic);  // GetStaticLyrics, slot 13
    raw.language = rt::ReadString(info, slot::kLyricsLanguage);  // GetLanguage, slot 15
    if (raw.ttml.empty() && raw.staticText.empty()) return std::nullopt;
    return raw;
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
        failure = "factory not found (" + activation.how + ", code " + Hex(activation.result) + ")";
        return false;
    }
    // IPlayerStatics.get_Current (slot 6): Apple's own player, the one the interface uses. It is never constructed.
    rt::Ref current;
    HRESULT hr = rt::CallSlot(statics, slot::kPlayerStaticsCurrent, current.Put());
    if (FAILED(hr) || !current) {
        failure = FAILED(hr) ? "Player.Current failed (code " + Hex(hr) + ")" : "Player.Current is still empty";
        return false;
    }
    // The identifier of IPlayer is a hash of its methods (src/core/amp_abi.hpp): if Apple changed them, this fails.
    if (!QueryAny(current, amp::kIidsPlayer, m_player)) {
        failure = "IPlayer missing: this version of Apple Music changed the player interface";
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
        QueryAny(itemObject, amp::kIidsNowPlayingItem, item)) {
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
        bool stringsDue = changed || now - m_stringsAt >= kStringsEveryMs;
        if (changed) {
            m_lyrics.reset();
            m_lyricsTries = 0;
            m_lyricsNextTry = 0;
        }
        bool lyricsDue = !m_lyrics && m_lyricsTries < kLyricsMaxTries && now >= m_lyricsNextTry;

        // The cover and the lyrics only exist on the 1.1792 interface: the older identifier is not asked for them.
        rt::Ref newest;
        if (stringsDue || lyricsDue) rt::Query(itemObject, rt::GuidOf(amp::kIidsNowPlayingItem[0]), newest);
        if (stringsDue) {
            m_title = rt::ReadString(item, slot::kItemName);
            m_artist = rt::ReadString(item, slot::kItemArtist);
            m_album = rt::ReadString(item, slot::kItemAlbum);
            m_subtitle = rt::ReadString(item, slot::kItemSubtitle);
            m_artwork = newest ? ReadArtwork(newest) : ArtworkReading{};
            m_item = itemObject;
            m_itemQueueId = reading.queueItemId;
            m_stringsAt = now;
        }
        if (lyricsDue) {
            ++m_lyricsTries;
            m_lyricsNextTry = now + kLyricsRetryMs;
            if (newest) m_lyrics = ReadLyrics(newest);
            else m_lyricsTries = kLyricsMaxTries;
        }
        reading.title = m_title;
        reading.artist = m_artist;
        reading.album = m_album;
        reading.subtitle = m_subtitle;
        reading.artwork = m_artwork;
        reading.lyricsAvailable = m_lyrics.has_value();
    } else {
        m_item.Reset();
        m_artwork = ArtworkReading{};
        m_lyrics.reset();
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

namespace {

// IPlayQueue of PlayQueue.Current, or empty with `failure`.
rt::Ref CurrentPlayQueue(std::string& failure) {
    rt::Ref statics, current, queue;
    rt::Activation activation =
        rt::GetActivationFactory(L"AMP.Services.PlayQueue", rt::GuidOf(amp::kIidPlayQueueStatics), statics);
    if (FAILED(activation.result)) {
        failure = "AMP.Services.PlayQueue not found (code " + Hex(activation.result) + ")";
        return {};
    }
    // IPlayQueueStatics.get_Current (slot 6).
    if (FAILED(rt::CallSlot(statics, slot::kPlayQueueStaticsCurrent, current.Put())) || !current) {
        failure = "PlayQueue.Current is empty";
        return {};
    }
    // The identifier is a hash of the interface's methods (src/core/amp_abi.hpp): a changed queue fails here.
    if (!rt::Query(current, rt::GuidOf(amp::kIidPlayQueue), queue)) {
        failure = "IPlayQueue missing: this version of Apple Music changed the queue interface";
        return {};
    }
    return queue;
}

// The elements of an IVector<IComponentModel>, which Apple's observable vectors also answer. At most `limit`;
// nullopt when the collection is not such a vector.
std::optional<std::vector<rt::Ref>> ComponentModels(const rt::Ref& collection, size_t limit) {
    std::vector<rt::Ref> out;
    rt::Ref vector;
    UINT32 size = 0;
    if (!rt::Query(collection, rt::GuidOf(amp::kIidVectorOfComponentModel), vector) ||
        FAILED(rt::CallSlot(vector, rt::kVectorSizeSlot, &size))) {
        return std::nullopt;
    }
    for (UINT32 i = 0; i < size && out.size() < limit; i++) {
        rt::Ref element;
        if (SUCCEEDED(rt::CallSlot(vector, rt::kVectorGetAtSlot, i, element.Put())) && element) {
            out.push_back(std::move(element));
        }
    }
    return out;
}

// IContentDescriptorDriven.get_ContentDescriptor (slot 6) as an IContentDescriptor, or empty.
rt::Ref DescriptorOf(const rt::Ref& model) {
    rt::Ref driven, object, descriptor;
    if (!rt::Query(model, rt::GuidOf(amp::kIidContentDescriptorDriven), driven) ||
        FAILED(rt::CallSlot(driven, slot::kDrivenContentDescriptor, object.Put())) || !object) {
        return {};
    }
    rt::Query(object, rt::GuidOf(amp::kIidContentDescriptor), descriptor);
    return descriptor;
}

std::optional<AmpQueueEntry> ReadQueueEntry(const rt::Ref& model, int32_t sectionKind) {
    rt::Ref item, titled;
    // IPlayQueueItem: get_PlayQueueItemID 6, get_Subtitle 7, get_Duration 10.
    if (!rt::Query(model, rt::GuidOf(amp::kIidPlayQueueItem), item)) return std::nullopt;
    AmpQueueEntry entry;
    entry.sectionKind = sectionKind;
    if (FAILED(rt::CallSlot(item, slot::kQueueItemId, &entry.id))) return std::nullopt;
    entry.subtitle = rt::ReadString(item, slot::kQueueItemSubtitle);
    rt::CallSlot(item, slot::kQueueItemDuration, &entry.durationRaw);
    // ITitledComponent.get_Title (slot 6).
    if (rt::Query(model, rt::GuidOf(amp::kIidTitledComponent), titled)) {
        entry.title = rt::ReadString(titled, slot::kTitledTitle);
    }
    // IContentDescriptor.get_AdamId (slot 21).
    if (rt::Ref descriptor = DescriptorOf(model)) entry.catalogId = rt::ReadString(descriptor, slot::kDescriptorAdamId);
    return entry;
}

// The sections of the queue: PlayQueueSection objects, each a container of entries (IComponentModelContainer).
std::vector<rt::Ref> QueueSections(std::string& failure) {
    rt::Ref queue = CurrentPlayQueue(failure);
    rt::Ref sections;
    // IPlayQueue.get_Sections (slot 9): IObservableVector<IComponentModel>.
    if (queue && (FAILED(rt::CallSlot(queue, slot::kPlayQueueSections, sections.Put())) || !sections)) {
        failure = "the queue has no sections";
    }
    if (!sections) return {};
    std::optional<std::vector<rt::Ref>> models = ComponentModels(sections, kMaxQueueEntries);
    if (!models) {
        failure = "the queue's sections are not an IVector<IComponentModel>: this version of Apple Music changed them";
    }
    return models.value_or(std::vector<rt::Ref>{});
}

// The entries of one section, with the section's kind (IPlayQueueSection.get_SectionKind, slot 6).
std::vector<rt::Ref> SectionEntries(const rt::Ref& sectionModel, int32_t& kind) {
    rt::Ref section, container, items;
    kind = 0;
    if (rt::Query(sectionModel, rt::GuidOf(amp::kIidPlayQueueSection), section)) {
        rt::CallSlot(section, slot::kSectionKind, &kind);
    }
    // IComponentModelContainer.get_Items (slot 6).
    if (!rt::Query(sectionModel, rt::GuidOf(amp::kIidComponentModelContainer), container) ||
        FAILED(rt::CallSlot(container, slot::kContainerItems, items.Put())) || !items) {
        return {};
    }
    // One past the cap, so that ReadAppleQueue sees a section longer than it reads.
    return ComponentModels(items, kMaxQueueEntries + 1).value_or(std::vector<rt::Ref>{});
}

rt::VectorIids CollectionIids(const char* const (&iids)[4]) {
    return {rt::GuidOf(iids[0]), rt::GuidOf(iids[1]), rt::GuidOf(iids[2]), rt::GuidOf(iids[3])};
}

}  // namespace

std::optional<std::vector<AmpQueueEntry>> ReadAppleQueue(std::string& failure, bool& cut) {
    cut = false;
    std::vector<rt::Ref> sections = QueueSections(failure);
    if (!failure.empty()) return std::nullopt;
    std::vector<AmpQueueEntry> entries;
    for (const rt::Ref& sectionModel : sections) {
        int32_t kind = 0;
        for (const rt::Ref& model : SectionEntries(sectionModel, kind)) {
            if (entries.size() >= kMaxQueueEntries) {
                cut = true;
                return entries;
            }
            if (std::optional<AmpQueueEntry> entry = ReadQueueEntry(model, kind)) entries.push_back(std::move(*entry));
        }
    }
    return entries;
}

rt::Ref StartAppleQueueRemove(uint64_t itemId, std::string& failure) {
    rt::Ref queue = CurrentPlayQueue(failure);
    if (!queue) return {};
    // IPlayQueue.DeletePlayQueueItemsAsync (slot 15): IVector<UInt64> of entry ids, then an IAsyncAction.
    rt::Ref ids = rt::MakeVector(CollectionIids(amp::kIidsUInt64Collections), std::vector<uint64_t>{itemId});
    rt::Ref operation;
    HRESULT hr = rt::CallSlot(queue, slot::kPlayQueueDelete, ids.Get(), operation.Put());
    if (FAILED(hr) || !operation) {
        failure = "DeletePlayQueueItemsAsync failed (code " + Hex(hr) + ")";
        return {};
    }
    return operation;
}

}  // namespace ampify::win
