// AutoMix on Apple Music for Windows. Apple's queue player (an AVCFQueuePlayer of AVFoundationCF.dll, in
// AMPLibraryAgent.exe) moves on to the next song while a second AVCFPlayer of Ampify's own plays the end of the
// current one, on the same asset, into an output stream of its own that the audio hooks keep silent: its samples go
// to the deck (src/core/automix/mix_deck.hpp), which mixes them into Apple's stream. What this rests on was measured
// on 12/11/2026 and is written up in knowledge/apple-music-playback.md: a player sent to a device by name
// (AVCFPlayerSetAudioDeviceUID) gets a stream of its own; audio mixes, processing taps, rates and track switches do
// nothing for Apple Music's streams, so volumes, filters and speed are the deck's; a forward end time on the current
// item with a seek of the queued next one switches without a gap. Nothing here touches FairPlay: the second player
// uses Apple's own asset through the public player API, and the deck sees decoded samples on their way out.

#include "automix.hpp"

#include <windows.h>
#include <mmdeviceapi.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include "../../../extensions/atmos-leveler/format_detection.hpp"
#include "../../core/automix/mix_deck.hpp"
#include "../hook.hpp"
#include "hooks_detail.hpp"

namespace ampify::win::audio_hooks {

namespace {

using detail::AddressOf;
using detail::FromAddress;
using detail::Note;

struct CMTime {
    std::int64_t value;
    std::int32_t timescale;
    std::uint32_t flags;
    std::int64_t epoch;
};

constexpr std::uint32_t kTimeValid = 1;
constexpr unsigned kUtf8 = 0x08000100;

CMTime Seconds(double seconds) {
    return CMTime{static_cast<std::int64_t>(std::llround(seconds * 1e6)), 1000000, kTimeValid, 0};
}

std::optional<double> ToSeconds(const CMTime& time) {
    if (!(time.flags & kTimeValid) || time.timescale <= 0) return std::nullopt;
    return static_cast<double>(time.value) / time.timescale;
}

// AVFoundationCF and CoreFoundation, read once. A missing function leaves AutoMix unavailable, never half working.
struct Api {
    void*(__cdecl* copyCurrentItem)(void*) = nullptr;
    const void*(__cdecl* copyItems)(void*) = nullptr;
    void*(__cdecl* itemAsset)(void*) = nullptr;
    CMTime(__cdecl* itemTime)(void*) = nullptr;
    CMTime(__cdecl* itemDuration)(void*) = nullptr;
    void(__cdecl* seekItem)(void*, CMTime) = nullptr;
    void(__cdecl* seekItemTolerance)(void*, CMTime, CMTime, CMTime) = nullptr;  // time, tolerance before and after
    void(__cdecl* endTime)(void*, CMTime) = nullptr;
    void*(__cdecl* createItem)(void*, void*, void*) = nullptr;
    void*(__cdecl* createPlayer)(void*, void*, void*, void*) = nullptr;
    void(__cdecl* seekPlayer)(void*, CMTime) = nullptr;
    void(__cdecl* play)(void*) = nullptr;
    void(__cdecl* pause)(void*) = nullptr;
    float(__cdecl* rate)(void*) = nullptr;
    CMTime(__cdecl* playerTime)(void*) = nullptr;
    void(__cdecl* setVolume)(void*, float) = nullptr;
    void(__cdecl* setDevice)(void*, const void*) = nullptr;
    const void*(__cdecl* copyUrl)(void*) = nullptr;
    long long(__cdecl* arrayCount)(const void*) = nullptr;
    const void*(__cdecl* arrayAt)(const void*, long long) = nullptr;
    const void*(__cdecl* urlString)(const void*) = nullptr;
    unsigned char(__cdecl* cstring)(const void*, char*, long long, unsigned) = nullptr;
    const void*(__cdecl* makeString)(const void*, const wchar_t*, long long) = nullptr;
    const void*(__cdecl* retain)(const void*) = nullptr;
    void(__cdecl* release)(const void*) = nullptr;
    const void*(__cdecl* copyTracks)(void*) = nullptr;
    const void*(__cdecl* copyTrackAsset)(const void*) = nullptr;
    const void*(__cdecl* copyFormats)(const void*) = nullptr;
    const void*(__cdecl* copyDescription)(const void*) = nullptr;
    // Optional: without them the second player plays the stream's first version, not Apple's player's.
    unsigned long long(__cdecl* variantPrefs)(void*) = nullptr;
    void(__cdecl* setVariantPrefs)(void*, unsigned long long) = nullptr;
    bool ready = false;
};

const Api& A() {
    static const Api api = [] {
        Api a;
        HMODULE av = GetModuleHandleW(L"AVFoundationCF.dll");
        HMODULE cf = GetModuleHandleW(L"CoreFoundation.dll");
        if (!av || !cf) return a;
        auto get = [](HMODULE module, const char* name) { return reinterpret_cast<void*>(GetProcAddress(module, name)); };
        a.copyCurrentItem = FromAddress<decltype(a.copyCurrentItem)>(get(av, "AVCFPlayerCopyCurrentItem"));
        a.copyItems = FromAddress<decltype(a.copyItems)>(get(av, "AVCFQueuePlayerCopyCurrentItems"));
        a.itemAsset = FromAddress<decltype(a.itemAsset)>(get(av, "AVCFPlayerItemGetAsset"));
        a.itemTime = FromAddress<decltype(a.itemTime)>(get(av, "AVCFPlayerItemGetCurrentTime"));
        a.itemDuration = FromAddress<decltype(a.itemDuration)>(get(av, "AVCFPlayerItemGetDuration"));
        a.seekItem = FromAddress<decltype(a.seekItem)>(get(av, "AVCFPlayerItemSeekToTime"));
        a.seekItemTolerance =
            FromAddress<decltype(a.seekItemTolerance)>(get(av, "AVCFPlayerItemSeekToTimeWithTolerance"));
        a.endTime = FromAddress<decltype(a.endTime)>(get(av, "AVCFPlayerItemSetForwardPlaybackEndTime"));
        a.createItem = FromAddress<decltype(a.createItem)>(get(av, "AVCFPlayerItemCreateWithAsset"));
        a.createPlayer = FromAddress<decltype(a.createPlayer)>(get(av, "AVCFPlayerCreateWithPlayerItemAndOptions"));
        a.seekPlayer = FromAddress<decltype(a.seekPlayer)>(get(av, "AVCFPlayerSeekToTime"));
        a.play = FromAddress<decltype(a.play)>(get(av, "AVCFPlayerPlay"));
        a.pause = FromAddress<decltype(a.pause)>(get(av, "AVCFPlayerPause"));
        a.rate = FromAddress<decltype(a.rate)>(get(av, "AVCFPlayerGetRate"));
        a.playerTime = FromAddress<decltype(a.playerTime)>(get(av, "AVCFPlayerGetCurrentTime"));
        a.setVolume = FromAddress<decltype(a.setVolume)>(get(av, "AVCFPlayerSetVolume"));
        a.setDevice = FromAddress<decltype(a.setDevice)>(get(av, "AVCFPlayerSetAudioDeviceUID"));
        a.copyUrl = FromAddress<decltype(a.copyUrl)>(get(av, "AVCFURLAssetCopyURL"));
        a.arrayCount = FromAddress<decltype(a.arrayCount)>(get(cf, "CFArrayGetCount"));
        a.arrayAt = FromAddress<decltype(a.arrayAt)>(get(cf, "CFArrayGetValueAtIndex"));
        a.urlString = FromAddress<decltype(a.urlString)>(get(cf, "CFURLGetString"));
        a.cstring = FromAddress<decltype(a.cstring)>(get(cf, "CFStringGetCString"));
        a.makeString = FromAddress<decltype(a.makeString)>(get(cf, "CFStringCreateWithCharacters"));
        a.retain = FromAddress<decltype(a.retain)>(get(cf, "CFRetain"));
        a.release = FromAddress<decltype(a.release)>(get(cf, "CFRelease"));
        a.copyTracks = FromAddress<decltype(a.copyTracks)>(get(av, "AVCFPlayerItemCopyTracks"));
        a.copyTrackAsset = FromAddress<decltype(a.copyTrackAsset)>(get(av, "AVCFPlayerItemTrackCopyAssetTrack"));
        a.copyFormats = FromAddress<decltype(a.copyFormats)>(get(av, "AVCFAssetTrackCopyFormatDescriptions"));
        a.copyDescription = FromAddress<decltype(a.copyDescription)>(get(cf, "CFCopyDescription"));
        a.variantPrefs = FromAddress<decltype(a.variantPrefs)>(get(av, "AVCFPlayerItemGetVariantPreferences"));
        a.setVariantPrefs = FromAddress<decltype(a.setVariantPrefs)>(get(av, "AVCFPlayerItemSetVariantPreferences"));
        a.ready = a.copyTracks && a.copyTrackAsset && a.copyFormats && a.copyDescription && a.copyCurrentItem &&
                  a.copyItems && a.itemAsset && a.itemTime && a.itemDuration && a.seekItem &&
                  a.endTime && a.createItem && a.createPlayer && a.seekPlayer && a.play && a.pause && a.rate &&
                  a.playerTime && a.setVolume && a.setDevice && a.copyUrl && a.arrayCount && a.arrayAt &&
                  a.urlString && a.cstring && a.makeString && a.retain && a.release;
        return a;
    }();
    return api;
}

// A CoreFoundation reference released at the end of the scope.
class Ref {
public:
    explicit Ref(const void* object = nullptr) : m_object(object) {}
    ~Ref() {
        if (m_object) A().release(m_object);
    }
    Ref(const Ref&) = delete;
    Ref& operator=(const Ref&) = delete;
    const void* get() const { return m_object; }
    void* item() const { return const_cast<void*>(m_object); }
    explicit operator bool() const { return m_object != nullptr; }

private:
    const void* m_object;
};

// Apple's queue player and what it was made with, seen when Apple creates it. Kept alive here.
std::atomic<void*> g_queue{nullptr};
std::atomic<void*> g_options{nullptr};
std::atomic<void*> g_notify{nullptr};

// The song a queue item plays, named by its stream address without the query (which carries per-session values).
std::string NameOf(void* item) {
    if (!item) return {};
    void* asset = A().itemAsset(item);
    if (!asset) return {};
    Ref url(A().copyUrl(asset));
    if (!url) return {};
    const void* text = A().urlString(url.get());
    char buffer[2048] = {};
    if (!text || !A().cstring(text, buffer, sizeof(buffer), kUtf8)) return {};
    std::string name = buffer;
    return name.substr(0, name.find('?'));
}

// AVCFPlayerItemSeekToTime allows any tolerance and lands on a packet of the stream: measured on 12/11/2026, the next
// song of a mix came in 49 ms behind its point, an audible flam on the beat. A zero tolerance lands on the sample.
void SeekItemExactly(void* item, double seconds) {
    const CMTime zero{0, 1, kTimeValid, 0};
    if (A().seekItemTolerance) {
        A().seekItemTolerance(item, Seconds(seconds), zero, zero);
    } else {
        A().seekItem(item, Seconds(seconds));
    }
}

double DurationOf(void* item) {
    std::optional<double> seconds = ToSeconds(A().itemDuration(item));
    return seconds && std::isfinite(*seconds) && *seconds > 0 ? *seconds : 0;
}

// Who a song is, from the options Apple made its asset with: under the alternative configuration, MediaIdentifier
// ("aadamid=1607320391,adamid=1607320391,sadamid=1607320391,": the catalog id) and StorefrontID ("143442-3,42": the
// store, France here). Optional: AutoMix plays without it, only less well.
struct SongIdentity {
    std::string catalogId;
    std::string storefront;  // the store's number, such as 143442
};

SongIdentity IdentityOf(void* item) {
    struct Keys {
        const void*(__cdecl* copyOptions)(void*) = nullptr;
        const void*(__cdecl* value)(const void*, const void*) = nullptr;
        const void* alternative = nullptr;
        const void* media = nullptr;
        const void* storefront = nullptr;
    };
    static const Keys keys = [] {
        Keys k;
        HMODULE av = GetModuleHandleW(L"AVFoundationCF.dll");
        HMODULE cf = GetModuleHandleW(L"CoreFoundation.dll");
        if (!av || !cf) return k;
        auto constant = [av](const char* name) {
            void* address = reinterpret_cast<void*>(GetProcAddress(av, name));
            return address ? *static_cast<const void* const*>(address) : nullptr;
        };
        k.copyOptions = FromAddress<decltype(k.copyOptions)>(
            reinterpret_cast<void*>(GetProcAddress(av, "AVCFURLAssetCopyCreationOptions")));
        k.value = FromAddress<decltype(k.value)>(reinterpret_cast<void*>(GetProcAddress(cf, "CFDictionaryGetValue")));
        k.alternative = constant("AVCFURLAssetAlternativeConfigurationOptionsKey");
        k.media = constant("AVCFURLAssetAlternativeConfigurationMediaIdentifierKey");
        k.storefront = constant("AVCFURLAssetAlternativeConfigurationStorefrontIDKey");
        return k;
    }();
    SongIdentity identity;
    if (!item || !keys.copyOptions || !keys.value || !keys.alternative || !keys.media) return identity;
    void* asset = A().itemAsset(item);
    if (!asset) return identity;
    Ref options(keys.copyOptions(asset));
    const void* alternative = options ? keys.value(options.get(), keys.alternative) : nullptr;
    if (!alternative) return identity;
    auto text = [&](const void* key) -> std::string {
        const void* value = key ? keys.value(alternative, key) : nullptr;
        char buffer[256] = {};
        if (!value || !A().cstring(value, buffer, sizeof(buffer), kUtf8)) return {};
        return buffer;
    };
    // "adamid=" itself, not the end of "aadamid=" or "sadamid="
    std::string media = "," + text(keys.media);
    size_t at = media.find(",adamid=");
    if (at != std::string::npos) {
        size_t start = at + 8;
        size_t end = media.find(',', start);
        identity.catalogId = media.substr(start, end == std::string::npos ? std::string::npos : end - start);
    }
    std::string store = text(keys.storefront);
    identity.storefront = store.substr(0, store.find_first_not_of("0123456789"));
    bool digits = !identity.catalogId.empty() &&
                  identity.catalogId.find_first_not_of("0123456789") == std::string::npos;
    if (!digits) identity.catalogId.clear();
    return identity;
}

// Whether the item's sound is Dolby Atmos, once its tracks are loaded (the next song's are not): Apple's player then
// renders spatial objects and no stereo stream. The format descriptions of its tracks tell
// (atmos-leveler/format_detection.hpp).
std::optional<bool> InAtmos(void* item) {
    Ref tracks(A().copyTracks(item));
    if (!tracks) return std::nullopt;
    std::vector<std::string> descriptions;
    for (long long i = 0; i < A().arrayCount(tracks.get()); i++) {
        Ref asset(A().copyTrackAsset(A().arrayAt(tracks.get(), i)));
        if (!asset) continue;
        Ref formats(A().copyFormats(asset.get()));
        if (!formats) continue;
        for (long long j = 0; j < A().arrayCount(formats.get()); j++) {
            Ref text(A().copyDescription(A().arrayAt(formats.get(), j)));
            char buffer[1024] = {};
            if (text && A().cstring(text.get(), buffer, sizeof(buffer), kUtf8)) descriptions.emplace_back(buffer);
        }
    }
    const atmos::TrackKind kind = atmos::ClassifyDescriptions(descriptions);
    if (kind == atmos::TrackKind::Unknown) return std::nullopt;
    return kind == atmos::TrackKind::Atmos;
}

double SteadySeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// Where both songs become one sound. The audio threads reach it through detail::AutoMixMain and AutoMixTail, and
// only while g_deckOpen says there is a mix: the rest of the time Apple's stream costs one load.
automix::MixDeck g_deck;
std::atomic<bool> g_deckOpen{false};

// How evenly the second player's stream is fed during a mix. The old song leaves in its blocks: a block that comes
// late is a hole in the old song, which no recording of the stream itself would show.
struct Feed {
    std::atomic<bool> live{false};  // the old song is leaving in the blocks that come
    std::atomic<double> start{0};   // steady seconds at the first of them, 0 before any
    std::atomic<long long> frames{0};
    std::atomic<double> earliest{0};  // a block's arrival against an even feed, seconds
    std::atomic<double> latest{0};
};
Feed g_feed;

// The default output's id, as Windows names it: a player sent there by name gets a stream of its own.
std::wstring DefaultEndpoint() {
    std::wstring id;
    HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDeviceEnumerator* devices = nullptr;
    IMMDevice* device = nullptr;
    LPWSTR text = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                   reinterpret_cast<void**>(&devices))) &&
        SUCCEEDED(devices->GetDefaultAudioEndpoint(eRender, eMultimedia, &device)) && SUCCEEDED(device->GetId(&text))) {
        id = text;
    }
    if (text) CoTaskMemFree(text);
    if (device) device->Release();
    if (devices) devices->Release();
    if (SUCCEEDED(com)) CoUninitialize();
    return id;
}

// The two players and the deck of a mix, as the session drives them (mix_session.hpp). Called with the session's
// lock held.
class AvcfDecks : public automix::MixDecks {
public:
    // The queue's current (A) and next (B) items when the plan was taken. Retained until the next plan.
    void Songs(void* current, void* next) {
        Hold(m_source, current);
        Hold(m_target, next);
    }

    std::optional<double> MainTime() override {
        void* queue = g_queue.load();
        if (!queue) return std::nullopt;
        Ref item(A().copyCurrentItem(queue));
        if (!item) return std::nullopt;
        return ToSeconds(A().itemTime(item.item()));
    }

    std::string NextItem() override {
        void* queue = g_queue.load();
        if (!queue) return {};
        Ref items(A().copyItems(queue));
        if (!items || A().arrayCount(items.get()) < 2) return {};
        return NameOf(const_cast<void*>(A().arrayAt(items.get(), 1)));
    }

    bool MainOnNext() override {
        void* queue = g_queue.load();
        if (!queue || !m_target) return false;
        Ref item(A().copyCurrentItem(queue));
        return item.get() == m_target;
    }

    bool StartGhost(const automix::MixPlan& plan) override {
        StopGhost();
        const int rate = detail::MainStreamRate();
        void* asset = m_source ? A().itemAsset(m_source) : nullptr;
        const std::wstring endpoint = DefaultEndpoint();
        if (!asset || rate == 0 || endpoint.empty()) return false;
        m_ghostItem = A().createItem(nullptr, asset, g_notify.load());
        if (!m_ghostItem) return false;
        // The version of the song Apple's player plays, lossless when it does: left to itself the item plays the
        // stream's first, lossy one (seen: Apple's on its lossless variant, this one on the first compressed).
        if (A().variantPrefs && A().setVariantPrefs) A().setVariantPrefs(m_ghostItem, A().variantPrefs(m_source));
        m_ghost = A().createPlayer(nullptr, m_ghostItem, g_options.load(), g_notify.load());
        if (!m_ghost) {
            A().release(m_ghostItem);
            m_ghostItem = nullptr;
            return false;
        }
        // Sent to the default output by name, as Apple's own player is not. Silent until its stream is seen and is
        // the deck's (SeekGhost): from then on only what the deck writes there is heard.
        Ref uid(A().makeString(nullptr, endpoint.c_str(), static_cast<long long>(endpoint.size())));
        A().setDevice(m_ghost, uid.get());
        A().setVolume(m_ghost, 0.0f);
        g_deck.Open(rate, automix::HoldFor(plan));
        if (automix::Stretches(plan)) g_deck.SetStretch(automix::CarryFrom(plan), plan.keys.front().at, plan.rate);
        g_feed.start = 0;
        g_feed.live = false;
        g_deckOpen = true;
        detail::ExpectTailStreams(true);
        A().play(m_ghost);
        return true;
    }

    std::optional<double> GhostTime() override {
        if (!m_ghost) return std::nullopt;
        return ToSeconds(A().playerTime(m_ghost));
    }

    bool GhostCaptured() override { return detail::TailStreamsClaimed() && g_deck.TailSeconds() > 0; }

    void SeekGhost(double to) override {
        if (!m_ghost) return;
        // Exactly: with the default tolerance a seek far from what is loaded lands on the start of a segment of the
        // stream, seconds before its target (3.4 s measured on 12/11/2026).
        SeekItemExactly(m_ghostItem, to);
        A().setVolume(m_ghost, 1.0f);  // as decoded: the deck sets its level against Apple's own
    }

    void StopGhost() override {
        if (m_ghost) {
            A().setVolume(m_ghost, 0.0f);
            A().pause(m_ghost);
            A().release(m_ghost);
        }
        if (m_ghostItem) A().release(m_ghostItem);
        m_ghost = m_ghostItem = nullptr;
        g_deckOpen = false;
        g_deck.Close();
        detail::ForgetTailStreams();
    }

    std::optional<automix::DeckStep> Align() override {
        std::optional<double> ghost = GhostTime();
        std::optional<double> main = MainTime();
        if (!ghost || !main) return std::nullopt;
        return g_deck.Align(*ghost - *main, *main, detail::LastPlayerVolume());
    }

    std::optional<double> HeardTime() override { return g_deck.Time(); }

    double CopyErrorDb() override { return g_deck.ResidualDb(); }

    std::optional<double> SwitchGap() override { return g_deck.SwitchGap(); }

    void SetGains(const automix::DeckGains& gains) override { g_deck.SetGains(gains, detail::LastPlayerVolume()); }

    bool ArmSwitch(double aEnd, double bAt) override {
        void* queue = g_queue.load();
        if (!queue || !m_target || !m_source) return false;
        Disarm(false);  // B is sought to its point just below
        Ref current(A().copyCurrentItem(queue));
        if (current.get() != m_source) return false;  // the plan's song is no longer the one playing
        // The second player is where it must be and seeks no more: the next new stream is Apple's, for B.
        detail::ExpectTailStreams(false);
        m_armedItem = const_cast<void*>(A().retain(m_source));
        SeekItemExactly(m_target, bAt);
        A().endTime(m_armedItem, Seconds(aEnd));
        return true;
    }

    void Disarm(bool rewindNext) override {
        if (!m_armedItem) return;
        A().endTime(m_armedItem, CMTime{0, 0, 0, 0});  // invalid: no end before the song's own
        if (rewindNext && m_target) SeekItemExactly(m_target, 0);
        A().release(m_armedItem);
        m_armedItem = nullptr;
    }

private:
    static void Hold(void*& slot, void* item) {
        if (slot) A().release(slot);
        slot = item ? const_cast<void*>(A().retain(item)) : nullptr;
    }

    void* m_source = nullptr;
    void* m_target = nullptr;
    void* m_ghost = nullptr;
    void* m_ghostItem = nullptr;
    void* m_armedItem = nullptr;
};

class WinAutoMix : public AutoMixHost {
public:
    AutoMixState State() override {
        AutoMixState state;
        state.mix = m_session.GetStatus();
        void* queue = g_queue.load();
        if (!A().ready || !queue) return state;
        state.available = true;
        state.playing = A().rate(queue) != 0.0f;
        Ref items(A().copyItems(queue));
        long long count = items ? A().arrayCount(items.get()) : 0;
        if (count >= 1) {
            void* current = const_cast<void*>(A().arrayAt(items.get(), 0));
            std::optional<double> time = ToSeconds(A().itemTime(current));
            SongIdentity who = IdentityOf(current);
            state.current = AutoMixSong{NameOf(current), who.catalogId, who.storefront, time.value_or(0),
                                        DurationOf(current), InAtmos(current)};
        }
        if (count >= 2) {
            void* next = const_cast<void*>(A().arrayAt(items.get(), 1));
            SongIdentity who = IdentityOf(next);
            state.next = AutoMixSong{NameOf(next), who.catalogId, who.storefront, 0, DurationOf(next), InAtmos(next)};
        }
        return state;
    }

    std::string Schedule(const automix::MixPlan& plan) override {
        void* queue = g_queue.load();
        if (!A().ready || !queue) return "Apple's player is not ready";
        Ref items(A().copyItems(queue));
        if (!items || A().arrayCount(items.get()) < 2) return "nothing comes next";
        void* current = const_cast<void*>(A().arrayAt(items.get(), 0));
        void* next = const_cast<void*>(A().arrayAt(items.get(), 1));
        if (NameOf(next) != plan.next) return "the next song changed";
        // Dolby Atmos goes out as objects, not as a stereo stream the deck could mix into.
        if (detail::MainStreamRate() == 0) return "the sound does not go out as a stereo stream";
        std::string refused;
        {
            // Start first: it puts back what a replaced plan changed, on that plan's song; then the new target.
            std::lock_guard<std::mutex> guard(m_deckLock);
            refused = m_session.Start(plan, DurationOf(current), DurationOf(next));
            if (refused.empty()) m_decks.Songs(current, next);
        }
        if (!refused.empty()) return refused;
        m_interrupt = nullptr;  // a pause or a seek from before this plan says nothing about it
        m_plans++;
        Note("AutoMix: mix planned into the next song at " + detail::Fixed(plan.keys.front().at, 1) + " s");
        Wake();
        return {};
    }

    void Cancel(const std::string& reason) override {
        m_session.Cancel(reason);
        Wake();
    }

    std::uint64_t Plans() override { return m_plans.load(); }

    // Apple paused, sought, skipped or changed the queue: a mix in progress cannot stay in step. Called from Apple's
    // threads, inside its calls to AVFoundationCF: it takes no lock (the mix thread may be waiting on AVFoundationCF),
    // it only leaves a note the thread reads at its next tick.
    void Interrupt(const char* reason) {
        if (m_running.load()) m_interrupt = reason;
    }

private:
    static void Report(const automix::Status& status) {
        std::string line = "AutoMix: " + std::string(automix::PhaseName(status.phase));
        if (status.phase == automix::Phase::Ready && !std::isnan(status.leadSeconds)) {
            line += ", second player " + detail::Fixed(status.leadSeconds, 2) + " s ahead after " +
                    std::to_string(status.seeks) + " seek(s)";
        }
        if (status.phase == automix::Phase::TakingOver && !std::isnan(status.copyErrorDb)) {
            line += ", its copy within " + detail::Fixed(status.copyErrorDb, 1) + " dB of Apple's";
        }
        if (status.phase == automix::Phase::Done || status.phase == automix::Phase::Failed) {
            if (!std::isnan(status.switchLagMs)) {
                line += ", next song " + detail::Fixed(status.switchLagMs, 2) + " ms behind its point";
            }
            if (g_feed.start.load() != 0) {
                line += ", the old song's stream fed within " +
                        detail::Fixed((g_feed.latest.load() - g_feed.earliest.load()) * 1000, 1) + " ms";
            }
        }
        if (!status.reason.empty()) line += " (" + status.reason + ")";
        Note(line);
    }

    // One thread while a plan is alive, gone once the session rests.
    void Wake() {
        bool expected = false;
        if (!m_running.compare_exchange_strong(expected, true)) return;
        std::thread([this] {
            automix::Phase last = automix::Phase::Idle;
            while (true) {
                if (const char* reason = m_interrupt.exchange(nullptr)) m_session.Cancel(reason, true);
                {
                    std::lock_guard<std::mutex> guard(m_deckLock);
                    m_session.Tick(SteadySeconds());
                }
                automix::Status status = m_session.GetStatus();
                if (status.phase != last) {
                    last = status.phase;
                    Report(status);
                }
                if (!m_session.Active()) break;
                std::this_thread::sleep_for(std::chrono::duration<double>(m_session.TickInterval()));
            }
            {
                // Once more after a short while: a volume Apple set while the mix ended may carry the mix's share.
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                std::lock_guard<std::mutex> guard(m_deckLock);
                if (!m_session.Active()) m_decks.SetGains({});
            }
            m_running = false;
            m_interrupt = nullptr;
            // A plan taken while the thread was leaving would wait forever: look once more.
            if (m_session.Active()) Wake();
        }).detach();
    }

    AvcfDecks m_decks;
    static_assert(detail::kTailStreamSlots > automix::MixSession::kMaxSeeks, "a stream at the start and at each seek");
    automix::MixSession m_session{m_decks};
    std::mutex m_deckLock;  // Schedule and the thread both use the decks
    std::atomic<bool> m_running{false};
    std::atomic<const char*> m_interrupt{nullptr};  // a reason in static storage, from Apple's threads
    std::atomic<std::uint64_t> m_plans{0};
};

WinAutoMix& Instance() {
    static WinAutoMix* instance = new WinAutoMix();  // lives as long as the process: hooks may call it at exit
    return *instance;
}

// ---- Apple's calls: the queue player once, then what ends a mix in progress

using Generic_t = void*(__cdecl*)(void*, void*, void*, void*, void*, void*, void*, void*);
using SetRate_t = void(__cdecl*)(void*, float);
using Seek_t = void(__cdecl*)(void*, CMTime);

Generic_t g_createQueue = nullptr;
void* __cdecl CreateQueue_Hook(void* a, void* b, void* options, void* notify, void* e, void* f, void* g, void* h) {
    void* player = g_createQueue(a, b, options, notify, e, f, g, h);
    if (player) {
        // Apple's options dictionary is a temporary: the second player is made with it later, so it is kept. The
        // notification queue is a dispatch queue, not a CoreFoundation object (CFRetain on it crashes in objc.dll);
        // Apple keeps it for the life of the process.
        // shortcut: a second queue player would leave the first one and its options retained (one per process seen
        // so far); release them under the deck lock, with State() taking it too, if Apple ever makes several.
        if (options) A().retain(options);
        A().retain(player);
        g_options = options;
        g_notify = notify;
        g_queue = player;
    }
    return player;
}

SetRate_t g_setRate = nullptr;
void __cdecl SetRate_Hook(void* player, float rate) {
    g_setRate(player, rate);
    if (rate == 0.0f && player == g_queue.load()) Instance().Interrupt("paused");
}

Seek_t g_seekPlayer = nullptr;
// Apple's player makes a new stream when it seeks: while a mix expects the second player's, that one is Apple's.
void AppleSeeks() {
    if (g_deckOpen.load()) detail::ExpectTailStreams(false);
}

void __cdecl SeekPlayer_Hook(void* player, CMTime time) {
    if (player == g_queue.load()) AppleSeeks();
    g_seekPlayer(player, time);
    if (player == g_queue.load()) Instance().Interrupt("moved in the song");
}

Seek_t g_seekItem = nullptr;
void __cdecl SeekItem_Hook(void* item, CMTime time) {
    AppleSeeks();
    g_seekItem(item, time);
    Instance().Interrupt("moved in the song");
}

Generic_t g_seekItemTolerance = nullptr;
void* __cdecl SeekItemTolerance_Hook(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h) {
    AppleSeeks();
    void* result = g_seekItemTolerance(a, b, c, d, e, f, g, h);
    Instance().Interrupt("moved in the song");
    return result;
}

Generic_t g_advance = nullptr;
void* __cdecl Advance_Hook(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h) {
    AppleSeeks();
    void* result = g_advance(a, b, c, d, e, f, g, h);
    Instance().Interrupt("skipped");
    return result;
}

Generic_t g_remove = nullptr;
void* __cdecl Remove_Hook(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h) {
    void* result = g_remove(a, b, c, d, e, f, g, h);
    Instance().Interrupt("the queue changed");
    return result;
}

Generic_t g_removeAll = nullptr;
void* __cdecl RemoveAll_Hook(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h) {
    void* result = g_removeAll(a, b, c, d, e, f, g, h);
    Instance().Interrupt("the queue changed");
    return result;
}

template <typename Fn>
size_t Hook(const char* function, Fn replacement, Fn& original) {
    return HookImportFromNowOn("AVFoundationCF.dll", function, AddressOf(replacement),
                               reinterpret_cast<void**>(&original));
}

}  // namespace

AutoMixHost& AutoMix() { return Instance(); }

namespace detail {

void StartAutoMixWatch() {
    if (!A().ready) {
        Note("AutoMix: a function of AVFoundationCF is missing, AutoMix stays off");
        return;
    }
    size_t queue = Hook("AVCFQueuePlayerCreateWithPlayerItemsAndOptions", &CreateQueue_Hook, g_createQueue);
    size_t watched = Hook("AVCFPlayerSetRate", &SetRate_Hook, g_setRate) +
                     Hook("AVCFPlayerSeekToTime", &SeekPlayer_Hook, g_seekPlayer) +
                     Hook("AVCFPlayerItemSeekToTime", &SeekItem_Hook, g_seekItem) +
                     Hook("AVCFPlayerItemSeekToTimeWithToleranceAndCompletionCallback", &SeekItemTolerance_Hook,
                          g_seekItemTolerance) +
                     Hook("AVCFQueuePlayerAdvanceToNextItem", &Advance_Hook, g_advance) +
                     Hook("AVCFQueuePlayerRemoveItem", &Remove_Hook, g_remove) +
                     Hook("AVCFQueuePlayerRemoveAllItems", &RemoveAll_Hook, g_removeAll);
    Note("AutoMix: queue player followed (" + std::to_string(queue) + " import), " + std::to_string(watched) +
         " import(s) of Apple's controls watched");
}

bool AutoMixOpen() { return g_deckOpen.load(std::memory_order_relaxed); }

void AutoMixMain(float* samples, unsigned frames, int rate, std::uint64_t stream, unsigned queued) {
    g_deck.ProcessMain(samples, frames, rate, {stream, queued, SteadySeconds()});
}

bool AutoMixTail(float* samples, unsigned frames, int rate, bool silent, std::uint64_t stream, unsigned queued) {
    const double now = SteadySeconds();
    const bool heard = g_deck.ProcessTail(samples, frames, rate, silent, {stream, queued, now});
    if (!heard) {
        g_feed.live = false;
        return false;
    }
    if (!g_feed.live.exchange(true, std::memory_order_relaxed)) {
        g_feed.start = now;
        g_feed.frames = 0;
        g_feed.earliest = g_feed.latest = 0;
    }
    const double early = now - g_feed.start.load(std::memory_order_relaxed) -
                         static_cast<double>(g_feed.frames.load(std::memory_order_relaxed)) / rate;
    if (early < g_feed.earliest.load(std::memory_order_relaxed)) g_feed.earliest = early;
    if (early > g_feed.latest.load(std::memory_order_relaxed)) g_feed.latest = early;
    g_feed.frames += frames;
    return true;
}

}  // namespace detail

}  // namespace ampify::win::audio_hooks
