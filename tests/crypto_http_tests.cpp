// AMK.crypto.md5 (core/md5.*, checked against the RFC 1321 vectors) and AMK.http.post, which follows the rules of
// AMK.http.get: https only, declared hosts only, and a short list of headers.

#include <string>

#include "../core/md5.hpp"
#include "service_host.hpp"
#include "test.hpp"

TEST_CASE(Md5_MatchesTheRfcVectors) {
    CHECK(amk::Md5Hex("") == "d41d8cd98f00b204e9800998ecf8427e");
    CHECK(amk::Md5Hex("a") == "0cc175b9c0f1b6a831c399e269772661");
    CHECK(amk::Md5Hex("abc") == "900150983cd24fb0d6963f7d28e17f72");
    CHECK(amk::Md5Hex("message digest") == "f96b697d7cb7938d525a2f31aaf161d0");
    CHECK(amk::Md5Hex("abcdefghijklmnopqrstuvwxyz") == "c3fcd3d76192e4007dfb496cca67e13b");
    CHECK(amk::Md5Hex("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789") ==
          "d174ab98d277d9f5a5611c2c9f419d9f");
    CHECK(amk::Md5Hex("12345678901234567890123456789012345678901234567890123456789012345678901234567890") ==
          "57edf4a22be3c955ac49da2e2107b67a");
    // 55, 56 and 64 bytes: the padding boundaries.
    CHECK(amk::Md5Hex(std::string(55, 'a')) == "ef1772b6dff9a122358552954ad0df65");
    CHECK(amk::Md5Hex(std::string(56, 'a')) == "3b0c8ac703f828b04c6c197006d17218");
    CHECK(amk::Md5Hex(std::string(64, 'a')) == "014842d480b571495a4a0363793f7367");
}

TEST_CASE(CryptoModule_HashesUtf8Text) {
    amk::test::ServiceFixture<> f;
    auto element = f.Make("crypto", R"(
        AMK.log("abc " + AMK.crypto.md5("abc"));
        AMK.log("utf8 " + AMK.crypto.md5("é"));
    )");
    CHECK(element->Start());
    CHECK(f.Logged("abc 900150983cd24fb0d6963f7d28e17f72"));
    CHECK(f.Logged("utf8 " + amk::Md5Hex("\xC3\xA9")));
}

TEST_CASE(HttpPost_FollowsTheRulesOfGet) {
    amk::test::ServiceFixture<> f;
    auto element = f.Make("poster", R"(
        AMK.http.post("https://api.example.org/form", "a=1&b=2").then((r) => AMK.log("form " + r.status + " " + r.text));
        AMK.http.post("https://api.example.org/json", { titre: "x" }, { headers: { Authorization: "Token k" } })
          .then((r) => AMK.log("json " + r.status));
        AMK.http.post("https://evil.example/", "").catch((e) => AMK.log("hôte " + e.name));
        AMK.http.post("http://api.example.org/", "").catch((e) => AMK.log("clair " + e.name));
        AMK.http.post("https://api.example.org/", "", { headers: { Cookie: "x" } }).catch((e) => AMK.log("cookie " + e.name));
        AMK.http.post("https://api.example.org/", "", { headers: { Accept: "a\r\nX-Evil: 1" } })
          .catch((e) => AMK.log("ligne " + e.name));
    )", R"("network:api.example.org")");
    CHECK(element->Start());
    CHECK(f.Logged("form 201 envoyé"));
    CHECK(f.Logged("json 201"));
    CHECK(f.Logged("hôte PermissionDenied"));
    CHECK(f.Logged("clair TypeError"));
    CHECK(f.Logged("cookie TypeError"));
    CHECK(f.Logged("ligne TypeError"));

    CHECK(f.host.posts.size() == 2);
    if (f.host.posts.size() == 2) {
        const auto& form = f.host.posts[0];
        CHECK(form.url == "https://api.example.org/form" && form.body == "a=1&b=2");
        CHECK((form.headers == amk::HttpHeaders{{"content-type", "application/x-www-form-urlencoded"}}));
        const auto& json = f.host.posts[1];
        CHECK(json.body == R"({"titre":"x"})");
        CHECK((json.headers == amk::HttpHeaders{{"authorization", "Token k"}, {"content-type", "application/json"}}));
    }
}

TEST_CASE(HttpPost_HostWithoutTheServiceAnswersStatusZero) {
    amk::test::ServiceFixture<amk::test::FakeHost> f;
    auto element = f.Make("poster-plain", R"(
        AMK.http.post("https://api.example.org/", "x").then((r) => AMK.log("code " + r.status + " " + r.text));
    )", R"("network:api.example.org")");
    CHECK(element->Start());
    CHECK(f.Logged("code 0 envoi non pris en charge par cet hôte"));
}
