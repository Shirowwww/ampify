// The local remote control without a socket: the HTTP request parser, the loopback and token rules
// (src/core/remote_http.*), the routes (src/core/remote_protocol.*), and Ampify.remote.setState.

#include <optional>
#include <string>
#include <vector>

#include "../src/core/remote_protocol.hpp"
#include "service_host.hpp"
#include "test.hpp"

namespace {

using ampify::remote::ParseStatus;

const std::string kToken = "abcdefghijklmnopqrstuvwxyz0123456789ABCD";

struct FakeBackend : ampify::remote::Backend {
    std::vector<std::string> commands;
    std::vector<double> gains;
    bool accept = true;
    std::string StatusJson() override { return R"({"playing":true})"; }
    bool Command(ampify::remote::Action action) override {
        commands.emplace_back(ampify::remote::ActionName(action));
        return accept;
    }
    std::optional<double> SetGainDb(double db) override {
        gains.push_back(db);
        if (!accept) return std::nullopt;
        return db;
    }
};

ampify::remote::Request Parse(const std::string& text) {
    ampify::remote::Request request;
    CHECK(ampify::remote::ParseRequest(text, request) == ParseStatus::Complete);
    return request;
}

// A request from the remote control's own client: loopback Host header and the token.
std::string Raw(const std::string& method, const std::string& target, const std::string& extra = "",
                const std::string& body = "") {
    std::string text = method + " " + target + " HTTP/1.1\r\nHost: 127.0.0.1:8974\r\n" + extra;
    if (!body.empty()) text += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    return text + "\r\n" + body;
}

std::string WithToken(const std::string& method, const std::string& target, const std::string& body = "") {
    return Raw(method, target, "X-Ampify-Token: " + kToken + "\r\n", body);
}

ampify::remote::Response Handle(const std::string& text, FakeBackend& backend, bool loopback = true) {
    return ampify::remote::Handle(Parse(text), loopback, kToken, backend);
}

}  // namespace

TEST_CASE(RemoteParser_ReadsARequestWithQueryHeadersAndBody) {
    auto request = Parse("POST /volume?db=-6&name=a%20b+c HTTP/1.1\r\nHost: 127.0.0.1\r\nX-Ampify-Token:  t \r\n"
                         "Content-Length: 4\r\n\r\nabcd");
    CHECK(request.method == "POST");
    CHECK(request.path == "/volume");
    CHECK(request.query["db"] == "-6");
    CHECK(request.query["name"] == "a b c");
    CHECK(request.headers["x-ampify-token"] == "t");
    CHECK(request.body == "abcd");
}

TEST_CASE(RemoteParser_AsksForMoreAndRefusesWhatItCannotRead) {
    ampify::remote::Request request;
    CHECK(ampify::remote::ParseRequest("GET /status HTTP/1.1\r\nHost: x", request) == ParseStatus::Incomplete);
    CHECK(ampify::remote::ParseRequest("POST /x HTTP/1.1\r\nContent-Length: 5\r\n\r\nab", request) ==
          ParseStatus::Incomplete);
    CHECK(ampify::remote::ParseRequest("GET status HTTP/1.1\r\n\r\n", request) == ParseStatus::Bad);
    CHECK(ampify::remote::ParseRequest("get /status HTTP/1.1\r\n\r\n", request) == ParseStatus::Bad);
    CHECK(ampify::remote::ParseRequest("GET /status HTTP/2\r\n\r\n", request) == ParseStatus::Bad);
    CHECK(ampify::remote::ParseRequest("POST /x HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n", request) ==
          ParseStatus::Bad);
    CHECK(ampify::remote::ParseRequest("POST /x HTTP/1.1\r\nContent-Length: 1\r\nContent-Length: 1\r\n\r\na", request) ==
          ParseStatus::Bad);
    CHECK(ampify::remote::ParseRequest("GET /x HTTP/1.1\r\nA: b\r\n folded\r\n\r\n", request) == ParseStatus::Bad);
    CHECK(ampify::remote::ParseRequest("GET /x HTTP/1.1\r\nHost: a\r\nHost: b\r\n\r\n", request) == ParseStatus::Bad);
    // A bare CR or LF at the edge of a value is a control character, not space to trim.
    CHECK(ampify::remote::ParseRequest("GET /x HTTP/1.1\r\nHost: a\n\r\n\r\n", request) == ParseStatus::Bad);
    CHECK(ampify::remote::ParseRequest("GET /x HTTP/1.1\r\nHost: \ra\r\n\r\n", request) == ParseStatus::Bad);
    CHECK(ampify::remote::ParseRequest("GET /x?a=%zz HTTP/1.1\r\n\r\n", request) == ParseStatus::Bad);
    CHECK(ampify::remote::ParseRequest("POST /x HTTP/1.1\r\nContent-Length: 999999\r\n\r\n", request) ==
          ParseStatus::TooLarge);
    CHECK(ampify::remote::ParseRequest("GET /x HTTP/1.1\r\nA: " + std::string(9000, 'a'), request) ==
          ParseStatus::TooLarge);
    // Just above the body bound is too large; a number that is not a plausible length is unreadable.
    CHECK(ampify::remote::ParseRequest("POST /x HTTP/1.1\r\nContent-Length: 4097\r\n\r\n", request) ==
          ParseStatus::TooLarge);
    CHECK(ampify::remote::ParseRequest("POST /x HTTP/1.1\r\nContent-Length: 9999999\r\n\r\n", request) ==
          ParseStatus::Bad);
}

TEST_CASE(RemoteProtocol_NamesTheActionsAndAnswersUnreadableRequests) {
    using ampify::remote::Action;
    CHECK(ampify::remote::ActionName(Action::Play) == "play");
    CHECK(ampify::remote::ActionName(Action::Pause) == "pause");
    CHECK(ampify::remote::ActionName(Action::Next) == "next");
    CHECK(ampify::remote::ActionName(Action::Previous) == "previous");
    CHECK(ampify::remote::UnreadableResponse(ParseStatus::TooLarge).status == 413);
    CHECK(ampify::remote::UnreadableResponse(ParseStatus::Bad).status == 400);
}

TEST_CASE(RemoteProtocol_LoopbackAndTokens) {
    for (const char* yes : {"127.0.0.1", "127.255.3.4", "::1", "[::1]", "::ffff:127.0.0.1"}) {
        CHECK(ampify::remote::IsLoopbackText(yes));
    }
    for (const char* no : {"0.0.0.0", "192.168.1.20", "128.0.0.1", "127.0.0", "127.0.0.256", "localhost", "", "::",
                           "10.0.0.1"}) {
        CHECK(!ampify::remote::IsLoopbackText(no));
    }
    CHECK(ampify::remote::IsLoopbackIpv4(0x7F000001));
    CHECK(!ampify::remote::IsLoopbackIpv4(0xC0A80001));

    CHECK(ampify::remote::TokenMatches(kToken, kToken));
    CHECK(!ampify::remote::TokenMatches(kToken, kToken.substr(1)));
    CHECK(!ampify::remote::TokenMatches(kToken, ""));
    CHECK(!ampify::remote::TokenMatches("", ""));
    CHECK(ampify::remote::IsValidToken(kToken));
    CHECK(!ampify::remote::IsValidToken("court"));
    CHECK(!ampify::remote::IsValidToken(std::string(40, '!')));
    std::string a = ampify::remote::NewToken();
    std::string b = ampify::remote::NewToken();
    CHECK(ampify::remote::IsValidToken(a) && a.size() == 40 && a != b);
}

TEST_CASE(RemoteRouter_RefusesOtherPeersHostsPagesAndMissingTokens) {
    FakeBackend backend;
    CHECK(Handle(WithToken("GET", "/status"), backend, false).status == 403);  // a peer that is not loopback
    CHECK(Handle("GET /status HTTP/1.1\r\nX-Ampify-Token: " + kToken + "\r\n\r\n", backend).status == 403);  // no Host
    CHECK(Handle("GET /status HTTP/1.1\r\nHost: evil.example:8974\r\nX-Ampify-Token: " + kToken + "\r\n\r\n", backend)
              .status == 403);  // DNS rebinding
    CHECK(Handle(Raw("POST", "/next", "Origin: https://evil.example\r\nX-Ampify-Token: " + kToken + "\r\n"), backend)
              .status == 403);
    CHECK(Handle(Raw("OPTIONS", "/next"), backend).status == 403);
    CHECK(Handle(Raw("GET", "/status"), backend).status == 401);
    CHECK(Handle(Raw("GET", "/status", "X-Ampify-Token: faux\r\n"), backend).status == 401);
    CHECK(Handle(Raw("GET", "/status?token=" + kToken), backend).status == 400);
    CHECK(backend.commands.empty());

    CHECK(ampify::remote::Handle(Parse(WithToken("GET", "/status")), true, "", backend).status == 503);
}

TEST_CASE(RemoteRouter_RoutesStatusCommandsAndVolume) {
    FakeBackend backend;
    auto status = Handle(WithToken("GET", "/status"), backend);
    CHECK(status.status == 200 && status.body == R"({"playing":true})");
    CHECK(Handle(Raw("GET", "/status", "Authorization: Bearer " + kToken + "\r\n"), backend).status == 200);
    CHECK(Handle(Raw("GET", "/status", "Host2: x\r\nX-Ampify-Token: " + kToken + "\r\n"), backend).status == 200);
    CHECK(Handle(WithToken("POST", "/status"), backend).status == 405);

    for (const char* action : {"play", "pause", "next", "previous"}) {
        auto answer = Handle(WithToken("POST", std::string("/") + action), backend);
        CHECK(answer.status == 200 && answer.body == R"({"ok":true})");
    }
    CHECK((backend.commands == std::vector<std::string>{"play", "pause", "next", "previous"}));
    CHECK(Handle(WithToken("GET", "/next"), backend).status == 405);

    auto volume = Handle(WithToken("POST", "/volume?db=-6"), backend);
    CHECK(volume.status == 200 && volume.body == R"({"ok":true,"gainDb":-6})");
    CHECK(Handle(WithToken("POST", "/volume", R"({"db":-3.5})"), backend).status == 200);
    CHECK(Handle(WithToken("POST", "/volume?db=100"), backend).status == 200);  // bounded before the backend
    CHECK(Handle(WithToken("POST", "/volume?db=fort"), backend).status == 400);
    CHECK(Handle(WithToken("POST", "/volume"), backend).status == 400);
    CHECK((backend.gains == std::vector<double>{-6, -3.5, 12}));

    CHECK(Handle(WithToken("GET", "/ailleurs"), backend).status == 404);

    backend.accept = false;
    auto refused = Handle(WithToken("POST", "/play"), backend);
    CHECK(refused.status == 409 && refused.body.find("\"ok\":false") != std::string::npos);
}

TEST_CASE(RemoteProtocol_SerializesAClosedJsonResponse) {
    std::string bytes = ampify::remote::Serialize({401, R"({"ok":false})"});
    CHECK(bytes.rfind("HTTP/1.1 401 Unauthorized\r\n", 0) == 0);
    CHECK(bytes.find("Content-Length: 12\r\n") != std::string::npos);
    CHECK(bytes.find("Connection: close\r\n") != std::string::npos);
    CHECK(bytes.find("Access-Control") == std::string::npos);  // no CORS, ever
    CHECK(bytes.size() >= 12 && bytes.compare(bytes.size() - 12, 12, R"({"ok":false})") == 0);
}

TEST_CASE(RemoteModule_PublishesStateOnlyWhenTheRemoteIsOn) {
    ampify::test::ServiceFixture<> f;
    const std::string script = R"(
        Ampify.log("published " + Ampify.remote.setState({ counter: 3 }));
        try { Ampify.remote.setState([1]); } catch (e) { Ampify.log("list " + e.name); }
        try { Ampify.remote.setState({ text: "x".repeat(5000) }); } catch (e) { Ampify.log("big " + e.name); }
    )";
    auto off = f.Make("remote-off", script);
    CHECK(off->Start());
    CHECK(f.Logged("published false"));
    CHECK(f.Logged("list TypeError"));
    CHECK(f.Logged("big RangeError"));

    f.host.settings["remote.enabled"] = "1";
    auto on = f.Make("remote-on", script);
    CHECK(on->Start());
    CHECK(f.Logged("published true"));
    CHECK(f.host.remoteStates["remote-on"] == R"({"counter":3})");
    on->Stop();
    CHECK(f.host.remoteStates.count("remote-on") == 0);
}
