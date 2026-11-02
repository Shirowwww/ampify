// Apple's lyrics and cover: the TTML scanner, the lyrics JSON, the cover address, the "lyricschange" event and
// Ampify.player.lyrics() with a fake host.

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include "../src/core/lyrics_host.hpp"
#include "../src/core/manifest.hpp"
#include "../src/core/player_model.hpp"
#include "../src/core/player_watch.hpp"
#include "../src/core/script_extension.hpp"
#include "fake_host.hpp"
#include "test.hpp"

namespace {

namespace fs = std::filesystem;

struct LyricsFakeHost : ampify::test::FakeHost, ampify::LyricsHost {
    std::optional<ampify::RawLyrics> raw;
    std::optional<ampify::RawLyrics> Lyrics() override { return raw; }
    void* QueryCapability(std::string_view name) override {
        if (void* found = Offer<ampify::LyricsHost>(name, this)) return found;
        return FakeHost::QueryCapability(name);
    }
};

std::unique_ptr<ampify::ScriptExtension> Make(ampify::Host& host, ampify::Journal& journal, const std::string& id,
                                           const std::string& permissions, const std::string& main) {
    fs::path dir = fs::temp_directory_path() / "ampify-tests" / ("lyrics-" + id);
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string manifest =
        R"({"id":")" + id + R"(","name":"Demo","version":"1.0.0","kind":"extension","author":"Shirowwww",)" +
        R"("description":"Test.","kitApi":"1.0","main":"main.js","permissions":[)" + permissions + "]}";
    std::ofstream(dir / "main.js") << main;
    ampify::ManifestResult result = ampify::ParseManifest(manifest, id, 1, [](const std::string&) { return true; });
    return std::make_unique<ampify::ScriptExtension>(*result.manifest, dir, host, journal);
}

const char kWordTtml[] =
    R"(<?xml version="1.0" encoding="UTF-8"?>
<tt xmlns="http://www.w3.org/ns/ttml" xmlns:itunes="http://music.apple.com/lyric-ttml-internal" xml:lang="en">
  <head><metadata><iTunesMetadata><translations><translation xml:lang="fr"><text for="L1">Bonjour</text></translation></translations></iTunesMetadata></metadata></head>
  <body dur="10.0"><div>
    <p begin="1.000" end="3.500" itunes:key="L1"><span begin="1.000" end="1.800">Hello</span> <span begin="1.800" end="3.500">wor&amp;ld</span></p>
    <p begin="0:04.0" end="00:00:06.250"><span begin="4.0">Second</span> <span begin="5.0" end="6.250">line</span></p>
  </div></body>
</tt>)";

}  // namespace

// ---- Times --------------------------------------------------------------------------------------------------------

TEST_CASE(Lyrics_ParsesTheTimeFormsOfTtml) {
    CHECK(ampify::ParseTtmlTime("12.345") == 12345);
    CHECK(ampify::ParseTtmlTime("0.5") == 500);
    CHECK(ampify::ParseTtmlTime("7") == 7000);
    CHECK(ampify::ParseTtmlTime("1:02.5") == 62500);
    CHECK(ampify::ParseTtmlTime("00:01:02.500") == 62500);
    CHECK(ampify::ParseTtmlTime("1:00:00") == 3'600'000);
    CHECK(ampify::ParseTtmlTime("12.5s") == 12500);
    CHECK(ampify::ParseTtmlTime("1500ms") == 1500);
    CHECK(ampify::ParseTtmlTime(" 2.25 ") == 2250);
    CHECK(ampify::ParseTtmlTime("1.23456") == 1234);
}

TEST_CASE(Lyrics_RefusesMalformedTimes) {
    CHECK(!ampify::ParseTtmlTime(""));
    CHECK(!ampify::ParseTtmlTime("abc"));
    CHECK(!ampify::ParseTtmlTime("-1"));
    CHECK(!ampify::ParseTtmlTime("1:2:3:4"));
    CHECK(!ampify::ParseTtmlTime("1.5:30"));
    CHECK(!ampify::ParseTtmlTime("1:"));
    CHECK(!ampify::ParseTtmlTime("."));
    CHECK(!ampify::ParseTtmlTime("12345678901234"));
}

// ---- The scanner --------------------------------------------------------------------------------------------------

TEST_CASE(Lyrics_ReadsLinesAndWordsOfATimedDocument) {
    ampify::Lyrics lyrics = ampify::ParseTtml(kWordTtml);
    CHECK(lyrics.synced);
    CHECK(lyrics.language == "en");
    CHECK(lyrics.lines.size() == 2);
    const ampify::LyricLine& first = lyrics.lines[0];
    CHECK(first.startMs == 1000 && first.endMs == 3500);
    CHECK(first.text == "Hello wor&ld");  // the French translation in the head is not a line
    CHECK(first.words.size() == 2);
    CHECK(first.words[0].text == "Hello" && first.words[0].startMs == 1000 && first.words[0].endMs == 1800);
    CHECK(first.words[1].text == "wor&ld" && first.words[1].startMs == 1800 && first.words[1].endMs == 3500);
    const ampify::LyricLine& second = lyrics.lines[1];
    CHECK(second.startMs == 4000 && second.endMs == 6250);
    CHECK(second.words.size() == 2);
    CHECK(second.words[0].endMs == 5000);  // no end: the next word's start
    CHECK(lyrics.text == "Hello wor&ld\nSecond line");
}

TEST_CASE(Lyrics_ReadsLinesTimedAsAWholeWithoutWords) {
    ampify::Lyrics lyrics = ampify::ParseTtml(
        "<tt><body><div><p begin=\"1.0\" end=\"2.0\">One</p><p begin=\"2.0\">Two</p><p begin=\"5.5\" end=\"7\">Three</p>"
        "</div></body></tt>");
    CHECK(lyrics.synced && lyrics.lines.size() == 3);
    CHECK(lyrics.lines[0].words.empty());
    CHECK(lyrics.lines[1].startMs == 2000 && lyrics.lines[1].endMs == 5500);  // no end: the next line's start
    CHECK(lyrics.lines[2].endMs == 7000);
}

TEST_CASE(Lyrics_DecodesEntitiesAndBreaks) {
    ampify::Lyrics lyrics = ampify::ParseTtml(
        "<tt><body><p begin=\"1\" end=\"2\">a &lt;b&gt; &quot;c&quot; &apos;d&apos; it&#39;s &#233;&#x41; &unknown; <br/>next<br />last</p>"
        "</body></tt>");
    CHECK(lyrics.lines.size() == 1);
    CHECK(lyrics.lines[0].text == "a <b> \"c\" 'd' it's \xC3\xA9" "A &unknown;\nnext\nlast");
}

TEST_CASE(Lyrics_ToleratesBrokenMarkup) {
    // Unclosed tags, a stray '<', comments, CDATA, upper case names and an input that ends inside a tag.
    ampify::Lyrics lyrics = ampify::ParseTtml(
        "<TT><BODY><!-- note --><P BEGIN=\"1\" END=\"2\">1 < 2 <![CDATA[a & b]]><P begin=\"3\" end=\"4\">Open<span begin=\"3\">x"
        "</BODY></TT><p begin=\"9\" end=\"10\">Last<spa");
    CHECK(lyrics.lines.size() == 3);
    CHECK(lyrics.lines[0].text == "1 < 2 a & b");
    CHECK(lyrics.lines[1].text == "Openx");
    CHECK(lyrics.lines[2].text == "Last");
    CHECK(ampify::ParseTtml("").lines.empty());
    CHECK(ampify::ParseTtml("not xml at all").lines.empty());
}

TEST_CASE(Lyrics_LeavesTranslationsOutOfTheLine) {
    ampify::Lyrics lyrics = ampify::ParseTtml(
        "<tt><body><p begin=\"1\" end=\"2\"><span begin=\"1\" end=\"2\">Hola</span><span ttm:role=\"x-translation\">Hello</span></p>"
        "</body></tt>");
    CHECK(lyrics.lines.size() == 1 && lyrics.lines[0].text == "Hola");
}

TEST_CASE(Lyrics_AnOverlongDocumentIsRefused) {
    std::string big = "<tt><body><p begin=\"1\" end=\"2\">" + std::string(ampify::kMaxLyricsBytes, 'a') + "</p></body></tt>";
    CHECK(ampify::ParseTtml(big).lines.empty());
}

TEST_CASE(Lyrics_DeepNestingDoesNotBlowUp) {
    std::string deep;
    for (int i = 0; i < 5000; ++i) deep += "<div>";
    deep += "<p begin=\"1\" end=\"2\">deep</p>";
    CHECK(ampify::ParseTtml(deep).lines.size() == 1);
}

// ---- From the raw strings -----------------------------------------------------------------------------------------

TEST_CASE(Lyrics_StaticTextBecomesUntimedLines) {
    ampify::RawLyrics raw;
    raw.staticText = "\n\nFirst\r\nSecond\n\n\nThird\n\n";
    raw.language = "fr";
    std::optional<ampify::Lyrics> lyrics = ampify::LyricsFromRaw(raw);
    CHECK(lyrics.has_value() && !lyrics->synced);
    CHECK(lyrics->language == "fr");
    CHECK(lyrics->lines.size() == 4);  // First, Second, one blank separator, Third
    CHECK(lyrics->lines[0].text == "First" && lyrics->lines[1].text == "Second");
    CHECK(lyrics->lines[2].text.empty() && lyrics->lines[3].text == "Third");
    CHECK(lyrics->text == "First\nSecond\n\nThird");
}

TEST_CASE(Lyrics_TheTtmlWinsOverTheStaticTextAndTheLanguageComesFromApple) {
    ampify::RawLyrics raw;
    raw.ttml = kWordTtml;
    raw.staticText = "Something else";
    std::optional<ampify::Lyrics> lyrics = ampify::LyricsFromRaw(raw);
    CHECK(lyrics.has_value() && lyrics->synced && lyrics->language == "en");  // the xml:lang of the document
    raw.language = "en-GB";
    CHECK(ampify::LyricsFromRaw(raw)->language == "en-GB");
}

TEST_CASE(Lyrics_NothingUsableIsNull) {
    ampify::RawLyrics raw;
    CHECK(!ampify::LyricsFromRaw(raw).has_value());
    raw.ttml = "<tt><body></body></tt>";
    raw.staticText = " \n ";
    CHECK(!ampify::LyricsFromRaw(raw).has_value());
    CHECK(ampify::LyricsJson(std::nullopt).Dump() == "null");
}

TEST_CASE(Lyrics_TheJsonHasTheDocumentedShape) {
    ampify::RawLyrics raw;
    raw.ttml = "<tt><body><p begin=\"1.5\" end=\"2\"><span begin=\"1.5\" end=\"2\">Hi</span></p></body></tt>";
    raw.language = "en";
    std::string json = ampify::LyricsJson(ampify::LyricsFromRaw(raw)).Dump();
    CHECK(json == "{\"source\":\"apple\",\"synced\":true,\"language\":\"en\",\"lines\":[{\"start\":1500,\"end\":2000,"
                  "\"text\":\"Hi\",\"words\":[{\"start\":1500,\"end\":2000,\"text\":\"Hi\"}]}],\"text\":\"Hi\"}");

    ampify::RawLyrics plain;
    plain.staticText = "Only text";
    json = ampify::LyricsJson(ampify::LyricsFromRaw(plain)).Dump();
    CHECK(json.find("\"synced\":false") != std::string::npos);
    CHECK(json.find("\"start\"") == std::string::npos);
    CHECK(json.find("\"lines\":[{\"text\":\"Only text\",\"words\":[]}]") != std::string::npos);
}

// ---- The cover ----------------------------------------------------------------------------------------------------

TEST_CASE(Artwork_FillsTheTemplate) {
    ampify::ArtworkReading artwork;
    artwork.uri = "https://is1-ssl.mzstatic.com/image/thumb/Music/a/{w}x{h}{c}.{f}";
    CHECK(ampify::ArtworkAddress(artwork) == "https://is1-ssl.mzstatic.com/image/thumb/Music/a/1000x1000bb.jpg");
    artwork.cropCode = "cc";
    artwork.format = "png";
    CHECK(ampify::ArtworkAddress(artwork) == "https://is1-ssl.mzstatic.com/image/thumb/Music/a/1000x1000cc.png");
}

TEST_CASE(Artwork_KeepsTheAspectRatio) {
    ampify::ArtworkReading artwork;
    artwork.uri = "https://example.com/{w}x{h}.jpg";
    artwork.width = 2000;
    artwork.height = 1000;
    CHECK(ampify::ArtworkAddress(artwork) == "https://example.com/1000x500.jpg");
    artwork.width = 500;
    artwork.height = 1000;
    CHECK(ampify::ArtworkAddress(artwork) == "https://example.com/500x1000.jpg");
}

TEST_CASE(Artwork_AcceptsEscapedBracesAndAnAddressWithoutPlaceholders) {
    ampify::ArtworkReading artwork;
    artwork.uri = "https://example.com/%7Bw%7Dx%7bh%7d%7Bc%7D.%7Bf%7D";
    CHECK(ampify::ArtworkAddress(artwork) == "https://example.com/1000x1000bb.jpg");
    artwork.uri = "https://example.com/cover.jpg";
    CHECK(ampify::ArtworkAddress(artwork) == "https://example.com/cover.jpg");
}

TEST_CASE(Artwork_OnlyPassesOnSafeHttpsAddresses) {
    ampify::ArtworkReading artwork;
    artwork.uri = "http://example.com/a.jpg";
    CHECK(ampify::ArtworkAddress(artwork).empty());
    artwork.uri = "file:///C:/secret.jpg";
    CHECK(ampify::ArtworkAddress(artwork).empty());
    artwork.uri = "javascript:alert(1)";
    CHECK(ampify::ArtworkAddress(artwork).empty());
    artwork.uri = "";
    CHECK(ampify::ArtworkAddress(artwork).empty());
    artwork.uri = "https://example.com/a b.jpg";
    CHECK(ampify::ArtworkAddress(artwork).empty());
    artwork.uri = "HTTPS://example.com/a.jpg";
    CHECK(!ampify::ArtworkAddress(artwork).empty());
    // A crop code or a format that is not plain letters never reaches the address.
    artwork.uri = "https://example.com/{c}.{f}";
    artwork.cropCode = "../x";
    artwork.format = "jpg?evil";
    CHECK(ampify::ArtworkAddress(artwork) == "https://example.com/bb.jpg");
}

TEST_CASE(Artwork_TheColorIsRrggbb) {
    ampify::ArtworkReading artwork;
    CHECK(ampify::ArtworkColorHex(artwork).empty());
    artwork.hasColor = true;
    CHECK(ampify::ArtworkColorHex(artwork).empty());  // transparent black: Apple gave none
    artwork.alpha = 255;
    artwork.red = 0x12;
    artwork.green = 0xAB;
    artwork.blue = 0x05;
    CHECK(ampify::ArtworkColorHex(artwork) == "#12AB05");
    artwork.alpha = 0;
    CHECK(ampify::ArtworkColorHex(artwork) == "#12AB05");
}

TEST_CASE(Artwork_ReachesTheTrackOfTheSnapshotAndItsJson) {
    ampify::AmpReading reading;
    reading.hasItem = true;
    reading.title = "T";
    reading.artwork.uri = "https://example.com/{w}x{h}.jpg";
    reading.artwork.hasColor = true;
    reading.artwork.alpha = 255;
    reading.artwork.red = 255;
    ampify::PlayerSnapshot snapshot = ampify::SnapshotFromAmp(reading, ampify::AmpTimeUnit::Milliseconds);
    CHECK(snapshot.track->artwork == "https://example.com/1000x1000.jpg");
    CHECK(snapshot.track->artworkColor == "#FF0000");

    ampify::test::FakeHost host;
    host.player = snapshot;
    std::ostringstream out;
    ampify::Journal journal(out, [] { return std::string("t"); });
    auto element = Make(host, journal, "cover", R"("player.read")",
                        R"(const t = Ampify.player.state().track; Ampify.log(t.artwork + " " + t.artworkColor);)");
    CHECK(element->Start());
    CHECK(out.str().find("[cover] https://example.com/1000x1000.jpg #FF0000") != std::string::npos);
}

// ---- The event ----------------------------------------------------------------------------------------------------

namespace {

ampify::PlayerSnapshot Snap(const std::string& id, bool lyrics) {
    ampify::PlayerSnapshot snapshot;
    ampify::TrackInfo track;
    track.id = id;
    snapshot.track = track;
    snapshot.lyricsAvailable = lyrics;
    return snapshot;
}

}  // namespace

TEST_CASE(PlayerWatcher_LyricsChangeWhenTheLyricsOfTheTrackBecomeAvailable) {
    ampify::PlayerWatcher watcher;
    CHECK(watcher.Poll(Snap("a", false)).empty());
    CHECK(watcher.Poll(Snap("a", false)).empty());
    auto events = watcher.Poll(Snap("a", true));
    CHECK(events.size() == 1 && events[0] == "lyricschange");
    CHECK(watcher.Poll(Snap("a", true)).empty());
}

TEST_CASE(PlayerWatcher_ANewTrackWithLyricsAlreadyThereReportsBothInOrder) {
    ampify::PlayerWatcher watcher;
    watcher.Poll(Snap("a", true));
    auto events = watcher.Poll(Snap("b", true));
    CHECK(events.size() == 2 && events[0] == "trackchange" && events[1] == "lyricschange");
    CHECK(watcher.Poll(Snap("b", true)).empty());
}

TEST_CASE(PlayerWatcher_NoLyricsChangeWhenNothingIsLoadedOrTheFirstSnapshotHasThem) {
    ampify::PlayerWatcher watcher;
    CHECK(watcher.Poll(Snap("a", true)).empty());  // the first snapshot only records the state
    ampify::PlayerSnapshot none;
    none.lyricsAvailable = true;
    auto events = watcher.Poll(none);
    CHECK(events.size() == 1 && events[0] == "trackchange");
    // Lyrics that vanish and come back are announced again.
    watcher.Poll(Snap("a", false));
    auto again = watcher.Poll(Snap("a", true));
    CHECK(again.size() == 1 && again[0] == "lyricschange");
}

TEST_CASE(PlayerWatcher_StateChangesWhenTheLengthBecomesKnownOrAfterASeek) {
    auto at = [](bool playing, int64_t positionMs, int64_t durationMs) {
        ampify::PlayerSnapshot snapshot = Snap("a", false);
        snapshot.playing = playing;
        snapshot.positionMs = positionMs;
        snapshot.durationMs = durationMs;
        return snapshot;
    };
    ampify::PlayerWatcher watcher;
    watcher.Poll(at(true, 0, 0), 1000);
    auto known = watcher.Poll(at(true, 250, 163'000), 1250);  // the length arrives after the track
    CHECK(known.size() == 1 && known[0] == "statechange");
    CHECK(watcher.Poll(at(true, 500, 163'000), 1500).empty());  // plain progress
    CHECK(watcher.Poll(at(true, 900, 163'000), 2000).empty());  // a little drift is not a seek
    auto forward = watcher.Poll(at(true, 60'000, 163'000), 2250);
    CHECK(forward.size() == 1 && forward[0] == "statechange");
    auto back = watcher.Poll(at(true, 10'000, 163'000), 2500);
    CHECK(back.size() == 1 && back[0] == "statechange");
    watcher.Poll(at(false, 10'250, 163'000), 2750);
    auto paused = watcher.Poll(at(false, 90'000, 163'000), 3000);  // a seek while paused
    CHECK(paused.size() == 1 && paused[0] == "statechange");
    CHECK(watcher.Poll(at(false, 90'000, 163'000), 9000).empty());
    // Without the time of the poll, only the start and the stop count.
    CHECK(watcher.Poll(at(false, 5'000, 163'000)).empty());
}

// ---- Ampify.player.lyrics() ---------------------------------------------------------------------------------------

TEST_CASE(PlayerLyrics_ReturnsTheParsedLyricsToAnElementThatMayRead) {
    LyricsFakeHost host;
    host.raw = ampify::RawLyrics{kWordTtml, "", "en"};
    std::ostringstream out;
    ampify::Journal journal(out, [] { return std::string("t"); });
    auto element = Make(host, journal, "reader", R"("player.read")", R"(
        const l = Ampify.player.lyrics();
        Ampify.log(l.source + " " + l.synced + " " + l.language + " " + l.lines.length + " " + l.lines[0].words[1].text +
                   " " + l.lines[1].start + " " + JSON.stringify(l.text));)");
    CHECK(element->Start());
    CHECK(out.str().find("[reader] apple true en 2 wor&ld 4000 \"Hello wor&ld\\nSecond line\"") != std::string::npos);
}

TEST_CASE(PlayerLyrics_IsNullWithoutLyricsOrWithoutTheService) {
    LyricsFakeHost host;
    std::ostringstream out;
    ampify::Journal journal(out, [] { return std::string("t"); });
    auto none = Make(host, journal, "none", R"("player.read")", R"(Ampify.log("value " + Ampify.player.lyrics());)");
    CHECK(none->Start());
    CHECK(out.str().find("[none] value null") != std::string::npos);

    ampify::test::FakeHost plain;  // a host without the capability
    auto missing = Make(plain, journal, "missing", R"("player.read")", R"(Ampify.log("value " + Ampify.player.lyrics());)");
    CHECK(missing->Start());
    CHECK(out.str().find("[missing] value null") != std::string::npos);
}

TEST_CASE(PlayerLyrics_NeedsThePlayerReadPermission) {
    LyricsFakeHost host;
    host.raw = ampify::RawLyrics{"", "text", ""};
    std::ostringstream out;
    ampify::Journal journal(out, [] { return std::string("t"); });
    auto element = Make(host, journal, "denied", "",
                        R"(try { Ampify.player.lyrics(); } catch (e) { Ampify.log(e.name); })");
    CHECK(element->Start());
    CHECK(out.str().find("PermissionDenied") != std::string::npos);
}

TEST_CASE(Lyrics_CurrentGivesThePlayerLyricsAsAPromise) {
    LyricsFakeHost host;
    host.raw = ampify::RawLyrics{"", "La la", "fr"};
    std::ostringstream out;
    ampify::Journal journal(out, [] { return std::string("t"); });
    auto element = Make(host, journal, "promised", R"("player.read")", R"(
        Ampify.lyrics.current().then((l) => Ampify.log("current " + l.text + " " + l.language));)");
    CHECK(element->Start());
    element->Pump();
    CHECK(out.str().find("[promised] current La la fr") != std::string::npos);

    auto refused = Make(host, journal, "refused", "",
                        R"(Ampify.lyrics.current().catch((e) => Ampify.log("rejected " + e.name));)");
    CHECK(refused->Start());
    refused->Pump();
    CHECK(out.str().find("[refused] rejected PermissionDenied") != std::string::npos);
}

TEST_CASE(PlayerLyrics_TheLyricschangeEventReachesTheElement) {
    LyricsFakeHost host;
    std::ostringstream out;
    ampify::Journal journal(out, [] { return std::string("t"); });
    auto element = Make(host, journal, "listener", R"("player.read")",
                        R"(Ampify.player.on("lyricschange", () => Ampify.log("lyrics now " + Ampify.player.lyrics().text));)");
    CHECK(element->Start());
    host.raw = ampify::RawLyrics{"", "La la", ""};
    element->DeliverPlayer("lyricschange", Snap("a", true));
    element->Pump();
    CHECK(out.str().find("[listener] lyrics now La la") != std::string::npos);
}
