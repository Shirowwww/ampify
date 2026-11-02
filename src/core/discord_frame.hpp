#pragma once

// The Discord local IPC protocol, without any I/O: the frames that go through the pipe \\.\pipe\discord-ipc-<n>,
// the handshake, and the SET_ACTIVITY command for Rich Presence. A frame is a 4-byte opcode and a 4-byte length,
// both little-endian, followed by that many bytes of UTF-8 JSON. The Windows side (src/win/discord_ipc.*) only moves
// these bytes. Reference: the discord-rpc client (MIT) and the Discord "RPC" documentation.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "json.hpp"

namespace ampify::discord {

enum class Opcode : uint32_t { Handshake = 0, Frame = 1, Close = 2, Ping = 3, Pong = 4 };

// Discord refuses longer frames; the kit never needs more than a few hundred bytes.
inline constexpr size_t kMaxFrameBytes = 64 * 1024;

struct Frame {
    Opcode opcode = Opcode::Frame;
    std::string json;
};

// The bytes of one frame.
std::string EncodeFrame(Opcode opcode, std::string_view json);

// Cuts frames out of the bytes read from the pipe, in any chunk sizes.
class FrameReader {
public:
    void Feed(std::string_view bytes);

    // The next whole frame, or nullopt when more bytes are needed or the stream is broken (see failed()).
    std::optional<Frame> Next();

    // True after an unknown opcode or a length above kMaxFrameBytes: the connection must be closed.
    bool failed() const { return m_failed; }

private:
    std::string m_buffer;
    bool m_failed = false;
};

// A link under the presence, seen by the others (Discord hides a user's own buttons from them).
struct Button {
    std::string label;
    std::string url;
};

// What an element shows in Discord. Empty strings are left out of the payload. Times are Unix milliseconds,
// 0 for none: with startMs Discord counts up ("02:13 elapsed"), with both it draws a progress bar.
struct Activity {
    int type = 0;                // 0 playing (Discord's default), 2 listening, 3 watching, 5 competing
    int statusDisplay = -1;      // what the member list shows: 0 the application name, 1 state, 2 details; -1 unset
    std::string details;     // first line, such as the title
    std::string state;       // second line, such as the artist
    std::string largeImage;  // an asset key of the Discord application, or an https URL
    std::string largeText;
    std::string smallImage;
    std::string smallText;
    std::string detailsUrl;  // https links opened by a click on the line or the image
    std::string stateUrl;
    std::string largeUrl;
    std::string smallUrl;
    std::vector<Button> buttons;  // at most kMaxButtons
    int64_t startMs = 0;
    int64_t endMs = 0;
};

// Limits of Discord: details and state from 2 to 128 bytes, image texts up to 128, image keys and links up to 256,
// two buttons with a label up to 32 bytes and a link up to 512.
inline constexpr size_t kMaxActivityText = 128;
inline constexpr size_t kMaxImageKey = 256;
inline constexpr size_t kMaxButtons = 2;
inline constexpr size_t kMaxButtonLabel = 32;
inline constexpr size_t kMaxButtonUrl = 512;

// A Discord application id: 17 to 20 digits.
bool IsValidClientId(std::string_view id);

// {"v":1,"client_id":"..."}, sent with Opcode::Handshake right after the pipe opens.
std::string HandshakeJson(std::string_view clientId);

// {"cmd":"SET_ACTIVITY","args":{"pid":...,"activity":{...}},"nonce":"..."}. Without an activity, the presence
// is cleared ("activity": null).
std::string SetActivityJson(uint32_t pid, const std::optional<Activity>& activity, std::string_view nonce);

// Reads the object an element passed to Ampify.discord.setActivity, with the field names of the API
// (type, statusDisplay, details, state, largeImage, largeText, smallImage, smallText, detailsUrl, stateUrl, largeUrl,
// smallUrl, buttons, startMs, endMs). Unknown fields are refused, so a typo does not go unnoticed. Links must be
// https. On error returns nullopt and fills `error` in English.
std::optional<Activity> ActivityFromJson(const Json& value, std::string* error);

// True when a frame read from Discord is the READY event that follows a successful handshake.
bool IsReady(const Frame& frame);

// The message of an ERROR event, or of a Close frame, or nullopt when the frame is not an error.
std::optional<std::string> ErrorOf(const Frame& frame);

}  // namespace ampify::discord
