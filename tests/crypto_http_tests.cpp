// Ampify.crypto.md5 (core/md5.*, checked against the RFC 1321 vectors) and Ampify.http.post, which follows the rules of
// Ampify.http.get: https only, declared hosts only, and a short list of headers.

#include <string>

#include "../core/md5.hpp"
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

TEST_CASE(HttpPost_HostWithoutTheServiceAnswersStatusZero) {
    ampify::test::ServiceFixture<ampify::test::FakeHost> f;
    auto element = f.Make("poster-plain", R"(
        Ampify.http.post("https://api.example.org/", "x").then((r) => Ampify.log("code " + r.status + " " + r.text));
    )", R"("network:api.example.org")");
    CHECK(element->Start());
    CHECK(ampify::test::PumpUntil(*element, [&] { return f.Logged("code 0 sending is not supported by this host"); }));
}
