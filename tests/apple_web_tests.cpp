// Apple's public web API behind Ampify.music.analysis: the token read from the web player, the ids and storefronts
// kept, the answers merged, the token kept and asked again once when Apple refuses it, the songs missing in the
// user's storefront looked for in the US one.

#include <string>
#include <vector>

#include "../src/core/apple_web.hpp"
#include "../src/core/script_extension.hpp"
#include "support.hpp"
#include "test.hpp"

namespace {

using ampify::HttpResponse;
using ampify::HttpHeaders;
namespace appleweb = ampify::appleweb;

const char kPage[] = R"(<html><script type="module" src="/assets/index~a1B2-c3.js"></script></html>)";
const char kScript[] = R"(x="eyJhbGciOi.notatoken";const t="eyJhbGciOiJFUzI1NiJ9.eyJpc3MiOiJBIn0.c2lnLW5hdHVyZQ";)";
const char kToken[] = "eyJhbGciOiJFUzI1NiJ9.eyJpc3MiOiJBIn0.c2lnLW5hdHVyZQ";

// A stand-in for the network: the page, the script, and an analysis server that knows some songs per storefront.
struct FakeApple {
    std::vector<std::string> asked;  // every URL, in order
    int pageLoads = 0;
    int refusals = 0;  // the next analysis requests answered 401
    std::vector<std::string> knownFr{"111"};
    std::vector<std::string> knownUs{"111", "222"};

    appleweb::Fetch Fetch() {
        return appleweb::Fetch{
            [this](const std::string& url) -> HttpResponse {
                asked.push_back(url);
                if (url == "https://music.apple.com/us/browse") {
                    pageLoads++;
                    return {200, kPage};
                }
                if (url == "https://music.apple.com/assets/index~a1B2-c3.js") return {200, kScript};
                return {404, ""};
            },
            [this](const std::string& url, const HttpHeaders& headers) -> HttpResponse {
                asked.push_back(url);
                bool authorized = !headers.empty() && headers[0].second == std::string("Bearer ") + kToken;
                if (refusals > 0 || !authorized) {
                    refusals--;
                    return {401, ""};
                }
                const auto& known = url.find("/catalog/fr/") != std::string::npos ? knownFr : knownUs;
                std::string data;
                for (const std::string& id : known) {
                    if (url.find(id) == std::string::npos) continue;
                    data += std::string(data.empty() ? "" : ",") + R"({"id":")" + id +
                            R"(","attributes":{"bpm":{"main":106},"valence":{"main":0.9}}})";
                }
                if (data.empty()) return {404, ""};
                return {200, R"({"data":[)" + data + "]}"};
            }};
    }
};

}  // namespace

TEST_CASE(AppleWeb_ReadsThePlayerScriptAndItsToken) {
    CHECK(appleweb::IndexScriptPath(kPage).value_or("") == "/assets/index~a1B2-c3.js");
    CHECK(!appleweb::IndexScriptPath("<script src=\"/assets/vendor.js\">").has_value());
    CHECK(appleweb::TokenInScript(kScript).value_or("") == kToken);
    CHECK(!appleweb::TokenInScript("const t=\"eyJonly.two\";").has_value());
}

TEST_CASE(AppleWeb_KeepsOnlyCatalogIdsAndTwoLetterStorefronts) {
    CHECK((appleweb::CatalogIds({"123", "", "abc", "123", "4567", "1.5", "123456789012345678901"}) ==
           std::vector<std::string>{"123", "4567"}));
    // At most half the cache, so that a storefront's answers and their "us" fallback all fit until they are read.
    std::vector<std::string> many;
    for (int id = 1; id <= 400; id++) many.push_back(std::to_string(id));
    CHECK(appleweb::CatalogIds(many).size() == 250);
    CHECK(appleweb::CatalogIds(many).back() == "250");
    CHECK(appleweb::Storefront("FR") == "fr");
    CHECK(appleweb::Storefront("fra") == "us");
    CHECK(appleweb::Storefront("") == "us");
    CHECK(appleweb::AnalysisAddress("fr", {"1", "2"}) ==
          "https://amp-api.music.apple.com/v1/catalog/fr/audio-analysis?ids=1,2");
}

TEST_CASE(AppleWeb_GivesTheAnalysisAndLooksForMissingSongsInTheUsStore) {
    appleweb::ResetCache();
    FakeApple apple;
    HttpResponse got = appleweb::Analysis(apple.Fetch(), "fr", {"111", "222", "333"});
    CHECK(got.status == 200);
    CHECK(got.text.find(R"("111":{"bpm":{"main":106})") != std::string::npos);
    CHECK(got.text.find(R"("222":)") != std::string::npos);  // found in the US store
    CHECK(got.text.find(R"("333")") == std::string::npos);   // analysed nowhere: left out
    CHECK(apple.pageLoads == 1);

    // Asked again: everything comes from the cache, even the song nobody has (only it is asked again).
    std::size_t before = apple.asked.size();
    got = appleweb::Analysis(apple.Fetch(), "fr", {"111", "222"});
    CHECK(got.status == 200);
    CHECK(apple.asked.size() == before);
}

TEST_CASE(AppleWeb_AsksForANewTokenOnceWhenAppleRefusesIt) {
    appleweb::ResetCache();
    FakeApple apple;
    apple.refusals = 1;
    HttpResponse got = appleweb::Analysis(apple.Fetch(), "us", {"222"});
    CHECK(got.status == 200);
    CHECK(apple.pageLoads == 2);  // the token was read again after the refusal

    appleweb::ResetCache();
    FakeApple refusing;
    refusing.refusals = 5;
    got = appleweb::Analysis(refusing.Fetch(), "us", {"222"});
    CHECK(got.status == 0);
    CHECK(got.text.find("401") != std::string::npos);
}

TEST_CASE(AppleWeb_MusicNeedsPlayerReadAndReadsAMoodFromValenceAndEnergy) {
    ampify::test::ElementFixture<> f;
    auto denied = f.Make({.id = "nomusic", .permissions = ""}, R"(
        try {
          Ampify.music.analysis("1").then(() => Ampify.log("read"), (e) => Ampify.log("refused " + e.name));
        } catch (e) { Ampify.log("thrown " + e.name); })");
    CHECK(denied->Start());
    denied->Pump();
    CHECK(f.Logged("[nomusic] refused PermissionDenied") || f.Logged("[nomusic] thrown PermissionDenied"));
    CHECK(!f.Logged("[nomusic] read"));
    denied->Stop();

    auto reader = f.Make({.id = "mood", .permissions = R"("player.read")"}, R"(
        const m = (v, e) => Ampify.music.mood({ valence: { main: v }, energy: { main: e } });
        Ampify.log("moods " + [m(0.9, 0.8), m(0.9, 0.2), m(0.2, 0.2), m(0.2, 0.9)].join(","));
        Ampify.log("part " + Ampify.music.mood({ valence: { main: 0.9, ending: 0.1 }, energy: { main: 0.1, ending: 0.1 } }, "ending"));
        Ampify.log("none " + Ampify.music.mood({ bpm: { main: 120 } }) + " " + Ampify.music.mood(null));
        Ampify.music.analysis(["abc", ""]).then((items) => Ampify.log("empty " + Object.keys(items).length));)");
    CHECK(reader->Start());
    reader->Pump();
    CHECK(f.Logged("[mood] moods happy,calm,sad,tense"));
    CHECK(f.Logged("[mood] part sad"));
    CHECK(f.Logged("[mood] none null null"));
    CHECK(f.Logged("[mood] empty 0"));
    reader->Stop();
}

TEST_CASE(AppleWeb_SaysWhyWhenTheWebPlayerCannotBeRead) {
    appleweb::ResetCache();
    appleweb::Fetch offline{[](const std::string&) { return HttpResponse{0, "no network"}; },
                            [](const std::string&, const HttpHeaders&) { return HttpResponse{0, "no network"}; }};
    HttpResponse got = appleweb::Analysis(offline, "us", {"1"});
    CHECK(got.status == 0);
    CHECK(got.text.find("music.apple.com") != std::string::npos);
    std::vector<std::pair<std::string, std::string>> items;
    CHECK(!appleweb::CollectItems("not json", items));
    CHECK(appleweb::CollectItems(R"({"data":[{"id":"9","attributes":{"bpm":{"main":1}}},{"id":"8"}]})", items));
    CHECK(items.size() == 1);
}
