// Ampify.crypto.md5 (src/core/md5.*, checked against the RFC 1321 vectors) and Ampify.http.post, which follows the rules of
// Ampify.http.get: https only, declared hosts only, and a short list of headers.

#include <string>

#include "../src/core/md5.hpp"
#include "pump.hpp"
#include "service_host.hpp"
#include "test.hpp"

TEST_CASE(Md5_MatchesTheRfcVectors) {
    CHECK(ampify::Md5Hex("") == "d41d8cd98f00b204e9800998ecf8427e");
    CHECK(ampify::Md5Hex("a") == "0cc175b9c0f1b6a831c399e269772661");
    CHECK(ampify::Md5Hex("abc") == "900150983cd24fb0d6963f7d28e17f72");
    CHECK(ampify::Md5Hex("message digest") == "f96b697d7cb7938d525a2f31aaf161d0");
    CHECK(ampify::Md5Hex("abcdefghijklmnopqrstuvwxyz") == "c3fcd3d76192e4007dfb496cca67e13b");
    CHECK(ampify::Md5Hex("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789") ==
          "d174ab98d277d9f5a5611c2c9f419d9f");
    CHECK(ampify::Md5Hex("12345678901234567890123456789012345678901234567890123456789012345678901234567890") ==
          "57edf4a22be3c955ac49da2e2107b67a");
    // 55, 56 and 64 bytes: the padding boundaries.
    CHECK(ampify::Md5Hex(std::string(55, 'a')) == "ef1772b6dff9a122358552954ad0df65");
    CHECK(ampify::Md5Hex(std::string(56, 'a')) == "3b0c8ac703f828b04c6c197006d17218");
    CHECK(ampify::Md5Hex(std::string(64, 'a')) == "014842d480b571495a4a0363793f7367");
}

TEST_CASE(CryptoModule_HashesUtf8Text) {
    ampify::test::ServiceFixture<> f;
    auto element = f.Make("crypto", R"(
        Ampify.log("abc " + Ampify.crypto.md5("abc"));
        Ampify.log("utf8 " + Ampify.crypto.md5("é"));
    )");
    CHECK(element->Start());
    CHECK(f.Logged("abc 900150983cd24fb0d6963f7d28e17f72"));
    CHECK(f.Logged("utf8 " + ampify::Md5Hex("\xC3\xA9")));
}

TEST_CASE(HttpPost_FollowsTheRulesOfGet) {
    ampify::test::ServiceFixture<> f;
    auto element = f.Make("poster", R"(
        Ampify.http.post("https://api.example.org/form", "a=1&b=2").then((r) => Ampify.log("form " + r.status + " " + r.text));
        Ampify.http.post("https://api.example.org/json", { titre: "x" }, { headers: { Authorization: "Token k" } })
          .then((r) => Ampify.log("json " + r.status));
        Ampify.http.post("https://evil.example/", "").catch((e) => Ampify.log("host " + e.name));
        Ampify.http.post("http://api.example.org/", "").catch((e) => Ampify.log("plain " + e.name));
        Ampify.http.post("https://api.example.org/", "", { headers: { Cookie: "x" } }).catch((e) => Ampify.log("cookie " + e.name));
        Ampify.http.post("https://api.example.org/", "", { headers: { Accept: "a\r\nX-Evil: 1" } })
          .catch((e) => Ampify.log("line " + e.name));
    )", R"("network:api.example.org")");
    CHECK(element->Start());
    CHECK(ampify::test::PumpUntil(*element, [&] { return f.Logged("form 201 sent") && f.Logged("json 201"); }));
    CHECK(f.Logged("host PermissionDenied"));
    CHECK(f.Logged("plain TypeError"));
    CHECK(f.Logged("cookie TypeError"));
    CHECK(f.Logged("line TypeError"));

    // The two requests run on their own threads: they reach the host in either order.
    CHECK(f.host.posts.size() == 2);
    if (f.host.posts.size() == 2) {
        bool formFirst = f.host.posts[0].url == "https://api.example.org/form";
        const auto& form = f.host.posts[formFirst ? 0 : 1];
        CHECK(form.url == "https://api.example.org/form" && form.body == "a=1&b=2");
        CHECK((form.headers == ampify::HttpHeaders{{"content-type", "application/x-www-form-urlencoded"}}));
        const auto& json = f.host.posts[formFirst ? 1 : 0];
        CHECK(json.body == R"({"titre":"x"})");
        CHECK((json.headers == ampify::HttpHeaders{{"authorization", "Token k"}, {"content-type", "application/json"}}));
    }
}

TEST_CASE(HttpSend_PutPatchAndDeleteFollowTheRulesOfPost) {
    ampify::test::ServiceFixture<> f;
    auto element = f.Make("sender", R"(
        Ampify.http.put("https://api.example.org/a", { on: true }).then((r) => Ampify.log("put " + r.status));
        Ampify.http.patch("https://api.example.org/b", "x=1").then((r) => Ampify.log("patch " + r.status));
        Ampify.http.del("https://api.example.org/c", { headers: { Authorization: "Token k" } })
          .then((r) => Ampify.log("del " + r.status));
        Ampify.http.put("https://evil.example/", "").catch((e) => Ampify.log("host " + e.name));
        Ampify.http.del("https://api.example.org/", { headers: { Cookie: "x" } }).catch((e) => Ampify.log("cookie " + e.name));
    )", R"("network:api.example.org")");
    CHECK(element->Start());
    CHECK(ampify::test::PumpUntil(*element, [&] {
        return f.Logged("put 201") && f.Logged("patch 201") && f.Logged("del 201");
    }));
    CHECK(f.Logged("host PermissionDenied"));
    CHECK(f.Logged("cookie TypeError"));
    CHECK(f.host.sent.size() == 3);
    for (const auto& request : f.host.sent) {
        if (request.url == "PUT https://api.example.org/a") {
            CHECK(request.body == R"({"on":true})");
            CHECK((request.headers == ampify::HttpHeaders{{"content-type", "application/json"}}));
        } else if (request.url == "PATCH https://api.example.org/b") {
            CHECK(request.body == "x=1");
        } else {
            CHECK(request.url == "DELETE https://api.example.org/c");
            CHECK(request.body.empty());
            CHECK((request.headers == ampify::HttpHeaders{{"authorization", "Token k"}}));
        }
    }
    CHECK(f.host.posts.empty());
}

TEST_CASE(HttpSend_HostWithoutTheServiceAnswersStatusZero) {
    ampify::test::ServiceFixture<ampify::test::FakeHost> f;
    auto element = f.Make("plain", R"(
        Ampify.http.del("https://api.example.org/c").then((r) => Ampify.log("del " + r.status));
    )", R"("network:api.example.org")");
    CHECK(element->Start());
    CHECK(ampify::test::PumpUntil(*element, [&] { return f.Logged("del 0"); }));
}

// A GET may carry the same short list of headers, origin too (an API that only answers its own site's token).
TEST_CASE(HttpGet_CarriesTheCheckedHeadersOfPost) {
    ampify::test::ServiceFixture<> f;
    auto element = f.Make("getter", R"(
        Ampify.http.get("https://api.example.org/song", { headers: { Authorization: "Bearer t", Origin: "https://example.org" } })
          .then((r) => Ampify.log("headed " + r.status + " " + r.text));
        Ampify.http.get("https://api.example.org/", { headers: { Cookie: "x" } }).catch((e) => Ampify.log("cookie " + e.name));
        Ampify.http.get("https://api.example.org/", { headers: { Accept: "a\r\nX-Evil: 1" } })
          .catch((e) => Ampify.log("line " + e.name));
    )", R"("network:api.example.org")");
    CHECK(element->Start());
    CHECK(ampify::test::PumpUntil(*element, [&] { return f.Logged("headed 201 sent"); }));
    CHECK(f.Logged("cookie TypeError"));
    CHECK(f.Logged("line TypeError"));
    CHECK(f.host.headedGets.size() == 1);
    if (f.host.headedGets.size() == 1) {
        CHECK(f.host.headedGets[0].url == "https://api.example.org/song");
        CHECK((f.host.headedGets[0].headers ==
               ampify::HttpHeaders{{"authorization", "Bearer t"}, {"origin", "https://example.org"}}));
    }
}

TEST_CASE(HttpPost_AnAnswerThatComesBeforeTheStartEndsIsKept) {
    ampify::test::ServiceFixture<> f;
    // main.js keeps running after its request: the fake host answers at once, before Start returns.
    auto element = f.Make("early", R"(
        Ampify.http.post("https://api.example.org/form", "a=1").then((r) => Ampify.log("early " + r.status));
        const until = Date.now() + 200;
        while (Date.now() < until) {}
    )", R"("network:api.example.org")");
    CHECK(element->Start());
    CHECK(ampify::test::PumpUntil(*element, [&] { return f.Logged("early 201"); }, std::chrono::seconds(2)));
}

TEST_CASE(HttpPost_HostWithoutTheServiceAnswersStatusZero) {
    ampify::test::ServiceFixture<ampify::test::FakeHost> f;
    auto element = f.Make("poster-plain", R"(
        Ampify.http.post("https://api.example.org/", "x").then((r) => Ampify.log("code " + r.status + " " + r.text));
    )", R"("network:api.example.org")");
    CHECK(element->Start());
    CHECK(ampify::test::PumpUntil(*element, [&] { return f.Logged("code 0 sending is not supported by this host"); }));
}

TEST_CASE(HttpOpen_ShowsADeclaredPageInTheBrowserOnceInAWhile) {
    ampify::test::ServiceFixture<> f;
    auto element = f.Make("opener", R"(
        Ampify.log("first " + Ampify.http.open("https://www.example.org/auth?token=t"));
        Ampify.log("again " + Ampify.http.open("https://www.example.org/other"));
        try { Ampify.http.open("https://evil.example/"); } catch (e) { Ampify.log("host " + e.name); }
        try { Ampify.http.open("http://www.example.org/"); } catch (e) { Ampify.log("plain " + e.name); }
    )", R"("network:www.example.org")");
    CHECK(element->Start());
    CHECK(f.Logged("first true"));
    CHECK(f.Logged("again false"));  // one page every few seconds: a faulty loop cannot flood the browser
    CHECK(f.Logged("host PermissionDenied"));
    CHECK(f.Logged("plain TypeError"));
    CHECK((f.host.openedPages == std::vector<std::string>{"https://www.example.org/auth?token=t"}));
}

TEST_CASE(HttpOpen_AnAppleMusicLinkOpensInAppleMusic) {
    ampify::test::ServiceFixture<> f;
    auto element = f.Make("apple-opener", R"(
        Ampify.log("opened " + Ampify.http.open("https://music.apple.com/fr/album/v/1?i=2"));
    )", R"("network:music.apple.com")");
    CHECK(element->Start());
    CHECK(f.Logged("opened true"));
    CHECK((f.host.openedPages == std::vector<std::string>{"music://music.apple.com/fr/album/v/1?i=2"}));
}

// The video of a declared host comes back as a GIF ready for postForm; the host makes it, the core checks and encodes.
TEST_CASE(HttpGifFromVideo_TurnsAVideoOfADeclaredHostIntoAGif) {
    ampify::test::ServiceFixture<> f;
    f.host.httpResponse = {200, "video bytes"};
    auto element = f.Make("gif-maker", R"(
        Ampify.http.gifFromVideo("https://media.example.org/loop.mp4", { side: 120, fps: 8, seconds: 6 })
          .then((gif) => Ampify.log("gif " + gif.type + " " + gif.base64));
        Ampify.http.gifFromVideo("https://other.example.org/loop.mp4").catch((e) => Ampify.log("host " + e.name));
        Ampify.http.gifFromVideo("https://media.example.org/big.mp4", { side: 5000, fps: -3, seconds: 900 })
          .then(() => Ampify.log("clamped"));
    )", R"("network:media.example.org")");
    CHECK(element->Start());
    CHECK(ampify::test::PumpUntil(*element, [&] { return f.Logged("gif image/gif R0lGODlh") && f.Logged("clamped"); }));
    CHECK(f.Logged("host PermissionDenied"));
    std::lock_guard<std::mutex> lock(f.host.httpMutex);
    CHECK(f.host.gifCalls.size() == 2);
    if (f.host.gifCalls.size() == 2) {
        // The two requests run on two workers: either may reach the host first.
        bool inOrder = f.host.gifCalls[0].options.side == 120;
        const auto& asked = f.host.gifCalls[inOrder ? 0 : 1];
        const auto& clamped = f.host.gifCalls[inOrder ? 1 : 0];
        CHECK(asked.video == "video bytes");
        CHECK(asked.options.side == 120);
        CHECK(asked.options.fps == 8);
        CHECK(asked.options.maxMs == 6000);
        // Out of range: the largest GIF Ampify makes, and one frame a second.
        CHECK(clamped.options.side == 480);
        CHECK(clamped.options.fps == 1);
        CHECK(clamped.options.maxMs == 30000);
    }
}

// Regression: a host's answer stops at 8 MB, and a video cut there made a short GIF, kept for weeks.
TEST_CASE(HttpGifFromVideo_RefusesAVideoCutAtTheAnswersLimit) {
    ampify::test::ServiceFixture<> f;
    f.host.httpResponse = {200, std::string(size_t{8} << 20, 'v')};
    auto element = f.Make("gif-cut", R"(
        Ampify.http.gifFromVideo("https://media.example.org/long.mp4").catch((e) => Ampify.log("cut " + e.message));
    )", R"("network:media.example.org")");
    CHECK(element->Start());
    CHECK(ampify::test::PumpUntil(*element, [&] { return f.Logged("cut http.gifFromVideo: the video is over 8 MB"); }));
    std::lock_guard<std::mutex> lock(f.host.httpMutex);
    CHECK(f.host.gifCalls.empty());
}

TEST_CASE(HttpGifFromVideo_RejectsWithTheReason) {
    ampify::test::ServiceFixture<> f;
    f.host.httpResponse = {404, "missing"};
    auto element = f.Make("gif-missing", R"(
        Ampify.http.gifFromVideo("https://media.example.org/gone.mp4").catch((e) => Ampify.log("gone " + e.message));
    )", R"("network:media.example.org")");
    CHECK(element->Start());
    CHECK(ampify::test::PumpUntil(*element, [&] { return f.Logged("gone http.gifFromVideo: the video answered 404"); }));

    f.host.httpResponse = {200, ""};
    auto unreadable = f.Make("gif-unreadable", R"(
        Ampify.http.gifFromVideo("https://media.example.org/odd.mp4").catch((e) => Ampify.log("odd " + e.message));
    )", R"("network:media.example.org")");
    CHECK(unreadable->Start());
    CHECK(ampify::test::PumpUntil(*unreadable, [&] { return f.Logged("odd http.gifFromVideo: no video stream"); }));

    ampify::test::ServiceFixture<ampify::test::FakeHost> plain;
    auto without = plain.Make("gif-plain", R"(
        Ampify.http.gifFromVideo("https://media.example.org/loop.mp4").catch((e) => Ampify.log("plain " + e.message));
    )", R"("network:media.example.org")");
    CHECK(without->Start());
    CHECK(ampify::test::PumpUntil(*without, [&] { return plain.Logged("plain http.gifFromVideo: GIFs are not made by this host"); }));
}

TEST_CASE(HttpOpen_HostWithoutABrowserAnswersFalse) {
    ampify::test::ServiceFixture<ampify::test::FakeHost> f;
    auto element = f.Make("opener-plain", R"(
        Ampify.log("opened " + Ampify.http.open("https://www.example.org/"));
    )", R"("network:www.example.org")");
    CHECK(element->Start());
    CHECK(f.Logged("opened false"));
}
