// Windows only: Apple's player reached through fake WinRT objects whose vtables are filled slot by
// slot, with the slot numbers written here from knowledge/winmd/AMP.Services.txt (6 + position in the "methods:"
// line). Any other slot records a mistake. So these tests prove that src/win/amp_player.cpp calls the slots the winmd
// gives, and that the bridge falls back to SMTC and the media keys when Apple's player cannot be reached.

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include "../../src/core/amp_abi.hpp"
#include "../../src/core/lyrics_host.hpp"
#include "../../src/core/player_queue.hpp"
#include "../../src/core/settings.hpp"
#include "../../src/win/amp_item.hpp"
#include "../../src/win/amp_player.hpp"
#include "../../src/win/player_bridge.hpp"
#include "../../src/win/win_host.hpp"
#include "../../src/win/winrt_abi.hpp"
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
    int64_t seekTicks = -1;  // the TimeSpan SetPositionAsync received, -1 when never called
    unsigned char seekScrubbing = 0;
    int handlersAdded = 0, handlersRemoved = 0;
    IUnknown* playerHandler = nullptr;
    bool noItem = false;
    unsigned char playing = 1, paused = 0, placeholder = 0;
    int64_t position = 600'000'000, duration = 1'800'000'000;  // 100 ns, as Apple's TimeSpan-like Int64 may be
    int32_t format = 1;                                          // Lossless
    uint64_t queueId = 42;
    bool noArtwork = false;      // get_Artwork answers S_OK with a null object
    bool lyricsInfoNull = false;  // get_LyricsInfo answers S_OK with a null object
    unsigned char haveLyrics = 1;
    int lyricsAsked = 0;          // HaveLyrics calls
    int ttmlRead = 0;             // GetTTML calls
    bool noQueue = false;         // AMP.Services.PlayQueue is not there
    bool sectionsChanged = false;  // get_Sections answers an object that is not an IVector<IComponentModel>
    UINT32 longerNext = 0;        // the "next" section repeats its last entry this many more times
    std::vector<uint64_t> deleted;  // the ids DeletePlayQueueItemsAsync received
    int descriptorFlag = 0;       // the IContentDescriptor Is* slot that answers true (0: none)
} g;

const GUID kUnknown = rt::GuidOf("00000000-0000-0000-C000-000000000046");
const GUID kInspectable = rt::GuidOf("AF86E2E0-B12D-4C6A-9C5A-D7AA65101E90");
const GUID kActivationFactory = rt::GuidOf("00000035-0000-0000-C000-000000000046");
const GUID kAsyncInfo = rt::GuidOf("00000036-0000-0000-C000-000000000046");
const GUID kNotify = rt::GuidOf("90B17601-B065-586E-83D9-9ADC3A695284");

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
HRESULT STDMETHODCALLTYPE SetPositionAsync(Facet*, int64_t position, unsigned char scrubbing, void** out) {  // slot 22
    g.seekTicks = position;
    g.seekScrubbing = scrubbing;
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
HRESULT STDMETHODCALLTYPE Subtitle(Facet*, HSTRING* out) {  // INowPlayingItem.get_Subtitle, slot 6
    *out = MakeString(L"Artiste — Album");
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
    *out = MakeString(L"Title é");
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

// ---- The cover and the lyrics (1.1792) ----------------------------------------------------------------------------

Vtable artworkVt, uriVt, lyricsVt;
Fake artworkObj, uriObj, lyricsObj;
Facet artworkF{artworkVt.slots, &artworkObj}, uriF{uriVt.slots, &uriObj}, lyricsF{lyricsVt.slots, &lyricsObj};

HRESULT STDMETHODCALLTYPE ItemArtwork(Facet*, void** out) {  // INowPlayingItem.get_Artwork, slot 11
    *out = g.noArtwork ? nullptr : &artworkF;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE ItemLyricsInfo(Facet*, void** out) {  // get_LyricsInfo, slot 46
    *out = g.lyricsInfoNull ? nullptr : &lyricsF;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE ArtworkColor(Facet*, uint8_t* out) {  // IArtwork.get_BGColor, slot 6: A, R, G, B
    out[0] = 255;
    out[1] = 0x33;
    out[2] = 0x66;
    out[3] = 0x99;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE ArtworkFormat(Facet*, HSTRING* out) {  // get_Format, slot 10
    *out = MakeString(L"jpg");
    return S_OK;
}
HRESULT STDMETHODCALLTYPE ArtworkHeight(Facet*, double* out) {  // get_Height, slot 12
    *out = 600;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE ArtworkWidth(Facet*, double* out) {  // get_Width, slot 14
    *out = 600;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE ArtworkUri(Facet*, void** out) {  // get_Uri, slot 16
    *out = &uriF;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE ArtworkCrop(Facet*, HSTRING* out) {  // GetCropCode, slot 17
    *out = MakeString(L"bb");
    return S_OK;
}
HRESULT STDMETHODCALLTYPE UriAbsolute(Facet*, HSTRING* out) {  // IUriRuntimeClass.get_AbsoluteUri, slot 6
    *out = MakeString(L"https://is1-ssl.mzstatic.com/image/thumb/Music/a.jpg/{w}x{h}{c}.{f}");
    return S_OK;
}
HRESULT STDMETHODCALLTYPE HaveLyrics(Facet*, unsigned char* out) {  // ILyricsInfo.HaveLyrics, slot 12
    ++g.lyricsAsked;
    *out = g.haveLyrics;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE StaticLyrics(Facet*, HSTRING* out) {  // GetStaticLyrics, slot 13
    *out = MakeString(L"First line\nSecond line");
    return S_OK;
}
HRESULT STDMETHODCALLTYPE Ttml(Facet*, HSTRING* out) {  // GetTTML, slot 14
    ++g.ttmlRead;
    *out = MakeString(L"<tt xml:lang=\"en\"><body><div><p begin=\"1.0\" end=\"2.5\">Hello</p></div></body></tt>");
    return S_OK;
}
HRESULT STDMETHODCALLTYPE LyricsLanguage(Facet*, HSTRING* out) {  // GetLanguage, slot 15
    *out = MakeString(L"en");
    return S_OK;
}

// ---- Apple's queue: three sections (history, current, next) of PlayQueueItem entries --------------------------------

Vtable queueStaticsVt, queueVt, sectionsVt, sectionVt, containerVt, entriesVt, entryVt, titledVt, drivenVt,
    descriptorVt;
Fake queueStaticsObj, queueObj, sectionsObj;
Facet queueStaticsF{queueStaticsVt.slots, &queueStaticsObj}, queueF{queueVt.slots, &queueObj},
    sectionsF{sectionsVt.slots, &sectionsObj};

struct EntryFake {
    uint64_t id;
    const wchar_t* title;
    const wchar_t* artist;
    int64_t durationMs;
    const wchar_t* catalogId;
    Fake obj, descriptorObj;
    Facet item{entryVt.slots, &obj}, titled{titledVt.slots, &obj}, driven{drivenVt.slots, &obj},
        descriptor{descriptorVt.slots, &descriptorObj};
};
EntryFake entries[] = {
    {101, L"Earlier", L"Artist A", 200'000, L"1001"},
    {102, L"Now", L"Artist B", 180'000, L"1002"},
    {103, L"Soon", L"Artist C", 240'000, L"1003"},
    {104, L"Later", L"Artist D", 210'000, L""},
};

struct SectionFake {
    int32_t kind;  // PlayQueueSectionKind
    std::vector<EntryFake*> entries;
    Fake obj, listObj;
    Facet section{sectionVt.slots, &obj}, container{containerVt.slots, &obj}, list{entriesVt.slots, &listObj};
};
SectionFake sections[] = {{1, {&entries[0]}}, {2, {&entries[1]}}, {4, {&entries[2], &entries[3]}}};

// The fake whose facet `member` is `self`.
template <class T, size_t N>
T& Owning(T (&all)[N], Facet T::*member, Facet* self) {
    for (T& one : all) {
        if (&(one.*member) == self) return one;
    }
    ++g.unexpected;
    return all[0];
}

HRESULT STDMETHODCALLTYPE CurrentQueue(Facet*, void** out) {  // IPlayQueueStatics.get_Current, slot 6
    *out = &queueF;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE Sections(Facet*, void** out) {  // IPlayQueue.get_Sections, slot 9
    *out = g.sectionsChanged ? &queueF : &sectionsF;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE DeleteItems(Facet*, void* ids, void** out) {  // DeletePlayQueueItemsAsync, slot 15
    UINT32 size = 0;
    rt::CallSlot(ids, rt::kVectorSizeSlot, &size);  // IVector<UInt64>.get_Size
    for (UINT32 i = 0; i < size; i++) {
        uint64_t id = 0;
        rt::CallSlot(ids, rt::kVectorGetAtSlot, i, &id);
        g.deleted.push_back(id);
    }
    *out = &asyncF;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE SectionAt(Facet*, UINT32 index, void** out) {  // IVector<IComponentModel>.GetAt, 6
    *out = &sections[index].section;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE SectionCount(Facet*, UINT32* out) {  // get_Size, 7
    *out = static_cast<UINT32>(std::size(sections));
    return S_OK;
}
HRESULT STDMETHODCALLTYPE SectionKind(Facet* self, int32_t* out) {  // IPlayQueueSection.get_SectionKind, 6
    *out = Owning(sections, &SectionFake::section, self).kind;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE SectionItems(Facet* self, void** out) {  // IComponentModelContainer.get_Items, 6
    *out = &Owning(sections, &SectionFake::container, self).list;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE EntryAt(Facet* self, UINT32 index, void** out) {  // IVector<IComponentModel>.GetAt, 6
    const std::vector<EntryFake*>& list = Owning(sections, &SectionFake::list, self).entries;
    *out = &list[std::min<size_t>(index, list.size() - 1)]->item;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE EntryCount(Facet* self, UINT32* out) {  // get_Size, 7
    const SectionFake& section = Owning(sections, &SectionFake::list, self);
    *out = static_cast<UINT32>(section.entries.size()) + (section.kind == 4 ? g.longerNext : 0);
    return S_OK;
}
HRESULT STDMETHODCALLTYPE EntryId(Facet* self, uint64_t* out) {  // IPlayQueueItem.get_PlayQueueItemID, 6
    *out = Owning(entries, &EntryFake::item, self).id;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE EntrySubtitle(Facet* self, HSTRING* out) {  // get_Subtitle, 7
    *out = MakeString(Owning(entries, &EntryFake::item, self).artist);
    return S_OK;
}
HRESULT STDMETHODCALLTYPE EntryDuration(Facet* self, int64_t* out) {  // get_Duration, 10
    *out = Owning(entries, &EntryFake::item, self).durationMs;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE EntryTitle(Facet* self, HSTRING* out) {  // ITitledComponent.get_Title, 6
    *out = MakeString(Owning(entries, &EntryFake::titled, self).title);
    return S_OK;
}
HRESULT STDMETHODCALLTYPE EntryDescriptor(Facet* self, void** out) {  // get_ContentDescriptor, 6
    *out = &Owning(entries, &EntryFake::driven, self).descriptor;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE DescriptorAdamId(Facet* self, HSTRING* out) {  // IContentDescriptor.get_AdamId, 21
    *out = MakeString(Owning(entries, &EntryFake::descriptor, self).catalogId);
    return S_OK;
}
HRESULT STDMETHODCALLTYPE DescriptorCanonicalId(Facet* self, HSTRING* out) {  // get_CanonicalId, 22
    std::wstring id = Owning(entries, &EntryFake::descriptor, self).catalogId;
    *out = MakeString(id.empty() ? L"" : (L"c:" + id).c_str());
    return S_OK;
}
// The Is* getters (one byte): only the slot the test turned on says yes.
template <int Slot>
HRESULT STDMETHODCALLTYPE DescriptorFlag(Facet*, unsigned char* out) {
    *out = g.descriptorFlag == Slot ? 1 : 0;
    return S_OK;
}

void Build() {
    static bool built = false;
    if (built) return;
    built = true;
    const GUID player = rt::GuidOf(ampify::amp::kIidsPlayer[0]);
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
    playerVt.slots[22] = Fn(&SetPositionAsync);
    notifyVt.slots[6] = Fn(&AddHandler);
    notifyVt.slots[7] = Fn(&RemoveHandler);

    stateObj.facets = {{rt::GuidOf(ampify::amp::kIidPlayerState), &stateF}, {kNotify, &stateNotifyF}};
    stateVt.slots[24] = Fn(&IsPaused);
    stateVt.slots[25] = Fn(&IsPlaying);
    stateNotifyVt.slots[6] = Fn(&AddHandler);
    stateNotifyVt.slots[7] = Fn(&RemoveHandler);

    itemObj.facets = {{rt::GuidOf(ampify::amp::kIidsNowPlayingItem[0]), &itemF}};
    itemVt.slots[6] = Fn(&Subtitle);
    itemVt.slots[7] = Fn(&Artist);
    itemVt.slots[8] = Fn(&Album);
    itemVt.slots[11] = Fn(&ItemArtwork);
    itemVt.slots[19] = Fn(&Name);
    itemVt.slots[24] = Fn(&IsPlaceholder);
    itemVt.slots[30] = Fn(&CurrentPosition);
    itemVt.slots[31] = Fn(&Duration);
    itemVt.slots[38] = Fn(&AudioFormat);
    itemVt.slots[45] = Fn(&QueueItemId);
    itemVt.slots[46] = Fn(&ItemLyricsInfo);

    artworkObj.facets = {{rt::GuidOf(ampify::amp::kIidArtwork), &artworkF}};
    artworkVt.slots[6] = Fn(&ArtworkColor);
    artworkVt.slots[10] = Fn(&ArtworkFormat);
    artworkVt.slots[12] = Fn(&ArtworkHeight);
    artworkVt.slots[14] = Fn(&ArtworkWidth);
    artworkVt.slots[16] = Fn(&ArtworkUri);
    artworkVt.slots[17] = Fn(&ArtworkCrop);
    uriObj.facets = {{rt::GuidOf(ampify::amp::kIidUriRuntimeClass), &uriF}};
    uriVt.slots[6] = Fn(&UriAbsolute);
    lyricsObj.facets = {{rt::GuidOf(ampify::amp::kIidLyricsInfo), &lyricsF}};
    lyricsVt.slots[12] = Fn(&HaveLyrics);
    lyricsVt.slots[13] = Fn(&StaticLyrics);
    lyricsVt.slots[14] = Fn(&Ttml);
    lyricsVt.slots[15] = Fn(&LyricsLanguage);

    asyncObj.facets = {{kUnknown, &asyncF}, {kAsyncInfo, &asyncInfoF}};
    asyncInfoVt.slots[7] = Fn(&AsyncCompleted);

    namespace amp = ampify::amp;
    queueStaticsObj.facets = {{rt::GuidOf(amp::kIidPlayQueueStatics), &queueStaticsF}};
    queueStaticsVt.slots[6] = Fn(&CurrentQueue);
    queueObj.facets = {{rt::GuidOf(amp::kIidPlayQueue), &queueF}};
    queueVt.slots[9] = Fn(&Sections);
    queueVt.slots[15] = Fn(&DeleteItems);
    sectionsObj.facets = {{rt::GuidOf(amp::kIidVectorOfComponentModel), &sectionsF}};
    sectionsVt.slots[6] = Fn(&SectionAt);
    sectionsVt.slots[7] = Fn(&SectionCount);
    for (SectionFake& section : sections) {
        section.obj.facets = {{rt::GuidOf(amp::kIidPlayQueueSection), &section.section},
                              {rt::GuidOf(amp::kIidComponentModelContainer), &section.container}};
        section.listObj.facets = {{rt::GuidOf(amp::kIidVectorOfComponentModel), &section.list}};
    }
    sectionVt.slots[6] = Fn(&SectionKind);
    containerVt.slots[6] = Fn(&SectionItems);
    entriesVt.slots[6] = Fn(&EntryAt);
    entriesVt.slots[7] = Fn(&EntryCount);
    for (EntryFake& entry : entries) {
        entry.obj.facets = {{rt::GuidOf(amp::kIidPlayQueueItem), &entry.item},
                            {rt::GuidOf(amp::kIidTitledComponent), &entry.titled},
                            {rt::GuidOf(amp::kIidContentDescriptorDriven), &entry.driven}};
        entry.descriptorObj.facets = {{rt::GuidOf(amp::kIidContentDescriptor), &entry.descriptor}};
    }
    entryVt.slots[6] = Fn(&EntryId);
    entryVt.slots[7] = Fn(&EntrySubtitle);
    entryVt.slots[10] = Fn(&EntryDuration);
    titledVt.slots[6] = Fn(&EntryTitle);
    drivenVt.slots[6] = Fn(&EntryDescriptor);
    descriptorVt.slots[21] = Fn(&DescriptorAdamId);
    descriptorVt.slots[22] = Fn(&DescriptorCanonicalId);
    descriptorVt.slots[11] = Fn(&DescriptorFlag<11>);
    descriptorVt.slots[12] = Fn(&DescriptorFlag<12>);
    descriptorVt.slots[13] = Fn(&DescriptorFlag<13>);
    descriptorVt.slots[14] = Fn(&DescriptorFlag<14>);
    descriptorVt.slots[15] = Fn(&DescriptorFlag<15>);
    descriptorVt.slots[16] = Fn(&DescriptorFlag<16>);
    descriptorVt.slots[18] = Fn(&DescriptorFlag<18>);
    descriptorVt.slots[19] = Fn(&DescriptorFlag<19>);
    descriptorVt.slots[20] = Fn(&DescriptorFlag<20>);
}

HRESULT FakeActivation(std::wstring_view name, const GUID& iid, void** out) {
    Build();
    *out = nullptr;
    if (name == L"AMP.Services.Player" && IsEqualGUID(iid, rt::GuidOf(ampify::amp::kIidPlayerStatics))) {
        *out = &staticsF;
        return S_OK;
    }
    if (name == L"AMP.Services.PlayQueue" && IsEqualGUID(iid, rt::GuidOf(ampify::amp::kIidPlayQueueStatics)) &&
        !g.noQueue) {
        *out = &queueStaticsF;
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
    options.onInterfaceThread = [](std::function<void()> work, unsigned) {
        work();
        return true;
    };
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
    CHECK(reading->title == "Title \xC3\xA9");
    CHECK(reading->artist == "Artiste");
    CHECK(reading->album == "Album");
    CHECK(reading->subtitle == "Artiste — Album");
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

TEST_CASE(AmpPlayer_ReadsTheCoverAndTheLyricsOfTheItem) {
    Reset();
    Hook hook(FakeActivation);
    ampify::win::AmpPlayer player;
    std::string failure;
    CHECK(player.Activate(failure));
    std::optional<ampify::AmpReading> reading = player.Read();
    CHECK(reading.has_value());
    CHECK(reading->artwork.uri == "https://is1-ssl.mzstatic.com/image/thumb/Music/a.jpg/{w}x{h}{c}.{f}");
    CHECK(reading->artwork.format == "jpg" && reading->artwork.cropCode == "bb");
    CHECK(reading->artwork.width == 600 && reading->artwork.height == 600);
    CHECK(reading->artwork.hasColor && reading->artwork.alpha == 255 && reading->artwork.red == 0x33 &&
          reading->artwork.green == 0x66 && reading->artwork.blue == 0x99);
    CHECK(reading->lyricsAvailable);
    CHECK(player.Lyrics().has_value() && player.Lyrics()->language == "en");
    CHECK(player.Lyrics()->ttml.find("Hello") != std::string::npos);
    CHECK(player.Lyrics()->staticText == "First line\nSecond line");

    // Cached for the track: reading again does not ask Apple again.
    int asked = g.lyricsAsked;
    CHECK(player.Read().has_value());
    CHECK(g.lyricsAsked == asked);
    CHECK(g.unexpected == 0);
}

TEST_CASE(AmpPlayer_ASecondTrackIsReadAgainAndLyricsLateAreRetried) {
    Reset();
    Hook hook(FakeActivation);
    ampify::win::AmpPlayer player;
    std::string failure;
    CHECK(player.Activate(failure));
    g.haveLyrics = 0;
    std::optional<ampify::AmpReading> reading = player.Read();
    CHECK(reading.has_value() && !reading->lyricsAvailable && !player.Lyrics().has_value());
    CHECK(g.lyricsAsked == 1);
    CHECK(player.Read().has_value() && g.lyricsAsked == 1);  // not before the retry delay

    // A new queue entry resets the cache and asks at once.
    g.haveLyrics = 1;
    g.queueId = 43;
    reading = player.Read();
    CHECK(reading.has_value() && reading->lyricsAvailable && g.lyricsAsked == 2);

    // Lyrics that show up some seconds later are found by the retry.
    g.queueId = 44;
    g.haveLyrics = 0;
    CHECK(!player.Read()->lyricsAvailable);
    g.haveLyrics = 1;
    Sleep(3200);
    CHECK(player.Read()->lyricsAvailable);
    CHECK(g.unexpected == 0);
}

TEST_CASE(AmpPlayer_ASkippedCoverOrLyricsInfoLeavesTheOtherFieldsAlone) {
    Reset();
    Hook hook(FakeActivation);
    ampify::win::AmpPlayer player;
    std::string failure;
    CHECK(player.Activate(failure));
    g.noArtwork = true;
    g.lyricsInfoNull = true;
    std::optional<ampify::AmpReading> reading = player.Read();
    CHECK(reading.has_value() && reading->hasItem);
    CHECK(reading->artwork.uri.empty() && !reading->artwork.hasColor);
    CHECK(!reading->lyricsAvailable);
    CHECK(reading->title == "Title \xC3\xA9");
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
    CHECK(g.play == 1 && g.pause == 1 && g.next == 1 && g.previous == 1 && g.seekTicks == -1);
    // SetPositionAsync(TimeSpan, scrubbing): 100 ns ticks, and a jump rather than a drag of the scrubber.
    CHECK(player.Command(ampify::PlayerCommand::Seek, 14'000));
    CHECK(g.seekTicks == 140'000'000 && g.seekScrubbing == 0);
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

TEST_CASE(MakeVector_GivesItsValuesThroughEveryCollectionInterfaceAndRefusesChanges) {
    const char* const(&ids)[4] = ampify::amp::kIidsUInt64Collections;
    rt::VectorIids iids{rt::GuidOf(ids[0]), rt::GuidOf(ids[1]), rt::GuidOf(ids[2]), rt::GuidOf(ids[3])};
    rt::Ref vector = rt::MakeVector(iids, std::vector<uint64_t>{7, 8, 9});
    UINT32 size = 0;
    uint64_t value = 0;
    CHECK(SUCCEEDED(rt::CallSlot(vector, rt::kVectorSizeSlot, &size)) && size == 3);
    CHECK(SUCCEEDED(rt::CallSlot(vector, rt::kVectorGetAtSlot, 1u, &value)) && value == 8);
    CHECK(rt::CallSlot(vector, rt::kVectorGetAtSlot, 3u, &value) == E_BOUNDS);
    CHECK(rt::CallSlot(vector, 13, uint64_t{1}) == E_ACCESSDENIED);  // IVector.Append

    rt::Ref view, iterable, iterator, agile;
    uint64_t many[4] = {};
    UINT32 actual = 0;
    CHECK(rt::Query(vector, iids.view, view));
    CHECK(SUCCEEDED(rt::CallSlot(view, 9, 1u, 4u, many, &actual)) && actual == 2 && many[1] == 9);  // GetMany
    CHECK(rt::Query(vector, iids.iterable, iterable));
    CHECK(SUCCEEDED(rt::CallSlot(iterable, 6, iterator.Put())) && iterator);  // IIterable.First
    unsigned char has = 0;
    CHECK(SUCCEEDED(rt::CallSlot(iterator, 6, &value)) && value == 7);  // IIterator.get_Current
    CHECK(SUCCEEDED(rt::CallSlot(iterator, 8, &has)) && has == 1);     // MoveNext
    CHECK(SUCCEEDED(rt::CallSlot(iterator, 8, &has)) && has == 1);
    CHECK(SUCCEEDED(rt::CallSlot(iterator, 8, &has)) && has == 0);
    CHECK(rt::Query(vector, rt::GuidOf("94EA2B94-E9CC-49E0-C0FF-EE64CA8F5B90"), agile));  // IAgileObject
    CHECK(!rt::Query(vector, rt::GuidOf(ampify::amp::kIidVectorOfComponentModel), agile));
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
    CHECK(log.str().find("AMP.Services.Player not activated") != std::string::npos);
    CHECK(log.str().find("falling back to Windows media controls (SMTC)") != std::string::npos);
    CHECK(log.str().find("SMTC API not available") != std::string::npos);

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
    CHECK(s.track.has_value() && s.track->title == "Title \xC3\xA9" && s.track->lossless && !s.track->atmos);
    CHECK(s.positionMs == 60'000 && s.durationMs == 180'000);
    CHECK(s.track->queueItemId == "42");
    CHECK(s.track->artwork == "https://is1-ssl.mzstatic.com/image/thumb/Music/a.jpg/1000x1000bb.jpg");
    CHECK(s.track->artworkColor == "#336699");
    CHECK(s.lyricsAvailable);
    CHECK(bridge.Lyrics().has_value() && bridge.Lyrics()->language == "en");
    CHECK(log.str().find("AMP.Services.Player activated (test") != std::string::npos);
    CHECK(log.str().find("first state read by AMP.Services.Player: playing=yes") != std::string::npos);

    // player.appleCommands is off: pause goes to the media key, the state being known (playing).
    CHECK(bridge.Control(ampify::PlayerCommand::Pause, 0));
    CHECK(g.pause == 0);
    CHECK(keys.size() == 1 && keys[0] == VK_MEDIA_PLAY_PAUSE);
    CHECK(bridge.Control(ampify::PlayerCommand::Play, 0));  // already playing: nothing sent
    CHECK(keys.size() == 1 && g.play == 0);

    // Seek goes to Apple's player even so: Apple Music ignores the SMTC one.
    CHECK(bridge.Control(ampify::PlayerCommand::Seek, 14'000));
    CHECK(g.seekTicks == 140'000'000 && keys.size() == 1);

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
    CHECK(log.str().find("lives in AppleMusic.exe, it is not used here") != std::string::npos);
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
    CHECK(log.str().find("turned off by the player.apple setting") != std::string::npos);
}

TEST_CASE(PlayerBridge_ListsApplesWholeQueueSectionBySection) {
    Reset();
    Hook hook(FakeActivation);
    ampify::Settings settings;
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("t"); });
    std::vector<unsigned short> keys;
    ampify::win::PlayerBridge bridge("ui", settings, journal, TestOptions(&keys));
    ampify::QueueSnapshot queue = bridge.Queue();
    CHECK(queue.complete);
    CHECK(queue.source == "apple");
    CHECK(queue.items.size() == 4);
    CHECK(queue.items[0].id == "101" && queue.items[0].section == ampify::QueueSection::History);
    CHECK(queue.items[1].title == "Now" && queue.items[1].section == ampify::QueueSection::Current);
    CHECK(queue.items[2].artist == "Artist C" && queue.items[2].durationMs == 240'000);
    CHECK(queue.items[2].catalogId == "1003" && queue.items[2].section == ampify::QueueSection::Next);
    CHECK(queue.items[3].catalogId.empty());
    CHECK(log.str().find("queue read through AMP.Services.PlayQueue (4 entries)") != std::string::npos);
    CHECK(g.unexpected == 0);
}

TEST_CASE(AmpItem_ReadsTheIdentifiersAndTheKindOfAnItemsDescriptor) {
    Reset();
    Build();
    rt::Ref soon(&entries[2].item);
    auto song = ampify::win::ReadItemInfo(soon, "Soon");
    CHECK(song && song->id == "1003" && song->canonicalId == "c:1003" && song->kind == "song");
    CHECK(ampify::win::ReadItemInfo(soon, ""));  // no name to check: the descriptor alone
    const struct {
        int slot;
        const char* kind;
    } kinds[] = {{11, "album"},  {12, "artist"},   {13, "composer"}, {14, "genre"},  {15, "playlist"},
                 {16, "station"}, {18, "container"}, {19, "video"},    {20, "concert"}};
    for (const auto& expected : kinds) {
        g.descriptorFlag = expected.slot;
        auto info = ampify::win::ReadItemInfo(soon, "Soon");
        CHECK(info && info->kind == expected.kind);
    }
    CHECK(g.unexpected == 0);
}

TEST_CASE(AmpItem_ADescriptorOfAnotherNameIsNotTheItemTheMenuIsAbout) {
    Reset();
    Build();
    g.descriptorFlag = 15;  // a playlist: what the page around a row is
    CHECK(!ampify::win::ReadItemInfo(rt::Ref(&entries[2].item), "Another name"));
    auto page = ampify::win::ReadItemInfo(rt::Ref(&entries[2].item), "Soon");
    CHECK(page && page->kind == "playlist");
}

TEST_CASE(AmpItem_SaysNothingForAnObjectWithoutADescriptorAndKeepsWhatIsThere) {
    Reset();
    Build();
    CHECK(!ampify::win::ReadItemInfo(rt::Ref(&itemF), "Now"));  // no IContentDescriptorDriven
    auto plain = ampify::win::ReadItemInfo(rt::Ref(&entries[3].item), "Later");
    CHECK(plain && plain->kind == "song" && plain->id.empty() && plain->canonicalId.empty());  // named, no identifier
    g.descriptorFlag = 11;
    auto album = ampify::win::ReadItemInfo(rt::Ref(&entries[3].item), "Later");
    CHECK(album && album->kind == "album" && album->id.empty());  // the kind alone is still something
    CHECK(!ampify::win::ReadItemInfo(rt::Ref(), "Later"));
}

TEST_CASE(PlayerBridge_SaysWhenTheQueueIsLongerThanWhatItReads) {
    Reset();
    g.longerNext = 600;
    Hook hook(FakeActivation);
    ampify::Settings settings;
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("t"); });
    std::vector<unsigned short> keys;
    ampify::win::PlayerBridge bridge("ui", settings, journal, TestOptions(&keys));
    ampify::QueueSnapshot queue = bridge.Queue();
    CHECK(!queue.complete);
    CHECK(queue.items.size() == ampify::win::kMaxQueueEntries);
    CHECK(queue.reason.find("first 500") != std::string::npos);
    CHECK(g.unexpected == 0);

    g.longerNext = static_cast<UINT32>(ampify::win::kMaxQueueEntries) - 4;  // exactly 500 entries
    CHECK(bridge.Queue().complete);
}

TEST_CASE(PlayerBridge_NamesAChangedQueueCollectionInsteadOfThePanel) {
    Reset();
    g.sectionsChanged = true;
    Hook hook(FakeActivation);
    ampify::Settings settings;
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("t"); });
    std::vector<unsigned short> keys;
    ampify::win::PlayerBridge bridge("ui", settings, journal, TestOptions(&keys));
    ampify::QueueSnapshot queue = bridge.Queue();
    CHECK(!queue.complete);
    CHECK(queue.reason.find("changed") != std::string::npos);
    CHECK(queue.reason.find("panel") == std::string::npos);
    CHECK(g.unexpected == 0);
}

TEST_CASE(PlayerBridge_RemovesOnlyAnEntryStillToCome) {
    Reset();
    Hook hook(FakeActivation);
    ampify::Settings settings;
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("t"); });
    std::vector<unsigned short> keys;
    ampify::win::PlayerBridge bridge("ui", settings, journal, TestOptions(&keys));
    CHECK(bridge.RemoveFromQueue("103").ok);
    CHECK(g.deleted == std::vector<uint64_t>{103});
    ampify::QueueResult current = bridge.RemoveFromQueue("102");
    CHECK(!current.ok && current.reason.find("still to come") != std::string::npos);
    CHECK(!bridge.RemoveFromQueue("101").ok);
    ampify::QueueResult unknown = bridge.RemoveFromQueue("999");
    CHECK(!unknown.ok && unknown.reason.find("no entry 999") != std::string::npos);
    CHECK(g.deleted.size() == 1);
    CHECK(g.unexpected == 0);
}

TEST_CASE(PlayerBridge_RefusesToAddWithAReason) {
    Reset();
    Hook hook(FakeActivation);
    ampify::Settings settings;
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("t"); });
    std::vector<unsigned short> keys;
    ampify::win::PlayerBridge bridge("ui", settings, journal, TestOptions(&keys));
    ampify::QueueResult added = bridge.AddToQueue("1440833098", ampify::QueuePosition::Next);
    CHECK(!added.ok && added.reason.find("not possible") != std::string::npos);
    ampify::QueueResult wrong = bridge.AddToQueue("abc", ampify::QueuePosition::Next);
    CHECK(!wrong.ok && wrong.reason.find("catalog id") != std::string::npos);
    CHECK(g.unexpected == 0);
}

TEST_CASE(PlayerBridge_WithoutApplesQueueTheCurrentTrackIsAllThereIs) {
    Reset();
    g.noQueue = true;
    Hook hook(FakeActivation);
    ampify::Settings settings;
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("t"); });
    std::vector<unsigned short> keys;
    ampify::win::PlayerBridge bridge("ui", settings, journal, TestOptions(&keys));
    ampify::QueueSnapshot queue = bridge.Queue();
    CHECK(!queue.complete);
    CHECK(queue.source == "apple");
    CHECK(queue.reason.find("AMP.Services.PlayQueue not found") != std::string::npos);
    CHECK(queue.items.size() == 1);
    CHECK(queue.items[0].id == "42" && queue.items[0].section == ampify::QueueSection::Current);
    ampify::QueueResult removed = bridge.RemoveFromQueue("42");
    CHECK(!removed.ok && removed.reason.find("AMP.Services.PlayQueue not found") != std::string::npos);
    CHECK(g.deleted.empty());
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
    CHECK(host.Capability<ampify::LyricsHost>() != nullptr);
}
