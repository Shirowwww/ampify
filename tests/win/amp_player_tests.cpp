// Windows only, run under Wine: Apple's player reached through fake WinRT objects whose vtables are filled slot by
// slot, with the slot numbers written here from knowledge/winmd/AMP.Services.txt (6 + position in the "methods:"
// line). Any other slot records a mistake. So these tests prove that win/amp_player.cpp calls the slots the winmd
// gives, and that the bridge falls back to SMTC and the media keys when Apple's player cannot be reached.

#include <windows.h>

#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include "../../core/amp_abi.hpp"
#include "../../core/player_queue.hpp"
#include "../../core/settings.hpp"
#include "../../win/amp_player.hpp"
#include "../../win/player_bridge.hpp"
#include "../../win/win_host.hpp"
#include "../../win/winrt_abi.hpp"
#include "../test.hpp"

namespace {

namespace rt = ampify::win::rt;

// ---- A tiny fake WinRT object model -------------------------------------------------------------------------------

struct Fake;
struct Facet {
    void** vtable;
    Fake* owner;
};

struct Fake {
    std::vector<std::pair<GUID, Facet*>> facets;  // the first one is the object's identity
    std::string className;
    std::vector<GUID> iids;  // what GetIids answers
};

struct World {
    int unexpected = 0;
    int play = 0, pause = 0, next = 0, previous = 0;
    int handlersAdded = 0, handlersRemoved = 0;
    IUnknown* playerHandler = nullptr;
    bool noItem = false;
    unsigned char playing = 1, paused = 0, placeholder = 0;
    int64_t position = 600'000'000, duration = 1'800'000'000;  // 100 ns, as Apple's TimeSpan-like Int64 may be
    int32_t format = 1;                                          // Lossless
    uint64_t queueId = 42;
} g;

const GUID kUnknown = rt::GuidOf("00000000-0000-0000-C000-000000000046");
const GUID kInspectable = rt::GuidOf("AF86E2E0-B12D-4C6A-9C5A-D7AA65101E90");
const GUID kActivationFactory = rt::GuidOf("00000035-0000-0000-C000-000000000046");
const GUID kAsyncInfo = rt::GuidOf("00000036-0000-0000-C000-000000000046");
const GUID kNotify = rt::GuidOf("90B17601-B065-586E-83D9-9ADC3A695284");
// Made-up identifiers for the queue interfaces, which the probe must find through GetIids.
const GUID kFakeQueueStatics = rt::GuidOf("11111111-2222-3333-4444-555555555555");
const GUID kFakeQueue = rt::GuidOf("66666666-7777-8888-9999-AAAAAAAAAAAA");
const GUID kFakeVector = rt::GuidOf("BBBBBBBB-CCCC-DDDD-EEEE-FFFFFFFFFFFF");

template <class F>
void* Fn(F function) {
    void* pointer;
    static_assert(sizeof pointer == sizeof function);
    std::memcpy(&pointer, &function, sizeof pointer);
    return pointer;
}

HSTRING MakeString(const wchar_t* text) {
    HSTRING out = nullptr;
    rt::Api()->WindowsCreateString(text, static_cast<UINT32>(wcslen(text)), &out);
    return out;
}

HRESULT STDMETHODCALLTYPE Unexpected(Facet*) {
    ++g.unexpected;
    return E_NOTIMPL;
}
HRESULT STDMETHODCALLTYPE QueryInterfaceFake(Facet* self, REFIID iid, void** out) {
    Fake* fake = self->owner;
    if (IsEqualGUID(iid, kUnknown) || IsEqualGUID(iid, kInspectable)) {
        *out = fake->facets[0].second;
        return S_OK;
    }
    for (auto& [known, facet] : fake->facets) {
        if (IsEqualGUID(known, iid)) {
            *out = facet;
            return S_OK;
        }
    }
    *out = nullptr;
    return E_NOINTERFACE;
}
ULONG STDMETHODCALLTYPE AddRefFake(Facet*) { return 2; }  // the fakes live as long as the test program
ULONG STDMETHODCALLTYPE ReleaseFake(Facet*) { return 1; }
HRESULT STDMETHODCALLTYPE GetIidsFake(Facet* self, ULONG* count, GUID** iids) {
    const std::vector<GUID>& list = self->owner->iids;
    *count = static_cast<ULONG>(list.size());
    *iids = static_cast<GUID*>(CoTaskMemAlloc(sizeof(GUID) * (list.empty() ? 1 : list.size())));
    if (!list.empty()) std::memcpy(*iids, list.data(), sizeof(GUID) * list.size());
    return S_OK;
}
HRESULT STDMETHODCALLTYPE ClassNameFake(Facet* self, HSTRING* out) {
    std::wstring wide(self->owner->className.begin(), self->owner->className.end());
    *out = MakeString(wide.c_str());
    return S_OK;
}
HRESULT STDMETHODCALLTYPE TrustFake(Facet*, int* level) {
    *level = 0;
    return S_OK;
}

struct Vtable {
    void* slots[64];
    Vtable() {
        for (void*& slot : slots) slot = Fn(&Unexpected);
        slots[0] = Fn(&QueryInterfaceFake);
        slots[1] = Fn(&AddRefFake);
        slots[2] = Fn(&ReleaseFake);
        slots[3] = Fn(&GetIidsFake);
        slots[4] = Fn(&ClassNameFake);
        slots[5] = Fn(&TrustFake);
    }
};

// ---- Apple's player -----------------------------------------------------------------------------------------------

Vtable staticsVt, playerVt, notifyVt, stateVt, stateNotifyVt, itemVt, asyncVt, asyncInfoVt;
Fake staticsObj, playerObj, stateObj, itemObj, asyncObj;
Facet staticsF{staticsVt.slots, &staticsObj}, playerF{playerVt.slots, &playerObj}, notifyF{notifyVt.slots, &playerObj},
    stateF{stateVt.slots, &stateObj}, stateNotifyF{stateNotifyVt.slots, &stateObj}, itemF{itemVt.slots, &itemObj},
    asyncF{asyncVt.slots, &asyncObj}, asyncInfoF{asyncInfoVt.slots, &asyncObj};

HRESULT STDMETHODCALLTYPE CurrentPlayer(Facet*, void** out) {  // IPlayerStatics.get_Current, slot 6
    *out = &playerF;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE NowPlayingItem(Facet*, void** out) {  // IPlayer.get_NowPlayingItem, slot 6
    *out = g.noItem ? nullptr : &itemF;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE PlayerState(Facet*, void** out) {  // IPlayer.get_PlayerState, slot 7
    *out = &stateF;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE PlayAsync(Facet*, void** out) {  // IPlayer.PlayAsync, slot 12
    ++g.play;
    *out = &asyncF;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE PauseAsync(Facet*, void** out) {  // slot 13
    ++g.pause;
    *out = &asyncF;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE SkipPreviousAsync(Facet*, void** out) {  // slot 16
    ++g.previous;
    *out = &asyncF;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE SkipNextAsync(Facet*, void** out) {  // slot 17
    ++g.next;
    *out = &asyncF;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE AddHandler(Facet* self, IUnknown* handler, int64_t* token) {  // add_PropertyChanged, 6
    ++g.handlersAdded;
    handler->AddRef();
    if (self == &notifyF) g.playerHandler = handler;
    *token = g.handlersAdded;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE RemoveHandler(Facet*, int64_t) {  // remove_PropertyChanged, 7
    ++g.handlersRemoved;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE IsPaused(Facet*, unsigned char* out) {  // IPlayerState.get_IsPaused, slot 24
    *out = g.paused;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE IsPlaying(Facet*, unsigned char* out) {  // IPlayerState.get_IsPlaying, slot 25
    *out = g.playing;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE Artist(Facet*, HSTRING* out) {  // INowPlayingItem.get_Artist, slot 7
    *out = MakeString(L"Artiste");
    return S_OK;
}
HRESULT STDMETHODCALLTYPE Album(Facet*, HSTRING* out) {  // get_Album, slot 8
    *out = MakeString(L"Album");
    return S_OK;
}
HRESULT STDMETHODCALLTYPE Name(Facet*, HSTRING* out) {  // get_Name, slot 19
    *out = MakeString(L"Titre é");
    return S_OK;
}
HRESULT STDMETHODCALLTYPE IsPlaceholder(Facet*, unsigned char* out) {  // get_IsPlaceholder, slot 24
    *out = g.placeholder;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE CurrentPosition(Facet*, int64_t* out) {  // get_CurrentPosition, slot 30
    *out = g.position;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE Duration(Facet*, int64_t* out) {  // get_Duration, slot 31
    *out = g.duration;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE AudioFormat(Facet*, int32_t* out) {  // get_AudioFormat, slot 38
    *out = g.format;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE QueueItemId(Facet*, uint64_t* out) {  // get_PlayQueueItemID, slot 45
    *out = g.queueId;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE AsyncCompleted(Facet*, int32_t* status) {  // IAsyncInfo.get_Status, slot 7
    *status = 1;
    return S_OK;
}

// ---- The queue probe ----------------------------------------------------------------------------------------------

Vtable queueFactoryVt, queueStaticsVt, queueVt, queueIfaceVt, sectionsVt;
Fake queueFactoryObj, queueObj, sectionsObj;
Facet queueFactoryF{queueFactoryVt.slots, &queueFactoryObj}, queueStaticsF{queueStaticsVt.slots, &queueFactoryObj},
    queueF{queueVt.slots, &queueObj}, queueIfaceF{queueIfaceVt.slots, &queueObj},
    sectionsF{sectionsVt.slots, &sectionsObj};

HRESULT STDMETHODCALLTYPE CurrentQueue(Facet*, void** out) {  // IPlayQueueStatics.get_Current, slot 6
    *out = &queueF;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE Sections(Facet*, void** out) {  // IPlayQueue.get_Sections, slot 9
    *out = &sectionsF;
    return S_OK;
}

void Build() {
    static bool built = false;
    if (built) return;
    built = true;
    const GUID player = rt::GuidOf(ampify::amp::kIidPlayer);
    staticsObj.facets = {{rt::GuidOf(ampify::amp::kIidPlayerStatics), &staticsF}};
    staticsVt.slots[6] = Fn(&CurrentPlayer);

    playerObj.facets = {{player, &playerF}, {kNotify, &notifyF}};
    playerObj.className = "AMP.Services.Player";
    playerVt.slots[6] = Fn(&NowPlayingItem);
    playerVt.slots[7] = Fn(&PlayerState);
    playerVt.slots[12] = Fn(&PlayAsync);
    playerVt.slots[13] = Fn(&PauseAsync);
    playerVt.slots[16] = Fn(&SkipPreviousAsync);
    playerVt.slots[17] = Fn(&SkipNextAsync);
    notifyVt.slots[6] = Fn(&AddHandler);
    notifyVt.slots[7] = Fn(&RemoveHandler);

    stateObj.facets = {{rt::GuidOf(ampify::amp::kIidPlayerState), &stateF}, {kNotify, &stateNotifyF}};
    stateVt.slots[24] = Fn(&IsPaused);
    stateVt.slots[25] = Fn(&IsPlaying);
    stateNotifyVt.slots[6] = Fn(&AddHandler);
    stateNotifyVt.slots[7] = Fn(&RemoveHandler);

    itemObj.facets = {{rt::GuidOf(ampify::amp::kIidNowPlayingItem), &itemF}};
    itemVt.slots[7] = Fn(&Artist);
    itemVt.slots[8] = Fn(&Album);
    itemVt.slots[19] = Fn(&Name);
    itemVt.slots[24] = Fn(&IsPlaceholder);
    itemVt.slots[30] = Fn(&CurrentPosition);
    itemVt.slots[31] = Fn(&Duration);
    itemVt.slots[38] = Fn(&AudioFormat);
    itemVt.slots[45] = Fn(&QueueItemId);

    asyncObj.facets = {{kUnknown, &asyncF}, {kAsyncInfo, &asyncInfoF}};
    asyncInfoVt.slots[7] = Fn(&AsyncCompleted);

    queueFactoryObj.facets = {{kActivationFactory, &queueFactoryF}, {kFakeQueueStatics, &queueStaticsF}};
    queueFactoryObj.iids = {kActivationFactory, kFakeQueueStatics};
    queueStaticsVt.slots[6] = Fn(&CurrentQueue);
    queueObj.facets = {{kInspectable, &queueF}, {kFakeQueue, &queueIfaceF}, {kNotify, &queueF}};
    queueObj.className = "AMP.Services.PlayQueue";
    queueObj.iids = {kNotify, kFakeQueue};
    queueIfaceVt.slots[9] = Fn(&Sections);
    sectionsObj.facets = {{kInspectable, &sectionsF}};
    sectionsObj.className = "Windows.Foundation.Collections.IObservableVector`1<AMP.Services.IPlayQueueSection>";
    sectionsObj.iids = {kFakeVector};
}

HRESULT FakeActivation(std::wstring_view name, const GUID& iid, void** out) {
    Build();
    *out = nullptr;
    if (name == L"AMP.Services.Player" && IsEqualGUID(iid, rt::GuidOf(ampify::amp::kIidPlayerStatics))) {
        *out = &staticsF;
        return S_OK;
    }
    if (name == L"AMP.Services.PlayQueue" && IsEqualGUID(iid, kActivationFactory)) {
        *out = &queueFactoryF;
        return S_OK;
    }
    return REGDB_E_CLASSNOTREG;  // SMTC and everything else: not there
}

HRESULT NoActivation(std::wstring_view, const GUID&, void** out) {
    *out = nullptr;
    return REGDB_E_CLASSNOTREG;
}

void Reset() {
    Build();
    World fresh;
    g = fresh;
}

// Removes the hook at the end of a test, whatever happens.
struct Hook {
    explicit Hook(rt::ActivationHook hook) { rt::SetActivationHookForTests(hook); }
    ~Hook() { rt::SetActivationHookForTests(nullptr); }
};

ampify::win::PlayerBridgeOptions TestOptions(std::vector<unsigned short>* keys) {
    ampify::win::PlayerBridgeOptions options;
    options.keys = [keys](unsigned short key) {
        keys->push_back(key);
        return true;
    };
    options.waitForWindow = false;
    options.retryMs = 0;
    options.maxAttempts = 2;
    return options;
}

}  // namespace

TEST_CASE(AmpPlayer_ReadsTheStateThroughTheWinmdSlots) {
    Reset();
    Hook hook(FakeActivation);
    rt::EnsureMultithreaded();
    ampify::win::AmpPlayer player;
    std::string failure;
    CHECK(player.Activate(failure));
    CHECK(player.ActivationPath() == "test");
    std::optional<ampify::AmpReading> reading = player.Read();
    CHECK(reading.has_value());
    CHECK(reading->playing && !reading->paused && reading->hasItem && !reading->placeholder);
    CHECK(reading->title == "Titre \xC3\xA9");
    CHECK(reading->artist == "Artiste");
    CHECK(reading->album == "Album");
    CHECK(reading->positionRaw == 600'000'000 && reading->durationRaw == 1'800'000'000);
    CHECK(reading->audioFormat == 1);
    CHECK(reading->queueItemId == 42);
    CHECK(player.Subscribed());
    CHECK(g.handlersAdded == 2);  // the player and its PlayerState
    CHECK(g.unexpected == 0);

    g.noItem = true;
    reading = player.Read();
    CHECK(reading.has_value() && !reading->hasItem);
    CHECK(g.unexpected == 0);
}

TEST_CASE(AmpPlayer_CommandsCallTheirOwnSlotsAndNothingElse) {
    Reset();
    Hook hook(FakeActivation);
    ampify::win::AmpPlayer player;
    std::string failure;
    CHECK(player.Activate(failure));
    CHECK(player.Command(ampify::PlayerCommand::Play));
    CHECK(player.Command(ampify::PlayerCommand::Pause));
    CHECK(player.Command(ampify::PlayerCommand::Next));
    CHECK(player.Command(ampify::PlayerCommand::Previous));
    CHECK(!player.Command(ampify::PlayerCommand::Seek));  // SetPositionAsync is never called
    CHECK(g.play == 1 && g.pause == 1 && g.next == 1 && g.previous == 1);
    CHECK(g.unexpected == 0);
}

TEST_CASE(AmpPlayer_CountsApplesPropertyChangedNotifications) {
    Reset();
    Hook hook(FakeActivation);
    ampify::win::AmpPlayer player;
    std::string failure;
    CHECK(player.Activate(failure));
    CHECK(g.playerHandler != nullptr);
    CHECK(player.Notifications() == 0);
    // PropertyChangedEventHandler.Invoke is slot 3 of the delegate.
    rt::CallSlot(static_cast<void*>(g.playerHandler), 3, static_cast<void*>(&playerF), static_cast<void*>(nullptr));
    CHECK(player.Notifications() == 1);
    g.playerHandler->Release();
}

TEST_CASE(AmpPlayer_TheQueueProbeFindsTheInterfacesByElimination) {
    Reset();
    Hook hook(FakeActivation);
    ampify::win::AmpPlayer player;
    std::string line = player.ProbeQueue();
    CHECK(line.find("IPlayQueueStatics = 11111111-2222-3333-4444-555555555555") != std::string::npos);
    CHECK(line.find("IPlayQueue = 66666666-7777-8888-9999-AAAAAAAAAAAA") != std::string::npos);
    CHECK(line.find("IObservableVector`1<AMP.Services.IPlayQueueSection>") != std::string::npos);
    CHECK(line.find("BBBBBBBB-CCCC-DDDD-EEEE-FFFFFFFFFFFF") != std::string::npos);
    CHECK(g.unexpected == 0);
}

TEST_CASE(PlayerBridge_FallsBackToSmtcThenTheMediaKeysWhenApplesPlayerIsMissing) {
    Reset();
    Hook hook(NoActivation);
    ampify::Settings settings;
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("t"); });
    std::vector<unsigned short> keys;
    ampify::win::PlayerBridge bridge("ui", settings, journal, TestOptions(&keys));

    CHECK(bridge.Snapshot().source.empty());
    CHECK(bridge.Snapshot().source.empty());
    CHECK(bridge.AppleAttempts() == 2);
    CHECK(log.str().find("AMP.Services.Player non activé") != std::string::npos);
    CHECK(log.str().find("repli sur les contrôles multimédias de Windows (SMTC)") != std::string::npos);
    CHECK(log.str().find("API SMTC indisponible") != std::string::npos);

    CHECK(bridge.Control(ampify::PlayerCommand::Next, 0));
    CHECK(keys.size() == 1 && keys[0] == VK_MEDIA_NEXT_TRACK);
    // The state is unknown: the play/pause key could do the opposite, so play is refused.
    CHECK(!bridge.Control(ampify::PlayerCommand::Play, 0));
    CHECK(!bridge.Control(ampify::PlayerCommand::Seek, 1000));
    CHECK(keys.size() == 1);
}

TEST_CASE(PlayerBridge_ReadsApplesPlayerAndUsesItsCommandsOnlyWhenAllowed) {
    Reset();
    Hook hook(FakeActivation);
    ampify::Settings settings;
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("t"); });
    std::vector<unsigned short> keys;
    ampify::win::PlayerBridge bridge("ui", settings, journal, TestOptions(&keys));

    ampify::PlayerSnapshot s = bridge.Snapshot();
    CHECK(s.source == "apple");
    CHECK(s.playing);
    CHECK(s.track.has_value() && s.track->title == "Titre \xC3\xA9" && s.track->lossless && !s.track->atmos);
    CHECK(s.positionMs == 60'000 && s.durationMs == 180'000);
    CHECK(s.track->queueItemId == "42");
    CHECK(log.str().find("AMP.Services.Player activé (test") != std::string::npos);
    CHECK(log.str().find("premier état lu par AMP.Services.Player : en lecture=oui") != std::string::npos);
    CHECK(log.str().find("IPlayQueue = 66666666-7777-8888-9999-AAAAAAAAAAAA") != std::string::npos);

    // player.appleCommands is off: pause goes to the media key, the state being known (playing).
    CHECK(bridge.Control(ampify::PlayerCommand::Pause, 0));
    CHECK(g.pause == 0);
    CHECK(keys.size() == 1 && keys[0] == VK_MEDIA_PLAY_PAUSE);
    CHECK(bridge.Control(ampify::PlayerCommand::Play, 0));  // already playing: nothing sent
    CHECK(keys.size() == 1 && g.play == 0);

    settings.Set("player.appleCommands", "true");
    CHECK(bridge.Control(ampify::PlayerCommand::Next, 0));
    CHECK(g.next == 1 && keys.size() == 1);
    CHECK(g.unexpected == 0);
}

TEST_CASE(PlayerBridge_TheEngineNeverAsksForApplesPlayer) {
    Reset();
    Hook hook(FakeActivation);
    ampify::Settings settings;
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("t"); });
    std::vector<unsigned short> keys;
    ampify::win::PlayerBridge bridge("engine", settings, journal, TestOptions(&keys));
    CHECK(bridge.Snapshot().source.empty());
    CHECK(bridge.AppleAttempts() == 0);
    CHECK(log.str().find("vit dans AppleMusic.exe, il n'est pas utilisé ici") != std::string::npos);
}

TEST_CASE(PlayerBridge_TheKillSwitchKeepsApplesPlayerOff) {
    Reset();
    Hook hook(FakeActivation);
    ampify::Settings settings;
    settings.Set("player.apple", "false");
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("t"); });
    std::vector<unsigned short> keys;
    ampify::win::PlayerBridge bridge("ui", settings, journal, TestOptions(&keys));
    CHECK(bridge.Snapshot().source.empty());
    CHECK(bridge.AppleAttempts() == 0);
    CHECK(log.str().find("désactivé par le réglage player.apple") != std::string::npos);
}

TEST_CASE(PlayerBridge_TheQueueHasTheCurrentTrackAndRefusesChangesWithAReason) {
    Reset();
    Hook hook(FakeActivation);
    ampify::Settings settings;
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("t"); });
    std::vector<unsigned short> keys;
    ampify::win::PlayerBridge bridge("ui", settings, journal, TestOptions(&keys));
    ampify::QueueSnapshot queue = bridge.Queue();
    CHECK(!queue.complete);
    CHECK(queue.source == "apple");
    CHECK(queue.items.size() == 1);
    CHECK(queue.items[0].id == "42" && queue.items[0].section == ampify::QueueSection::Current);
    ampify::QueueResult added = bridge.AddToQueue("1", ampify::QueuePosition::Next);
    CHECK(!added.ok && !added.reason.empty());
    CHECK(!bridge.RemoveFromQueue("42").ok);
}

TEST_CASE(WinHost_OffersTheQueueCapability) {
    namespace fs = std::filesystem;
    fs::path kit = fs::temp_directory_path() / "ampify-tests" / "win-host-queue";
    fs::create_directories(kit);
    ampify::Settings settings;
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("t"); });
    ampify::WinHost host("engine", kit, settings, journal);
    CHECK(host.Capability<ampify::QueueHost>() != nullptr);
}
