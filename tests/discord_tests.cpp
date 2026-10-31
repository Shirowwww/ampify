// Discord Rich Presence: the IPC framing, the handshake and SET_ACTIVITY payloads (core/discord_frame.*), and
// Ampify.discord against a host that records what it is asked to show.

#include <string>

#include "../core/discord_frame.hpp"
#include "service_host.hpp"
#include "test.hpp"

namespace {

using ampify::discord::Opcode;

const std::string kClient = "123456789012345678";

}  // namespace

TEST_CASE(DiscordFrame_EncodesOpcodeAndLengthLittleEndian) {
    std::string frame = ampify::discord::EncodeFrame(Opcode::Frame, "{}");
    CHECK(frame == std::string("\x01\x00\x00\x00\x02\x00\x00\x00{}", 10));
    std::string big = ampify::discord::EncodeFrame(Opcode::Handshake, std::string(300, 'a'));
    CHECK(big.size() == 308);
    CHECK(static_cast<unsigned char>(big[4]) == 0x2C && static_cast<unsigned char>(big[5]) == 0x01);
}

TEST_CASE(DiscordFrame_ReaderCutsFramesFromAnyChunks) {
    std::string bytes = ampify::discord::EncodeFrame(Opcode::Frame, R"({"a":1})") +
                        ampify::discord::EncodeFrame(Opcode::Ping, "{}");
    ampify::discord::FrameReader reader;
    for (char c : bytes.substr(0, 9)) reader.Feed(std::string(1, c));
    CHECK(!reader.Next());  // header and one byte only
    reader.Feed(bytes.substr(9));
    auto first = reader.Next();
    CHECK(first && first->opcode == Opcode::Frame && first->json == R"({"a":1})");
    auto second = reader.Next();
    CHECK(second && second->opcode == Opcode::Ping && second->json == "{}");
    CHECK(!reader.Next());
    CHECK(!reader.failed());
}

TEST_CASE(DiscordFrame_ReaderRefusesBrokenStreams) {
    ampify::discord::FrameReader tooLong;
    tooLong.Feed(std::string("\x01\x00\x00\x00\xFF\xFF\xFF\x00", 8));
    CHECK(!tooLong.Next());
    CHECK(tooLong.failed());

    ampify::discord::FrameReader unknown;
    unknown.Feed(std::string("\x09\x00\x00\x00\x00\x00\x00\x00", 8));
    CHECK(!unknown.Next());
    CHECK(unknown.failed());
}

TEST_CASE(DiscordFrame_HandshakeAndActivityPayloads) {
    CHECK(ampify::discord::HandshakeJson(kClient) == R"({"v":1,"client_id":"123456789012345678"})");

    ampify::discord::Activity activity;
    activity.details = "Title";
    activity.state = "by Artist";
    activity.largeImage = "logo";
    activity.startMs = 1700000000500;
    std::string json = ampify::discord::SetActivityJson(42, activity, "7");
    CHECK(json == R"({"cmd":"SET_ACTIVITY","args":{"pid":42,"activity":{"details":"Title","state":"by Artist",)"
                  R"("timestamps":{"start":1700000000},"assets":{"large_image":"logo"},"instance":false}},"nonce":"7"})");

    std::string cleared = ampify::discord::SetActivityJson(42, std::nullopt, "8");
    CHECK(cleared == R"({"cmd":"SET_ACTIVITY","args":{"pid":42,"activity":null},"nonce":"8"})");
}

TEST_CASE(DiscordFrame_ActivityFromJsonChecksFields) {
    std::string error;
    auto parse = [&](const std::string& text) {
        error.clear();
        return ampify::discord::ActivityFromJson(*ampify::Json::Parse(text), &error);
    };
    auto ok = parse(R"({"clientId":"1","details":"Title","state":"A","endMs":2000})");
    CHECK(ok && ok->details == "Title" && ok->state == "A " && ok->endMs == 2000);  // one byte is padded

    CHECK(!parse(R"({"detail":"faute de frappe"})") && error.find("detail") != std::string::npos);
    CHECK(!parse(R"({"details":3})"));
    CHECK(!parse(R"({"largeImage":"x"})"));  // neither details nor state
    CHECK(!parse(R"({"details":"x","startMs":-5})"));
    CHECK(!parse(R"([1])"));

    // Longer than 128 bytes: cut on a character boundary.
    std::string accents;
    for (int i = 0; i < 100; i++) accents += "é";  // 200 bytes
    auto cut = parse(R"({"details":")" + accents + R"("})");
    CHECK(cut && cut->details.size() == 128);
}

TEST_CASE(DiscordFrame_ReadyErrorAndClientIds) {
    ampify::discord::Frame ready{Opcode::Frame, R"({"cmd":"DISPATCH","evt":"READY","data":{"v":1}})"};
    CHECK(ampify::discord::IsReady(ready));
    CHECK(!ampify::discord::ErrorOf(ready));
    ampify::discord::Frame error{Opcode::Frame, R"({"cmd":"SET_ACTIVITY","evt":"ERROR","data":{"message":"no"}})"};
    CHECK(!ampify::discord::IsReady(error));
    CHECK(ampify::discord::ErrorOf(error).value_or("") == "no");
    ampify::discord::Frame closed{Opcode::Close, R"({"code":4000,"message":"invalid client id"})"};
    CHECK(ampify::discord::ErrorOf(closed).value_or("") == "invalid client id");

    CHECK(ampify::discord::IsValidClientId(kClient));
    CHECK(!ampify::discord::IsValidClientId("1234"));
    CHECK(!ampify::discord::IsValidClientId("12345678901234567x"));
}

TEST_CASE(DiscordModule_NeedsPermissionSettingAndValidActivity) {
    ampify::test::ServiceFixture<> f;
    const std::string script = R"(
        try { Ampify.discord.setActivity({ clientId: "123456789012345678", details: "x" }); }
        catch (e) { Ampify.log("refused " + e.name); }
    )";
    auto without = f.Make("discord-none", script);
    CHECK(without->Start());
    CHECK(f.Logged("refused PermissionDenied"));

    auto off = f.Make("discord-off", R"(
        Ampify.log("shown " + Ampify.discord.setActivity({ clientId: "123456789012345678", details: "Title" }));
    )", R"("ipc.discord")");
    CHECK(off->Start());
    CHECK(f.Logged("shown false"));
    CHECK(f.Logged("Discord: off (setting discord.enabled is 0)"));
    CHECK(f.host.activities.empty());

    f.host.settings["discord.enabled"] = "1";
    auto on = f.Make("discord-on", R"(
        Ampify.log("shown " + Ampify.discord.setActivity({ clientId: "123456789012345678", details: "Title",
                                                       state: "Artist", startMs: 1000 }));
        try { Ampify.discord.setActivity({ clientId: "abc", details: "x" }); } catch (e) { Ampify.log("id " + e.name); }
        try { Ampify.discord.setActivity({ clientId: "123456789012345678", detail: "x" }); }
        catch (e) { Ampify.log("field " + e.name); }
    )", R"("ipc.discord")");
    CHECK(on->Start());
    CHECK(f.Logged("shown true"));
    CHECK(f.Logged("id TypeError"));
    CHECK(f.Logged("field TypeError"));
    CHECK(f.host.activities.size() == 1);
    CHECK(f.host.activities[0].elementId == "discord-on" && f.host.activities[0].clientId == kClient);
    CHECK(f.host.activities[0].activity.state == "Artist" && f.host.activities[0].activity.startMs == 1000);

    on->Stop();  // the presence goes away with the element
    CHECK(f.host.clearedActivities.size() == 1 && f.host.clearedActivities[0] == "discord-on");
}
