// Base64 and multipart/form-data bodies (src/core/form_data.*), and the two APIs built on them: Ampify.http.postForm
// and Ampify.player.artwork.

#include <string>

#include "../src/core/form_data.hpp"
#include "pump.hpp"
#include "service_host.hpp"
#include "test.hpp"

TEST_CASE(FormData_Base64RoundTripsAnyBytes) {
    CHECK(ampify::Base64Encode("") == "");
    CHECK(ampify::Base64Encode("f") == "Zg==");
    CHECK(ampify::Base64Encode("fo") == "Zm8=");
    CHECK(ampify::Base64Encode("foo") == "Zm9v");
    CHECK(ampify::Base64Encode("foobar") == "Zm9vYmFy");
    std::string bytes;
    for (int i = 0; i < 256; i++) bytes.push_back(static_cast<char>(i));
    CHECK(ampify::Base64Decode(ampify::Base64Encode(bytes)).value_or("") == bytes);
    CHECK(ampify::Base64Decode("Zm8=").value_or("") == "fo");

    CHECK(!ampify::Base64Decode("Zm8"));     // not a multiple of 4
    CHECK(!ampify::Base64Decode("Zm 8"));    // space
    CHECK(!ampify::Base64Decode("Z=8="));    // padding inside
    CHECK(!ampify::Base64Decode("Zm8*"));    // outside the alphabet
}

TEST_CASE(FormData_MultipartBodyHasTextAndFileParts) {
    std::string body = ampify::MultipartBody(
        {{"reqtype", "fileupload", "", ""}, {"file", std::string("\x89PNG\0", 5), "cover.png", "image/png"}}, "b0");
    CHECK(body == std::string("--b0\r\nContent-Disposition: form-data; name=\"reqtype\"\r\n\r\nfileupload\r\n"
                              "--b0\r\nContent-Disposition: form-data; name=\"file\"; filename=\"cover.png\"\r\n"
                              "Content-Type: image/png\r\n\r\n") +
                      std::string("\x89PNG\0", 5) + "\r\n--b0--\r\n");
}

TEST_CASE(HttpPostForm_SendsAFileAsMultipart) {
    ampify::test::ServiceFixture<> f;
    auto element = f.Make("uploader", R"(
        Ampify.http.postForm("https://upload.example.org/api", {
          reqtype: "fileupload",
          "files[]": { filename: "cover.jpg", type: "image/jpeg", base64: "/9j/AA==" },
        }).then((r) => Ampify.log("sent " + r.status));
        Ampify.http.postForm("https://upload.example.org/api", { "bad name": "x" }).catch((e) => Ampify.log("name " + e.name));
        Ampify.http.postForm("https://upload.example.org/api", { f: { filename: "../x", base64: "" } })
          .catch((e) => Ampify.log("path " + e.name));
        Ampify.http.postForm("https://upload.example.org/api", { f: { filename: "a.jpg", base64: "%%%%" } })
          .catch((e) => Ampify.log("bytes " + e.name));
        Ampify.http.postForm("https://evil.example/", { a: "1" }).catch((e) => Ampify.log("host " + e.name));
    )", R"("network:upload.example.org")");
    CHECK(element->Start());
    CHECK(ampify::test::PumpUntil(*element, [&] { return f.Logged("sent 201"); }));
    CHECK(f.Logged("name TypeError"));
    CHECK(f.Logged("path TypeError"));
    CHECK(f.Logged("bytes TypeError"));
    CHECK(f.Logged("host PermissionDenied"));

    CHECK(f.host.posts.size() == 1);
    if (f.host.posts.size() != 1) return;
    const auto& post = f.host.posts[0];
    CHECK(post.headers.size() == 1 && post.headers[0].first == "content-type");
    std::string type = post.headers.empty() ? "" : post.headers[0].second;
    CHECK(type.rfind("multipart/form-data; boundary=", 0) == 0);
    std::string boundary = type.substr(type.find('=') + 1);
    CHECK(post.body.find("--" + boundary + "\r\n") == 0);
    CHECK(post.body.find("name=\"files[]\"; filename=\"cover.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n" +
                         std::string("\xFF\xD8\xFF\x00", 4)) != std::string::npos);
}

TEST_CASE(PlayerArtwork_GivesTheCoverAsBase64OrNull) {
    ampify::test::ServiceFixture<> f;
    f.host.artwork = ampify::ArtworkHost::File{"image/png", "PNG"};
    auto element = f.Make("cover", R"(
        Ampify.player.artwork().then((a) => Ampify.log("cover " + a.type + " " + a.base64));
    )", R"("player.read")");
    CHECK(element->Start());
    CHECK(ampify::test::PumpUntil(*element, [&] { return f.Logged("cover image/png UE5H"); }));
    {
        std::lock_guard<std::mutex> lock(f.host.httpMutex);
        f.host.artwork.reset();
    }
    auto none = f.Make("cover-none", R"(
        Ampify.player.artwork().then((a) => Ampify.log("then " + a));
    )", R"("player.read")");
    CHECK(none->Start());
    CHECK(ampify::test::PumpUntil(*none, [&] { return f.Logged("then null"); }));

    auto denied = f.Make("cover-denied", R"(
        try { Ampify.player.artwork().catch((e) => Ampify.log("denied " + e.name)); } catch (e) { Ampify.log("denied " + e.name); }
    )");
    CHECK(denied->Start());
    CHECK(ampify::test::PumpUntil(*denied, [&] { return f.Logged("denied PermissionDenied"); }));
}
