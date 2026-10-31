// Ampify.http runs the request off the JavaScript thread: the checks stay synchronous, the answer comes on a later
// Pump, a stopped element drops what is still in flight, and the number of requests in flight is capped.

#include <string>

#include "pump.hpp"
#include "service_host.hpp"
#include "test.hpp"

namespace {

constexpr const char* kNetwork = R"("network:api.example.org")";

size_t Count(const std::string& text, const std::string& part) {
    size_t count = 0;
    for (size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + part.size())) count++;
    return count;
}

}  // namespace

TEST_CASE(HttpAsync_SlowRequestDoesNotBlockATimerOfTheSameElement) {
    ampify::test::ServiceFixture<> f;
    f.host.HoldHttp();
    auto element = f.Make("http-timer", R"(
        Ampify.http.get("https://api.example.org/slow").then((r) => Ampify.log("got " + r.status));
        setTimeout(() => Ampify.log("tick"), 0);
    )", kNetwork);
    CHECK(element->Start());
    CHECK(ampify::test::PumpUntil(*element, [&] { return f.Logged("tick"); }));  // a timer waits 1 ms at least
    CHECK(!f.Logged("got "));

    f.host.ReleaseHttp();
    CHECK(ampify::test::PumpUntil(*element, [&] { return f.Logged("got 200"); }));
}

TEST_CASE(HttpAsync_PromiseResolvesWithTheResponseOnALaterPump) {
    ampify::test::ServiceFixture<> f;
    f.host.HoldHttp();
    f.host.httpResponse = {404, "missing"};
    auto element = f.Make("http-later", R"(
        Ampify.http.get("https://api.example.org/x").then((r) => Ampify.log("got " + r.status + " " + r.text));
        Ampify.http.post("https://api.example.org/y", "a=1").then((r) => Ampify.log("sent " + r.status + " " + r.text));
    )", kNetwork);
    CHECK(element->Start());
    CHECK(f.host.WaitForRequests(1));
    element->Pump();
    CHECK(!f.Logged("got "));

    f.host.ReleaseHttp();
    CHECK(ampify::test::PumpUntil(*element, [&] { return f.Logged("got 404 missing") && f.Logged("sent 201 sent"); }));
}

TEST_CASE(HttpAsync_NetworkFailureResolvesWithStatusZero) {
    ampify::test::ServiceFixture<> f;
    f.host.httpResponse = {0, "network: down"};
    auto element = f.Make("http-down", R"(
        Ampify.http.get("https://api.example.org/x").then((r) => Ampify.log("got " + r.status + " " + r.text),
                                                           (e) => Ampify.log("threw " + e.name));
    )", kNetwork);
    CHECK(element->Start());
    CHECK(ampify::test::PumpUntil(*element, [&] { return f.Logged("got 0 network: down"); }));
    CHECK(!f.Logged("threw"));
}

TEST_CASE(HttpAsync_RequestInFlightAtStopIsDropped) {
    ampify::test::ServiceFixture<> f;
    f.host.HoldHttp();
    auto element = f.Make("http-stop", R"(
        Ampify.http.get("https://api.example.org/x").then((r) => Ampify.log("got " + r.status));
    )", kNetwork);
    CHECK(element->Start());
    CHECK(f.host.WaitForRequests(1));

    element->Stop();  // does not wait for the request
    CHECK(element->httpInFlight() == 1);
    f.host.ReleaseHttp();
    CHECK(ampify::test::PumpUntil(*element, [&] { return element->httpInFlight() == 0; }));
    element->Pump();
    CHECK(!f.Logged("got "));
}

TEST_CASE(HttpAsync_AnswerOfAStoppedRunNeverReachesTheRestartedElement) {
    ampify::test::ServiceFixture<> f;
    f.host.HoldHttp();
    auto element = f.Make("http-restart", R"(
        Ampify.http.get("https://api.example.org/x").then((r) => Ampify.log("got " + r.status));
    )", kNetwork);
    CHECK(element->Start());
    CHECK(f.host.WaitForRequests(1));
    element->Stop();
    CHECK(element->Start());
    CHECK(f.host.WaitForRequests(2));

    f.host.ReleaseHttp();
    CHECK(ampify::test::PumpUntil(*element, [&] { return f.Logged("got 200"); }));
    CHECK(ampify::test::PumpUntil(*element, [&] { return element->httpInFlight() == 0; }));
    element->Pump();
    CHECK(Count(f.out.str(), "got ") == 1);
}

TEST_CASE(HttpAsync_RefusalsStillRejectBeforeAnyRequestLeaves) {
    ampify::test::ServiceFixture<> f;
    f.host.HoldHttp();
    auto element = f.Make("http-refused", R"(
        Ampify.http.get("https://evil.example/").catch((e) => Ampify.log("host " + e.name));
        Ampify.http.get("http://api.example.org/").catch((e) => Ampify.log("plain " + e.name));
        Ampify.http.post("https://api.example.org/", "x".repeat(65 * 1024)).catch((e) => Ampify.log("body " + e.name));
        Ampify.http.post("https://api.example.org/", "", { headers: { Cookie: "x" } })
          .catch((e) => Ampify.log("header " + e.name));
    )", kNetwork);
    CHECK(element->Start());  // the gate is shut: only a synchronous refusal can have logged by now
    CHECK(f.Logged("host PermissionDenied"));
    CHECK(f.Logged("plain TypeError"));
    CHECK(f.Logged("body RangeError"));
    CHECK(f.Logged("header TypeError"));
    CHECK(f.host.RequestCount() == 0);
    f.host.ReleaseHttp();
}

TEST_CASE(HttpAsync_RequestsInFlightAreCappedPerElement) {
    ampify::test::ServiceFixture<> f;
    f.host.HoldHttp();
    auto element = f.Make("http-cap", R"(
        for (let i = 0; i < 6; i++) {
          Ampify.http.get("https://api.example.org/" + i).then(() => Ampify.log("got"),
                                                                (e) => Ampify.log("cap " + e.name));
        }
    )", kNetwork);
    CHECK(element->Start());
    CHECK(Count(f.out.str(), "cap RangeError") == 6 - ampify::kMaxHttpInFlight);

    f.host.ReleaseHttp();
    CHECK(ampify::test::PumpUntil(*element, [&] { return Count(f.out.str(), "got") == ampify::kMaxHttpInFlight; }));
}
