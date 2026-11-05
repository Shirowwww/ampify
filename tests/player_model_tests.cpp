// The portable half of the Windows player: the slots of Apple's player checked against the winmd list, the mapping of
// raw readings to PlayerSnapshot, the command routes, the queue model, and Ampify.player.queue / toggle with a fake host.

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../src/core/amp_abi.hpp"
#include "../src/core/manifest.hpp"
#include "../src/core/player_model.hpp"
#include "../src/core/player_queue.hpp"
#include "../src/core/player_watch.hpp"
#include "../src/core/script_extension.hpp"
#include "fake_host.hpp"
#include "test.hpp"

namespace {

namespace fs = std::filesystem;

void ReadWinmdList(std::istream& file, std::map<std::string, std::vector<std::string>>& types) {
    std::string line, current;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("  methods: ", 0) == 0) {
            std::istringstream list(line.substr(11));
            for (std::string name; std::getline(list, name, ',');) {
                size_t start = name.find_first_not_of(' ');
                types[current].push_back(start == std::string::npos ? "" : name.substr(start));
            }
        } else if (!line.empty() && line[0] != ' ') {
            current = line;
        }
    }
}

// "AMP.Services.IPlayer" -> its methods, in order, from knowledge/winmd/AMP.Services.txt and AMP.UI.Views.txt.
std::map<std::string, std::vector<std::string>> WinmdMethods() {
    std::map<std::string, std::vector<std::string>> types;
    for (const char* name : {"AMP.Services.txt", "AMP.UI.Views.txt"}) {
        std::ifstream file(fs::path(AMPIFY_SOURCE_DIR) / "knowledge" / "winmd" / name);
        ReadWinmdList(file, types);
    }
    return types;
}

int SlotIn(const std::vector<std::string>& methods, const std::string& method) {
    for (size_t i = 0; i < methods.size(); ++i) {
        if (methods[i] == method) return ampify::amp::kInspectableSlots + static_cast<int>(i);
    }
    return -1;
}

struct Fixture {
    ampify::test::FakeHost host;
    std::ostringstream out;
    ampify::Journal journal{out, [] { return std::string("t"); }};

    std::unique_ptr<ampify::ScriptExtension> Make(const std::string& id, const std::string& permissions,
                                               const std::string& main) {
        fs::path dir = fs::temp_directory_path() / "ampify-tests" / ("player-model-" + id);
        fs::remove_all(dir);
        fs::create_directories(dir);
        const std::string manifest =
            R"({"id":")" + id + R"(","name":"Demo","version":"1.0.0","kind":"extension","author":"Shirowwww",)" +
            R"("description":"Test.","kitApi":"1.0","main":"main.js","permissions":[)" + permissions + "]}";
        std::ofstream(dir / "manifest.json") << manifest;
        std::ofstream(dir / "main.js") << main;
        ampify::ManifestResult result = ampify::ParseManifest(manifest, id, 1, [](const std::string&) { return true; });
        return std::make_unique<ampify::ScriptExtension>(*result.manifest, dir, host, journal);
    }

    bool Logged(const std::string& text) const { return out.str().find(text) != std::string::npos; }
};

// A host with a queue, as the Windows host offers it.
struct QueueFakeHost : ampify::test::FakeHost, ampify::QueueHost {
    ampify::QueueSnapshot queue;
    std::vector<std::string> added;    // "id|next" or "id|last"
    std::vector<std::string> removed;
    bool accept = true;

    ampify::QueueSnapshot Queue() override { return queue; }
    ampify::QueueResult AddToQueue(const std::string& trackId, ampify::QueuePosition position) override {
        added.push_back(trackId + (position == ampify::QueuePosition::Next ? "|next" : "|last"));
        return {accept, accept ? "" : "not now"};
    }
    ampify::QueueResult RemoveFromQueue(const std::string& itemId) override {
        removed.push_back(itemId);
        return {accept, accept ? "" : "not now"};
    }
    void* QueryCapability(std::string_view name) override {
        if (void* found = Offer<ampify::QueueHost>(name, this)) return found;
        return FakeHost::QueryCapability(name);
    }
};

struct QueueFixture : Fixture {
    QueueFakeHost queueHost;
    std::unique_ptr<ampify::ScriptExtension> MakeWithQueue(const std::string& id, const std::string& permissions,
                                                        const std::string& main) {
        fs::path dir = fs::temp_directory_path() / "ampify-tests" / ("player-queue-" + id);
        fs::remove_all(dir);
        fs::create_directories(dir);
        const std::string manifest =
            R"({"id":")" + id + R"(","name":"Demo","version":"1.0.0","kind":"extension","author":"Shirowwww",)" +
            R"("description":"Test.","kitApi":"1.0","main":"main.js","permissions":[)" + permissions + "]}";
        std::ofstream(dir / "main.js") << main;
        ampify::ManifestResult result = ampify::ParseManifest(manifest, id, 1, [](const std::string&) { return true; });
        return std::make_unique<ampify::ScriptExtension>(*result.manifest, dir, queueHost, journal);
    }
};

ampify::AmpReading Reading() {
    ampify::AmpReading reading;
    reading.playing = true;
    reading.hasItem = true;
    reading.title = "Title";
    reading.artist = "Artist";
    reading.album = "Album";
    reading.positionRaw = 600'000'000;   // 60 s in 100 ns
    reading.durationRaw = 1'800'000'000; // 180 s
    reading.audioFormat = static_cast<int32_t>(ampify::amp::AudioFormat::DolbyAtmos);
    reading.queueItemId = 42;
    return reading;
}

}  // namespace

// ---- Slots against the winmd list ---------------------------------------------------------------------------------

TEST_CASE(AmpAbi_EverySlotMatchesTheMethodOrderOfTheWinmd) {
    auto types = WinmdMethods();
    CHECK(!types.empty());
    for (const ampify::amp::SlotEvidence& evidence : ampify::amp::kSlotEvidence) {
        auto found = types.find(evidence.interfaceName);
        if (found == types.end()) {
            ampify::test::ReportFailure(__FILE__, __LINE__,
                                     std::string("type missing from the winmd: ") + evidence.interfaceName);
            continue;
        }
        int slot = SlotIn(found->second, evidence.method);
        if (slot != evidence.slot) {
            ampify::test::ReportFailure(__FILE__, __LINE__, std::string(evidence.interfaceName) + "." + evidence.method +
                                                             " : slot " + std::to_string(evidence.slot) + ", winmd " +
                                                             std::to_string(slot));
        }
    }
}

TEST_CASE(PlayerModel_SplitsArtistAndAlbumOnTheEmDash) {
    auto [artist, album] = ampify::SplitArtistAlbum("Billie Eilish \xE2\x80\x94 WHEN WE ALL FALL ASLEEP");
    CHECK(artist == "Billie Eilish");
    CHECK(album == "WHEN WE ALL FALL ASLEEP");
    auto [alone, none] = ampify::SplitArtistAlbum("Daft Punk");
    CHECK(alone == "Daft Punk" && none.empty());

    ampify::AmpReading reading;
    reading.hasItem = true;
    reading.title = "bad guy";
    reading.subtitle = "Billie Eilish \xE2\x80\x94 WHEN WE ALL FALL ASLEEP";
    ampify::PlayerSnapshot snapshot = ampify::SnapshotFromAmp(reading, ampify::AmpTimeUnit::Unknown);
    CHECK(snapshot.track && snapshot.track->artist == "Billie Eilish");
    CHECK(snapshot.track && snapshot.track->album == "WHEN WE ALL FALL ASLEEP");
}

TEST_CASE(AmpAbi_TheVolumeMuteAndEqMethodsAreNeverInTheTable) {
    auto types = WinmdMethods();
    const auto& player = types["AMP.Services.IPlayer"];
    const int volume = SlotIn(player, "SetVolumeAsync");
    const int mute = SlotIn(player, "SetMuteAsync");
    CHECK(volume > 0 && mute > 0);
    for (const ampify::amp::SlotEvidence& evidence : ampify::amp::kSlotEvidence) {
        const std::string method = evidence.method;
        CHECK(method.find("Volume") == std::string::npos);
        CHECK(method.find("Mute") == std::string::npos);
        CHECK(method.find("EQ") == std::string::npos);
        if (std::string(evidence.interfaceName) == "AMP.Services.IPlayer") {
            CHECK(evidence.slot != volume && evidence.slot != mute);
        }
    }
}

TEST_CASE(AmpAbi_TheAudioFormatEnumHasTheFieldsTheMappingAssumes) {
    std::ifstream file(fs::path(AMPIFY_SOURCE_DIR) / "knowledge" / "winmd" / "AMP.Services.txt");
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    CHECK(text.find("AMP.Services.CommonModels.AudioFormat\n  fields: Other, Lossless, HiResLossless, DolbyAtmos, "
                    "Spatial, Surround") != std::string::npos ||
          text.find("AMP.Services.CommonModels.AudioFormat\r\n  fields: Other, Lossless, HiResLossless, DolbyAtmos, "
                    "Spatial, Surround") != std::string::npos);
}

// ---- Readings to snapshots ----------------------------------------------------------------------------------------

TEST_CASE(PlayerModel_TheTimeUnitComesFromTheSmtcLengthWhenThereIsOne) {
    using ampify::AmpTimeUnit;
    CHECK(ampify::DetectAmpTimeUnit(1'800'000'000, 180'000) == AmpTimeUnit::Ticks100ns);
    CHECK(ampify::DetectAmpTimeUnit(180'000, 180'000) == AmpTimeUnit::Milliseconds);
    CHECK(ampify::DetectAmpTimeUnit(180, 180'000) == AmpTimeUnit::Seconds);
    // Without SMTC: a song lasts between 20 s and 10 h.
    CHECK(ampify::DetectAmpTimeUnit(1'800'000'000, std::nullopt) == AmpTimeUnit::Ticks100ns);
    CHECK(ampify::DetectAmpTimeUnit(180'000, std::nullopt) == AmpTimeUnit::Milliseconds);
    CHECK(ampify::DetectAmpTimeUnit(180, std::nullopt) == AmpTimeUnit::Seconds);
    CHECK(ampify::DetectAmpTimeUnit(0, 180'000) == AmpTimeUnit::Unknown);
    // SMTC describes another track (too far from every unit): the duration alone decides.
    CHECK(ampify::DetectAmpTimeUnit(180'000, 5) == AmpTimeUnit::Milliseconds);
    CHECK(ampify::AmpTimeToMs(1'800'000'000, AmpTimeUnit::Ticks100ns) == 180'000);
    CHECK(ampify::AmpTimeToMs(-5, AmpTimeUnit::Milliseconds) == 0);
}

TEST_CASE(PlayerModel_AnAppleReadingBecomesASnapshot) {
    ampify::PlayerSnapshot s = ampify::SnapshotFromAmp(Reading(), ampify::AmpTimeUnit::Ticks100ns);
    CHECK(s.source == "apple");
    CHECK(s.playing);
    CHECK(s.positionMs == 60'000);
    CHECK(s.durationMs == 180'000);
    CHECK(s.track.has_value());
    CHECK(s.track->title == "Title");
    CHECK(s.track->artist == "Artist");
    CHECK(s.track->album == "Album");
    CHECK(s.track->variantId == "dolby-atmos");
    CHECK(s.track->atmos);
    CHECK(!s.track->lossless);
    CHECK(s.track->queueItemId == "42");
    CHECK(s.track->id == ampify::TrackIdOf("Title", "Artist", "Album"));
    CHECK(s.track->id.size() == 18 && s.track->id.rfind("t-", 0) == 0);
}

TEST_CASE(PlayerModel_AppleFormatsMapToLosslessAndAtmos) {
    using ampify::amp::AudioFormat;
    CHECK(ampify::IsLosslessFormat(static_cast<int32_t>(AudioFormat::Lossless)));
    CHECK(ampify::IsLosslessFormat(static_cast<int32_t>(AudioFormat::HiResLossless)));
    CHECK(!ampify::IsLosslessFormat(static_cast<int32_t>(AudioFormat::DolbyAtmos)));
    CHECK(ampify::IsAtmosFormat(static_cast<int32_t>(AudioFormat::DolbyAtmos)));
    CHECK(!ampify::IsAtmosFormat(static_cast<int32_t>(AudioFormat::Spatial)));
    CHECK(ampify::VariantOfAudioFormat(2) == "hi-res-lossless");
    CHECK(ampify::VariantOfAudioFormat(0) == "other");
    CHECK(ampify::VariantOfAudioFormat(9) == "format-9");
}

TEST_CASE(PlayerModel_PausedOrPlaceholderAppleReadings) {
    ampify::AmpReading reading = Reading();
    reading.paused = true;  // Apple can say playing and paused while it buffers: paused wins
    CHECK(!ampify::SnapshotFromAmp(reading, ampify::AmpTimeUnit::Ticks100ns).playing);

    reading = Reading();
    reading.placeholder = true;
    ampify::PlayerSnapshot s = ampify::SnapshotFromAmp(reading, ampify::AmpTimeUnit::Ticks100ns);
    CHECK(!s.track.has_value());
    CHECK(s.source == "apple");

    reading = Reading();
    reading.hasItem = false;
    CHECK(!ampify::SnapshotFromAmp(reading, ampify::AmpTimeUnit::Ticks100ns).track.has_value());

    reading = Reading();
    reading.positionRaw = reading.durationRaw + 50'000'000;  // a position past the end is kept at the end
    CHECK(ampify::SnapshotFromAmp(reading, ampify::AmpTimeUnit::Ticks100ns).positionMs == 180'000);
}

TEST_CASE(PlayerModel_AppleMusicsSmtcArtistHoldsTheAlbumToo) {
    // Apple Music writes "Artist — Album" in the session's artist and leaves its album empty.
    ampify::SmtcReading reading;
    reading.status = ampify::kSmtcPlaying;
    reading.title = "Rollout (feat. Jeune Morty)";
    reading.artist = "ELIESG \xE2\x80\x94 <333*";
    ampify::PlayerSnapshot s = ampify::SnapshotFromSmtc(reading);
    CHECK(s.track && s.track->artist == "ELIESG" && s.track->album == "<333*");
    CHECK(s.track->id == ampify::TrackIdOf("Rollout (feat. Jeune Morty)", "ELIESG", "<333*"));

    reading.album = "Real album";  // a player that fills the album keeps its artist as it is
    s = ampify::SnapshotFromSmtc(reading);
    CHECK(s.track->artist == "ELIESG \xE2\x80\x94 <333*" && s.track->album == "Real album");
}

TEST_CASE(PlayerModel_AnSmtcReadingMovesOnWhilePlaying) {
    ampify::SmtcReading reading;
    reading.status = ampify::kSmtcPlaying;
    reading.title = "Title";
    reading.artist = "Artist";
    reading.album = "Album";
    reading.endTicks = 1'800'000'000;
    reading.positionTicks = 600'000'000;
    reading.lastUpdated = 1'000'000'000;
    reading.now = reading.lastUpdated + 20'000'000;  // 2 s later
    ampify::PlayerSnapshot s = ampify::SnapshotFromSmtc(reading);
    CHECK(s.source == "smtc");
    CHECK(s.playing);
    CHECK(s.positionMs == 62'000);
    CHECK(s.durationMs == 180'000);
    CHECK(s.track->id == ampify::TrackIdOf("Title", "Artist", "Album"));  // the same id as from Apple's player
    CHECK(s.track->queueItemId.empty());
    CHECK(s.track->variantId.empty());

    reading.status = ampify::kSmtcPaused;  // paused: no extrapolation
    CHECK(ampify::SnapshotFromSmtc(reading).positionMs == 60'000);
    CHECK(!ampify::SnapshotFromSmtc(reading).playing);

    reading.status = ampify::kSmtcPlaying;
    reading.now = reading.lastUpdated + 10'000'000'000;  // far past the end
    CHECK(ampify::SnapshotFromSmtc(reading).positionMs == 180'000);

    // A song that never started (loading, or a stream that never comes) stays at 0: Apple renews the timeline about
    // once a second once a song is heard.
    reading.positionTicks = 0;
    reading.now = reading.lastUpdated + 100'000'000;  // 10 s at 0
    CHECK(ampify::SnapshotFromSmtc(reading).positionMs == 0);
    reading.now = reading.lastUpdated + 5'000'000;  // half a second after it was set at 0: counted on
    CHECK(ampify::SnapshotFromSmtc(reading).positionMs == 500);

    reading.title.clear();
    reading.artist.clear();
    CHECK(!ampify::SnapshotFromSmtc(reading).track.has_value());

    // Nothing playing: Apple Music 1.7 publishes a stopped entry, "Not Playing" in its own language, with no artist and
    // no length. It is no song.
    ampify::SmtcReading idle;
    idle.status = ampify::kSmtcPaused;
    idle.title = "\xC3\x80 l'arr\xC3\xAAt";  // "À l'arrêt", the French one
    CHECK(!ampify::SnapshotFromSmtc(idle).track.has_value());
    idle.artist = "Artist";  // a real entry with an artist stays one, even without a length
    CHECK(ampify::SnapshotFromSmtc(idle).track.has_value());
}

TEST_CASE(PlayerModel_ANewQueueEntryOfTheSameSongIsATrackChange) {
    ampify::PlayerWatcher watcher;
    ampify::AmpReading reading = Reading();
    watcher.Poll(ampify::SnapshotFromAmp(reading, ampify::AmpTimeUnit::Ticks100ns));
    CHECK(watcher.Poll(ampify::SnapshotFromAmp(reading, ampify::AmpTimeUnit::Ticks100ns)).empty());
    reading.queueItemId = 43;
    auto events = watcher.Poll(ampify::SnapshotFromAmp(reading, ampify::AmpTimeUnit::Ticks100ns));
    CHECK(events.size() == 1 && events[0] == "trackchange");
    reading.paused = true;
    events = watcher.Poll(ampify::SnapshotFromAmp(reading, ampify::AmpTimeUnit::Ticks100ns));
    CHECK(events.size() == 1 && events[0] == "statechange");
}

// ---- Command routes -----------------------------------------------------------------------------------------------

TEST_CASE(PlayerModel_PlayWhilePlayingIsAlreadyDone) {
    ampify::PlayerSnapshot known;
    known.source = "smtc";
    known.playing = true;
    ampify::RouteAvailability all{true, true, true};
    CHECK(ampify::PlanCommand(ampify::PlayerCommand::Play, 0, known, all).alreadyDone);
    CHECK(!ampify::PlanCommand(ampify::PlayerCommand::Pause, 0, known, all).alreadyDone);
    known.playing = false;
    CHECK(ampify::PlanCommand(ampify::PlayerCommand::Pause, 0, known, all).alreadyDone);
}

TEST_CASE(PlayerModel_RoutesAreAppleThenSmtcThenTheMediaKey) {
    ampify::PlayerSnapshot known;
    known.source = "apple";
    known.playing = true;
    ampify::CommandPlan plan = ampify::PlanCommand(ampify::PlayerCommand::Pause, 0, known, {true, true, true});
    CHECK(plan.steps.size() == 3);
    CHECK(plan.steps[0].route == ampify::CommandRoute::Apple);
    CHECK(plan.steps[1].route == ampify::CommandRoute::Smtc);
    CHECK(plan.steps[2].route == ampify::CommandRoute::MediaKey);
    CHECK(plan.steps[2].key == ampify::CommandStep::Key::PlayPause);

    plan = ampify::PlanCommand(ampify::PlayerCommand::Next, 0, known, {false, false, true});
    CHECK(plan.steps.size() == 1 && plan.steps[0].key == ampify::CommandStep::Key::Next);
    plan = ampify::PlanCommand(ampify::PlayerCommand::Previous, 0, ampify::PlayerSnapshot{}, {false, true, true});
    CHECK(plan.steps.size() == 2 && plan.steps[1].key == ampify::CommandStep::Key::Previous);
}

TEST_CASE(PlayerModel_TheToggleKeyWaitsForAKnownState) {
    // Unknown state: the play/pause key could stop the music instead of starting it.
    ampify::CommandPlan plan = ampify::PlanCommand(ampify::PlayerCommand::Play, 0, ampify::PlayerSnapshot{}, {false, false, true});
    CHECK(!plan.alreadyDone);
    CHECK(plan.steps.empty());
    plan = ampify::PlanCommand(ampify::PlayerCommand::Play, 0, ampify::PlayerSnapshot{}, {false, true, true});
    CHECK(plan.steps.size() == 1 && plan.steps[0].route == ampify::CommandRoute::Smtc);
}

TEST_CASE(PlayerModel_SeekGoesThroughApplesPlayerThenSmtcWithinTheTrack) {
    ampify::PlayerSnapshot known;
    known.source = "apple";
    known.durationMs = 180'000;
    ampify::CommandPlan plan = ampify::PlanCommand(ampify::PlayerCommand::Seek, 999'999, known, {true, true, true});
    CHECK(plan.steps.size() == 2 && plan.steps[0].route == ampify::CommandRoute::Apple &&
          plan.steps[1].route == ampify::CommandRoute::Smtc);
    CHECK(plan.seekMs == 180'000);
    CHECK(ampify::PlanCommand(ampify::PlayerCommand::Seek, -5, known, {true, true, true}).seekMs == 0);
    plan = ampify::PlanCommand(ampify::PlayerCommand::Seek, 1000, known, {false, true, true});
    CHECK(plan.steps.size() == 1 && plan.steps[0].route == ampify::CommandRoute::Smtc);
    CHECK(ampify::PlanCommand(ampify::PlayerCommand::Seek, 1000, known, {false, false, true}).steps.empty());
}

// ---- Queue model --------------------------------------------------------------------------------------------------

TEST_CASE(PlayerQueue_SectionsIdsAndPositions) {
    CHECK(ampify::QueueSectionFromKind(2) == ampify::QueueSection::Current);
    CHECK(ampify::QueueSectionFromKind(5) == ampify::QueueSection::Autoplay);
    CHECK(ampify::QueueSectionFromKind(77) == ampify::QueueSection::Unknown);
    CHECK(std::string(ampify::QueueSectionName(ampify::QueueSection::NextAdded)) == "next-added");
    CHECK(ampify::IsValidQueueId("1440833098"));
    CHECK(ampify::IsValidQueueId("t-0123456789abcdef"));
    CHECK(!ampify::IsValidQueueId(""));
    CHECK(!ampify::IsValidQueueId("a b"));
    CHECK(!ampify::IsValidQueueId(std::string(129, 'a')));
    CHECK(!ampify::IsValidQueueId("é"));
    CHECK(ampify::IsCatalogId("1440833098"));
    CHECK(!ampify::IsCatalogId(""));
    CHECK(!ampify::IsCatalogId("14408a3098"));
    CHECK(!ampify::IsCatalogId("1\"}]"));
    CHECK(!ampify::IsCatalogId(std::string(20, '1')));
    ampify::QueuePosition position = ampify::QueuePosition::Next;
    CHECK(ampify::ParseQueuePosition("", position) && position == ampify::QueuePosition::Last);
    CHECK(ampify::ParseQueuePosition("next", position) && position == ampify::QueuePosition::Next);
    CHECK(!ampify::ParseQueuePosition("first", position));
}

TEST_CASE(PlayerQueue_JsonSaysWhatTheHostKnows) {
    ampify::QueueSnapshot snapshot;
    snapshot.source = "apple";
    snapshot.reason = "partielle";
    snapshot.items.push_back(
        {"42", "Title", "Artist", 180'000, ampify::QueueSection::Current, "1440833098", "Album"});
    CHECK(ampify::QueueJson(snapshot, true).Dump() ==
          R"({"available":true,"complete":false,"source":"apple","reason":"partielle","items":[{"id":"42",)"
          R"("title":"Title","artist":"Artist","album":"Album","durationMs":180000,"section":"current",)"
          R"("catalogId":"1440833098"}]})");
    std::string none = ampify::QueueJson(ampify::QueueSnapshot{}, false).Dump();
    CHECK(none.find(R"("available":false)") != std::string::npos);
    CHECK(none.find(R"("items":[])") != std::string::npos);
}

// ---- Ampify.player.queue and toggle ----------------------------------------------------------------------------------

TEST_CASE(PlayerQueue_ListNeedsPlayerReadAndWritesNeedPlayerControl) {
    QueueFixture f;
    f.queueHost.queue.complete = true;
    f.queueHost.queue.source = "apple";
    f.queueHost.queue.items.push_back({"7", "Next", "Artist", 1000, ampify::QueueSection::Next});

    auto denied = f.MakeWithQueue("qdenied", "", R"(
        try { Ampify.player.queue.list(); } catch (e) { Ampify.log("list " + e.name); }
        try { Ampify.player.queue.add("1"); } catch (e) { Ampify.log("add " + e.name); }
    )");
    CHECK(denied->Start());
    CHECK(f.Logged("[qdenied] list PermissionDenied"));
    CHECK(f.Logged("[qdenied] add PermissionDenied"));
    CHECK(f.queueHost.added.empty());

    auto reader = f.MakeWithQueue("qread", R"("player.read")", R"(
        const q = Ampify.player.queue.list();
        Ampify.log("queue " + q.available + " " + q.complete + " " + q.items.length + " " + q.items[0].title + " " +
                q.items[0].section);
    )");
    CHECK(reader->Start());
    CHECK(f.Logged("[qread] queue true true 1 Next next"));

    auto writer = f.MakeWithQueue("qwrite", R"("player.control")", R"(
        Ampify.log("add " + Ampify.player.queue.add("1440833098", { position: "next" }));
        Ampify.log("add2 " + Ampify.player.queue.add(12));
        Ampify.log("remove " + Ampify.player.queue.remove("7"));
        try { Ampify.player.queue.add("x", { position: "first" }); } catch (e) { Ampify.log("pos " + e.name); }
        try { Ampify.player.queue.add(); } catch (e) { Ampify.log("noid " + e.name); }
        try { Ampify.player.queue.remove("a b"); } catch (e) { Ampify.log("badid " + e.name); }
    )");
    CHECK(writer->Start());
    CHECK(f.Logged("[qwrite] add true"));
    CHECK(f.Logged("[qwrite] add2 true"));
    CHECK(f.Logged("[qwrite] remove true"));
    CHECK(f.Logged("[qwrite] pos RangeError"));
    CHECK(f.Logged("[qwrite] noid TypeError"));
    CHECK(f.Logged("[qwrite] badid TypeError"));
    CHECK(f.queueHost.added.size() == 2);
    CHECK(f.queueHost.added[0] == "1440833098|next");
    CHECK(f.queueHost.added[1] == "12|last");
    CHECK(f.queueHost.removed.size() == 1 && f.queueHost.removed[0] == "7");
}

TEST_CASE(PlayerQueue_ARefusalIsFalseWithItsReasonInTheJournal) {
    QueueFixture f;
    f.queueHost.accept = false;
    auto element = f.MakeWithQueue("qno", R"("player.control")", R"(Ampify.log("add " + Ampify.player.queue.add("1"));)");
    CHECK(element->Start());
    CHECK(f.Logged("[qno] add false"));
    CHECK(f.Logged("[qno] queue.add refused: not now"));
}

TEST_CASE(PlayerQueue_AHostWithoutAQueueAnswersNotAvailable) {
    Fixture f;
    auto element = f.Make("qnone", R"("player.read", "player.control")", R"(
        const q = Ampify.player.queue.list();
        Ampify.log("queue " + q.available + " " + q.items.length);
        Ampify.log("add " + Ampify.player.queue.add("1"));
    )");
    CHECK(element->Start());
    CHECK(f.Logged("[qnone] queue false 0"));
    CHECK(f.Logged("[qnone] add false"));
    CHECK(f.Logged("no queue in this process"));
}

TEST_CASE(PlayerModel_ToggleNeedsOnlyPlayerControlAndFollowsTheState) {
    Fixture f;
    f.host.player.playing = true;
    auto element = f.Make("toggle", R"("player.control")", R"(Ampify.player.toggle();)");
    CHECK(element->Start());
    CHECK(f.host.commands.size() == 1 && f.host.commands[0] == ampify::PlayerCommand::Pause);
    f.host.player.playing = false;
    auto again = f.Make("toggle2", R"("player.control")", R"(Ampify.player.toggle();)");
    CHECK(again->Start());
    CHECK(f.host.commands.size() == 2 && f.host.commands[1] == ampify::PlayerCommand::Play);
}

TEST_CASE(PlayerModel_StateCarriesTheSourceAndTheQueueEntry) {
    Fixture f;
    f.host.player = ampify::SnapshotFromAmp(Reading(), ampify::AmpTimeUnit::Ticks100ns);
    auto element = f.Make("source", R"("player.read")", R"(
        const s = Ampify.player.state();
        Ampify.log("state " + s.source + " " + s.track.queueItemId + " " + s.track.atmos + " " + s.positionMs);
    )");
    CHECK(element->Start());
    CHECK(f.Logged("[source] state apple 42 true 60000"));
}

// A song still loading, or a stream that never starts, is "playing" for Apple with its position at 0: the karaoke
// must not run ahead of a song nobody hears.
TEST_CASE(PositionClock_NeverInventsTimeWhileApplesPositionStays) {
    ampify::PositionClock clock;
    CHECK(clock.Next("a", true, 0, 200000, 1000) == 0);
    CHECK(clock.Next("a", true, 0, 200000, 9000) == 0);
    CHECK(!clock.Moving(9000));
}

// Apple moves its position about once a second: in between, the clock counts on from the last change.
TEST_CASE(PositionClock_MovesOnBetweenApplesUpdates) {
    ampify::PositionClock clock;
    CHECK(clock.Next("a", true, 30000, 200000, 1000) == 30000);
    CHECK(clock.Next("a", true, 30000, 200000, 1500) == 30500);
    CHECK(clock.Moving(1500));
    CHECK(clock.Next("a", true, 31000, 200000, 2030) == 31000);
    CHECK(clock.Next("b", true, 0, 200000, 3000) == 0);         // a new song starts at 0
    CHECK(clock.Next("b", true, 1990, 2000, 4000) == 1990);
    CHECK(clock.Next("b", true, 1990, 2000, 4800) == 2000);     // never past the song's end
}

TEST_CASE(PositionClock_StopsWhenApplesPositionStallsOrPauses) {
    ampify::PositionClock clock;
    CHECK(clock.Next("a", true, 30000, 200000, 1000) == 30000);
    CHECK(clock.Next("a", true, 30000, 200000, 5000) == 30000);  // buffering: Apple's value no longer moves
    CHECK(!clock.Moving(5000));
    CHECK(clock.Next("a", true, 31000, 200000, 6000) == 31000);  // it moves again
    CHECK(clock.Next("a", false, 31000, 200000, 6500) == 31500); // paused: what was counted is kept
    CHECK(!clock.Moving(6500));
    CHECK(clock.Next("a", false, 31000, 200000, 9000) == 31500);
}

TEST_CASE(PositionClock_FollowsASeekAndApplesLyricsLine) {
    ampify::PositionClock clock;
    CHECK(clock.Next("a", true, 30000, 200000, 1000) == 30000);
    clock.Seeked(90000, 1200);
    CHECK(clock.Next("a", true, 30000, 200000, 2200) == 91000);  // Apple's value from before the seek is old news
    CHECK(clock.Next("a", true, 91500, 200000, 2500) == 91500);
    // Without Apple's position, the line its lyrics view lights up proves the song moves: counted on from there.
    ampify::PositionClock lines;
    CHECK(lines.Next("a", true, 0, 200000, 1000) == 0);
    lines.Seeked(12000, 2000);
    CHECK(lines.Next("a", true, 0, 200000, 6000) == 16000);
    CHECK(lines.Moving(6000));
}

// A pause is not time played: the clock starts again from where it stopped, however long the pause lasted.
TEST_CASE(PositionClock_ResumesWhereThePauseLeftIt) {
    ampify::PositionClock clock;
    CHECK(clock.Next("a", true, 0, 200000, 1000) == 0);
    clock.Seeked(12000, 2000);
    CHECK(clock.Next("a", true, 0, 200000, 3000) == 13000);
    CHECK(clock.Next("a", false, 0, 200000, 3500) == 13500);
    CHECK(clock.Next("a", true, 0, 200000, 33500) == 13500);
    CHECK(clock.Next("a", true, 0, 200000, 34000) == 14000);
}
