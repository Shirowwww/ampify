// The portable half of the Windows player: the slots of Apple's player checked against the winmd list, the mapping of
// raw readings to PlayerSnapshot, the command routes, the queue model, and AMK.player.queue / toggle with a fake host.

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../core/amp_abi.hpp"
#include "../core/manifest.hpp"
#include "../core/player_model.hpp"
#include "../core/player_queue.hpp"
#include "../core/player_watch.hpp"
#include "../core/script_extension.hpp"
#include "fake_host.hpp"
#include "test.hpp"

namespace {

namespace fs = std::filesystem;

// "AMP.Services.IPlayer" -> its methods, in order, from knowledge/winmd/AMP.Services.txt.
std::map<std::string, std::vector<std::string>> WinmdMethods() {
    std::map<std::string, std::vector<std::string>> types;
    std::ifstream file(fs::path(AMK_SOURCE_DIR) / "knowledge" / "winmd" / "AMP.Services.txt");
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
    return types;
}

int SlotIn(const std::vector<std::string>& methods, const std::string& method) {
    for (size_t i = 0; i < methods.size(); ++i) {
        if (methods[i] == method) return amk::amp::kInspectableSlots + static_cast<int>(i);
    }
    return -1;
}

struct Fixture {
    amk::test::FakeHost host;
    std::ostringstream out;
    amk::Journal journal{out, [] { return std::string("t"); }};

    std::unique_ptr<amk::ScriptExtension> Make(const std::string& id, const std::string& permissions,
                                               const std::string& main) {
        fs::path dir = fs::temp_directory_path() / "amk-tests" / ("player-model-" + id);
        fs::remove_all(dir);
        fs::create_directories(dir);
        const std::string manifest =
            R"({"id":")" + id + R"(","name":"Démo","version":"1.0.0","kind":"extension","author":"Shirowwww",)" +
            R"("description":"Test.","kitApi":"1.0","main":"main.js","permissions":[)" + permissions + "]}";
        std::ofstream(dir / "manifest.json") << manifest;
        std::ofstream(dir / "main.js") << main;
        amk::ManifestResult result = amk::ParseManifest(manifest, id, 1, [](const std::string&) { return true; });
        return std::make_unique<amk::ScriptExtension>(*result.manifest, dir, host, journal);
    }

    bool Logged(const std::string& text) const { return out.str().find(text) != std::string::npos; }
};

// A host with a queue, as the Windows host offers it.
struct QueueFakeHost : amk::test::FakeHost, amk::QueueHost {
    amk::QueueSnapshot queue;
    std::vector<std::string> added;    // "id|next" or "id|last"
    std::vector<std::string> removed;
    bool accept = true;

    amk::QueueSnapshot Queue() override { return queue; }
    amk::QueueResult AddToQueue(const std::string& trackId, amk::QueuePosition position) override {
        added.push_back(trackId + (position == amk::QueuePosition::Next ? "|next" : "|last"));
        return {accept, accept ? "" : "pas maintenant"};
    }
    amk::QueueResult RemoveFromQueue(const std::string& itemId) override {
        removed.push_back(itemId);
        return {accept, accept ? "" : "pas maintenant"};
    }
    void* QueryCapability(std::string_view name) override {
        if (void* found = Offer<amk::QueueHost>(name, this)) return found;
        return FakeHost::QueryCapability(name);
    }
};

struct QueueFixture : Fixture {
    QueueFakeHost queueHost;
    std::unique_ptr<amk::ScriptExtension> MakeWithQueue(const std::string& id, const std::string& permissions,
                                                        const std::string& main) {
        fs::path dir = fs::temp_directory_path() / "amk-tests" / ("player-queue-" + id);
        fs::remove_all(dir);
        fs::create_directories(dir);
        const std::string manifest =
            R"({"id":")" + id + R"(","name":"Démo","version":"1.0.0","kind":"extension","author":"Shirowwww",)" +
            R"("description":"Test.","kitApi":"1.0","main":"main.js","permissions":[)" + permissions + "]}";
        std::ofstream(dir / "main.js") << main;
        amk::ManifestResult result = amk::ParseManifest(manifest, id, 1, [](const std::string&) { return true; });
        return std::make_unique<amk::ScriptExtension>(*result.manifest, dir, queueHost, journal);
    }
};

amk::AmpReading Reading() {
    amk::AmpReading reading;
    reading.playing = true;
    reading.hasItem = true;
    reading.title = "Titre";
    reading.artist = "Artiste";
    reading.album = "Album";
    reading.positionRaw = 600'000'000;   // 60 s in 100 ns
    reading.durationRaw = 1'800'000'000; // 180 s
    reading.audioFormat = static_cast<int32_t>(amk::amp::AudioFormat::DolbyAtmos);
    reading.queueItemId = 42;
    return reading;
}

}  // namespace

// ---- Slots against the winmd list ---------------------------------------------------------------------------------

TEST_CASE(AmpAbi_EverySlotMatchesTheMethodOrderOfTheWinmd) {
    auto types = WinmdMethods();
    CHECK(!types.empty());
    for (const amk::amp::SlotEvidence& evidence : amk::amp::kSlotEvidence) {
        auto found = types.find(evidence.interfaceName);
        if (found == types.end()) {
            amk::test::ReportFailure(__FILE__, __LINE__,
                                     std::string("type absent du winmd : ") + evidence.interfaceName);
            continue;
        }
        int slot = SlotIn(found->second, evidence.method);
        if (slot != evidence.slot) {
            amk::test::ReportFailure(__FILE__, __LINE__, std::string(evidence.interfaceName) + "." + evidence.method +
                                                             " : slot " + std::to_string(evidence.slot) + ", winmd " +
                                                             std::to_string(slot));
        }
    }
}

TEST_CASE(AmpAbi_TheVolumeMuteAndEqMethodsAreNeverInTheTable) {
    auto types = WinmdMethods();
    const auto& player = types["AMP.Services.IPlayer"];
    const int volume = SlotIn(player, "SetVolumeAsync");
    const int mute = SlotIn(player, "SetMuteAsync");
    CHECK(volume > 0 && mute > 0);
    for (const amk::amp::SlotEvidence& evidence : amk::amp::kSlotEvidence) {
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
    std::ifstream file(fs::path(AMK_SOURCE_DIR) / "knowledge" / "winmd" / "AMP.Services.txt");
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    CHECK(text.find("AMP.Services.CommonModels.AudioFormat\n  fields: Other, Lossless, HiResLossless, DolbyAtmos, "
                    "Spatial, Surround") != std::string::npos ||
          text.find("AMP.Services.CommonModels.AudioFormat\r\n  fields: Other, Lossless, HiResLossless, DolbyAtmos, "
                    "Spatial, Surround") != std::string::npos);
}

// ---- Readings to snapshots ----------------------------------------------------------------------------------------

TEST_CASE(PlayerModel_TheTimeUnitComesFromTheSmtcLengthWhenThereIsOne) {
    using amk::AmpTimeUnit;
    CHECK(amk::DetectAmpTimeUnit(1'800'000'000, 180'000) == AmpTimeUnit::Ticks100ns);
    CHECK(amk::DetectAmpTimeUnit(180'000, 180'000) == AmpTimeUnit::Milliseconds);
    CHECK(amk::DetectAmpTimeUnit(180, 180'000) == AmpTimeUnit::Seconds);
    // Without SMTC: a song lasts between 20 s and 10 h.
    CHECK(amk::DetectAmpTimeUnit(1'800'000'000, std::nullopt) == AmpTimeUnit::Ticks100ns);
    CHECK(amk::DetectAmpTimeUnit(180'000, std::nullopt) == AmpTimeUnit::Milliseconds);
    CHECK(amk::DetectAmpTimeUnit(180, std::nullopt) == AmpTimeUnit::Seconds);
    CHECK(amk::DetectAmpTimeUnit(0, 180'000) == AmpTimeUnit::Unknown);
    // SMTC describes another track (too far from every unit): the duration alone decides.
    CHECK(amk::DetectAmpTimeUnit(180'000, 5) == AmpTimeUnit::Milliseconds);
    CHECK(amk::AmpTimeToMs(1'800'000'000, AmpTimeUnit::Ticks100ns) == 180'000);
    CHECK(amk::AmpTimeToMs(-5, AmpTimeUnit::Milliseconds) == 0);
}

TEST_CASE(PlayerModel_AnAppleReadingBecomesASnapshot) {
    amk::PlayerSnapshot s = amk::SnapshotFromAmp(Reading(), amk::AmpTimeUnit::Ticks100ns);
    CHECK(s.source == "apple");
    CHECK(s.playing);
    CHECK(s.positionMs == 60'000);
    CHECK(s.durationMs == 180'000);
    CHECK(s.track.has_value());
    CHECK(s.track->title == "Titre");
    CHECK(s.track->artist == "Artiste");
    CHECK(s.track->album == "Album");
    CHECK(s.track->variantId == "dolby-atmos");
    CHECK(s.track->atmos);
    CHECK(!s.track->lossless);
    CHECK(s.track->queueItemId == "42");
    CHECK(s.track->id == amk::TrackIdOf("Titre", "Artiste", "Album"));
    CHECK(s.track->id.size() == 18 && s.track->id.rfind("t-", 0) == 0);
}

TEST_CASE(PlayerModel_AppleFormatsMapToLosslessAndAtmos) {
    using amk::amp::AudioFormat;
    CHECK(amk::IsLosslessFormat(static_cast<int32_t>(AudioFormat::Lossless)));
    CHECK(amk::IsLosslessFormat(static_cast<int32_t>(AudioFormat::HiResLossless)));
    CHECK(!amk::IsLosslessFormat(static_cast<int32_t>(AudioFormat::DolbyAtmos)));
    CHECK(amk::IsAtmosFormat(static_cast<int32_t>(AudioFormat::DolbyAtmos)));
    CHECK(!amk::IsAtmosFormat(static_cast<int32_t>(AudioFormat::Spatial)));
    CHECK(amk::VariantOfAudioFormat(2) == "hi-res-lossless");
    CHECK(amk::VariantOfAudioFormat(0) == "other");
    CHECK(amk::VariantOfAudioFormat(9) == "format-9");
}

TEST_CASE(PlayerModel_PausedOrPlaceholderAppleReadings) {
    amk::AmpReading reading = Reading();
    reading.paused = true;  // Apple can say playing and paused while it buffers: paused wins
    CHECK(!amk::SnapshotFromAmp(reading, amk::AmpTimeUnit::Ticks100ns).playing);

    reading = Reading();
    reading.placeholder = true;
    amk::PlayerSnapshot s = amk::SnapshotFromAmp(reading, amk::AmpTimeUnit::Ticks100ns);
    CHECK(!s.track.has_value());
    CHECK(s.source == "apple");

    reading = Reading();
    reading.hasItem = false;
    CHECK(!amk::SnapshotFromAmp(reading, amk::AmpTimeUnit::Ticks100ns).track.has_value());

    reading = Reading();
    reading.positionRaw = reading.durationRaw + 50'000'000;  // a position past the end is kept at the end
    CHECK(amk::SnapshotFromAmp(reading, amk::AmpTimeUnit::Ticks100ns).positionMs == 180'000);
}

TEST_CASE(PlayerModel_AnSmtcReadingMovesOnWhilePlaying) {
    amk::SmtcReading reading;
    reading.status = amk::kSmtcPlaying;
    reading.title = "Titre";
    reading.artist = "Artiste";
    reading.album = "Album";
    reading.endTicks = 1'800'000'000;
    reading.positionTicks = 600'000'000;
    reading.lastUpdated = 1'000'000'000;
    reading.now = reading.lastUpdated + 20'000'000;  // 2 s later
    amk::PlayerSnapshot s = amk::SnapshotFromSmtc(reading);
    CHECK(s.source == "smtc");
    CHECK(s.playing);
    CHECK(s.positionMs == 62'000);
    CHECK(s.durationMs == 180'000);
    CHECK(s.track->id == amk::TrackIdOf("Titre", "Artiste", "Album"));  // the same id as from Apple's player
    CHECK(s.track->queueItemId.empty());
    CHECK(s.track->variantId.empty());

    reading.status = amk::kSmtcPaused;  // paused: no extrapolation
    CHECK(amk::SnapshotFromSmtc(reading).positionMs == 60'000);
    CHECK(!amk::SnapshotFromSmtc(reading).playing);

    reading.status = amk::kSmtcPlaying;
    reading.now = reading.lastUpdated + 10'000'000'000;  // far past the end
    CHECK(amk::SnapshotFromSmtc(reading).positionMs == 180'000);

    reading.title.clear();
    reading.artist.clear();
    CHECK(!amk::SnapshotFromSmtc(reading).track.has_value());
}

TEST_CASE(PlayerModel_ANewQueueEntryOfTheSameSongIsATrackChange) {
    amk::PlayerWatcher watcher;
    amk::AmpReading reading = Reading();
    watcher.Poll(amk::SnapshotFromAmp(reading, amk::AmpTimeUnit::Ticks100ns));
    CHECK(watcher.Poll(amk::SnapshotFromAmp(reading, amk::AmpTimeUnit::Ticks100ns)).empty());
    reading.queueItemId = 43;
    auto events = watcher.Poll(amk::SnapshotFromAmp(reading, amk::AmpTimeUnit::Ticks100ns));
    CHECK(events.size() == 1 && events[0] == "trackchange");
    reading.paused = true;
    events = watcher.Poll(amk::SnapshotFromAmp(reading, amk::AmpTimeUnit::Ticks100ns));
    CHECK(events.size() == 1 && events[0] == "statechange");
}

// ---- Command routes -----------------------------------------------------------------------------------------------

TEST_CASE(PlayerModel_PlayWhilePlayingIsAlreadyDone) {
    amk::PlayerSnapshot known;
    known.source = "smtc";
    known.playing = true;
    amk::RouteAvailability all{true, true, true};
    CHECK(amk::PlanCommand(amk::PlayerCommand::Play, 0, known, all).alreadyDone);
    CHECK(!amk::PlanCommand(amk::PlayerCommand::Pause, 0, known, all).alreadyDone);
    known.playing = false;
    CHECK(amk::PlanCommand(amk::PlayerCommand::Pause, 0, known, all).alreadyDone);
}

TEST_CASE(PlayerModel_RoutesAreAppleThenSmtcThenTheMediaKey) {
    amk::PlayerSnapshot known;
    known.source = "apple";
    known.playing = true;
    amk::CommandPlan plan = amk::PlanCommand(amk::PlayerCommand::Pause, 0, known, {true, true, true});
    CHECK(plan.steps.size() == 3);
    CHECK(plan.steps[0].route == amk::CommandRoute::Apple);
    CHECK(plan.steps[1].route == amk::CommandRoute::Smtc);
    CHECK(plan.steps[2].route == amk::CommandRoute::MediaKey);
    CHECK(plan.steps[2].key == amk::CommandStep::Key::PlayPause);

    plan = amk::PlanCommand(amk::PlayerCommand::Next, 0, known, {false, false, true});
    CHECK(plan.steps.size() == 1 && plan.steps[0].key == amk::CommandStep::Key::Next);
    plan = amk::PlanCommand(amk::PlayerCommand::Previous, 0, amk::PlayerSnapshot{}, {false, true, true});
    CHECK(plan.steps.size() == 2 && plan.steps[1].key == amk::CommandStep::Key::Previous);
}

TEST_CASE(PlayerModel_TheToggleKeyWaitsForAKnownState) {
    // Unknown state: the play/pause key could stop the music instead of starting it.
    amk::CommandPlan plan = amk::PlanCommand(amk::PlayerCommand::Play, 0, amk::PlayerSnapshot{}, {false, false, true});
    CHECK(!plan.alreadyDone);
    CHECK(plan.steps.empty());
    plan = amk::PlanCommand(amk::PlayerCommand::Play, 0, amk::PlayerSnapshot{}, {false, true, true});
    CHECK(plan.steps.size() == 1 && plan.steps[0].route == amk::CommandRoute::Smtc);
}

TEST_CASE(PlayerModel_SeekGoesThroughSmtcOnlyWithinTheTrack) {
    amk::PlayerSnapshot known;
    known.source = "apple";
    known.durationMs = 180'000;
    amk::CommandPlan plan = amk::PlanCommand(amk::PlayerCommand::Seek, 999'999, known, {true, true, true});
    CHECK(plan.steps.size() == 1 && plan.steps[0].route == amk::CommandRoute::Smtc);
    CHECK(plan.seekMs == 180'000);
    CHECK(amk::PlanCommand(amk::PlayerCommand::Seek, -5, known, {true, true, true}).seekMs == 0);
    CHECK(amk::PlanCommand(amk::PlayerCommand::Seek, 1000, known, {true, false, true}).steps.empty());
}

// ---- Queue model --------------------------------------------------------------------------------------------------

TEST_CASE(PlayerQueue_SectionsIdsAndPositions) {
    CHECK(amk::QueueSectionFromKind(2) == amk::QueueSection::Current);
    CHECK(amk::QueueSectionFromKind(5) == amk::QueueSection::Autoplay);
    CHECK(amk::QueueSectionFromKind(77) == amk::QueueSection::Unknown);
    CHECK(std::string(amk::QueueSectionName(amk::QueueSection::NextAdded)) == "next-added");
    CHECK(amk::IsValidQueueId("1440833098"));
    CHECK(amk::IsValidQueueId("t-0123456789abcdef"));
    CHECK(!amk::IsValidQueueId(""));
    CHECK(!amk::IsValidQueueId("a b"));
    CHECK(!amk::IsValidQueueId(std::string(129, 'a')));
    CHECK(!amk::IsValidQueueId("é"));
    amk::QueuePosition position = amk::QueuePosition::Next;
    CHECK(amk::ParseQueuePosition("", position) && position == amk::QueuePosition::Last);
    CHECK(amk::ParseQueuePosition("next", position) && position == amk::QueuePosition::Next);
    CHECK(!amk::ParseQueuePosition("first", position));
}

TEST_CASE(PlayerQueue_JsonSaysWhatTheHostKnows) {
    amk::QueueSnapshot snapshot;
    snapshot.source = "apple";
    snapshot.reason = "partielle";
    snapshot.items.push_back({"42", "Titre", "Artiste", 180'000, amk::QueueSection::Current});
    CHECK(amk::QueueJson(snapshot, true).Dump() ==
          R"({"available":true,"complete":false,"source":"apple","reason":"partielle","items":[{"id":"42",)"
          R"("title":"Titre","artist":"Artiste","durationMs":180000,"section":"current"}]})");
    std::string none = amk::QueueJson(amk::QueueSnapshot{}, false).Dump();
    CHECK(none.find(R"("available":false)") != std::string::npos);
    CHECK(none.find(R"("items":[])") != std::string::npos);
}

// ---- AMK.player.queue and toggle ----------------------------------------------------------------------------------

TEST_CASE(PlayerQueue_ListNeedsPlayerReadAndWritesNeedPlayerControl) {
    QueueFixture f;
    f.queueHost.queue.complete = true;
    f.queueHost.queue.source = "apple";
    f.queueHost.queue.items.push_back({"7", "Suivant", "Artiste", 1000, amk::QueueSection::Next});

    auto denied = f.MakeWithQueue("qdenied", "", R"(
        try { AMK.player.queue.list(); } catch (e) { AMK.log("list " + e.name); }
        try { AMK.player.queue.add("1"); } catch (e) { AMK.log("add " + e.name); }
    )");
    CHECK(denied->Start());
    CHECK(f.Logged("[qdenied] list PermissionDenied"));
    CHECK(f.Logged("[qdenied] add PermissionDenied"));
    CHECK(f.queueHost.added.empty());

    auto reader = f.MakeWithQueue("qread", R"("player.read")", R"(
        const q = AMK.player.queue.list();
        AMK.log("queue " + q.available + " " + q.complete + " " + q.items.length + " " + q.items[0].title + " " +
                q.items[0].section);
    )");
    CHECK(reader->Start());
    CHECK(f.Logged("[qread] queue true true 1 Suivant next"));

    auto writer = f.MakeWithQueue("qwrite", R"("player.control")", R"(
        AMK.log("add " + AMK.player.queue.add("1440833098", { position: "next" }));
        AMK.log("add2 " + AMK.player.queue.add(12));
        AMK.log("remove " + AMK.player.queue.remove("7"));
        try { AMK.player.queue.add("x", { position: "first" }); } catch (e) { AMK.log("pos " + e.name); }
        try { AMK.player.queue.add(); } catch (e) { AMK.log("noid " + e.name); }
        try { AMK.player.queue.remove("a b"); } catch (e) { AMK.log("badid " + e.name); }
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
    auto element = f.MakeWithQueue("qno", R"("player.control")", R"(AMK.log("add " + AMK.player.queue.add("1"));)");
    CHECK(element->Start());
    CHECK(f.Logged("[qno] add false"));
    CHECK(f.Logged("[qno] queue.add refusé : pas maintenant"));
}

TEST_CASE(PlayerQueue_AHostWithoutAQueueAnswersNotAvailable) {
    Fixture f;
    auto element = f.Make("qnone", R"("player.read", "player.control")", R"(
        const q = AMK.player.queue.list();
        AMK.log("queue " + q.available + " " + q.items.length);
        AMK.log("add " + AMK.player.queue.add("1"));
    )");
    CHECK(element->Start());
    CHECK(f.Logged("[qnone] queue false 0"));
    CHECK(f.Logged("[qnone] add false"));
    CHECK(f.Logged("pas de file d'attente dans ce processus"));
}

TEST_CASE(PlayerModel_ToggleNeedsOnlyPlayerControlAndFollowsTheState) {
    Fixture f;
    f.host.player.playing = true;
    auto element = f.Make("toggle", R"("player.control")", R"(AMK.player.toggle();)");
    CHECK(element->Start());
    CHECK(f.host.commands.size() == 1 && f.host.commands[0] == amk::PlayerCommand::Pause);
    f.host.player.playing = false;
    auto again = f.Make("toggle2", R"("player.control")", R"(AMK.player.toggle();)");
    CHECK(again->Start());
    CHECK(f.host.commands.size() == 2 && f.host.commands[1] == amk::PlayerCommand::Play);
}

TEST_CASE(PlayerModel_StateCarriesTheSourceAndTheQueueEntry) {
    Fixture f;
    f.host.player = amk::SnapshotFromAmp(Reading(), amk::AmpTimeUnit::Ticks100ns);
    auto element = f.Make("source", R"("player.read")", R"(
        const s = AMK.player.state();
        AMK.log("state " + s.source + " " + s.track.queueItemId + " " + s.track.atmos + " " + s.positionMs);
    )");
    CHECK(element->Start());
    CHECK(f.Logged("[source] state apple 42 true 60000"));
}
