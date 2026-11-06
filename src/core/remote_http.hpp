#pragma once

// The HTTP side of the local remote control, without any socket: a small HTTP/1.1 request parser, the response
// bytes, and the security rules. The routes that give a request a meaning are in remote_protocol.hpp.
//
// Rules, all enforced here so that they are tested on Linux:
//   - a peer that is not a loopback address gets 403, whatever it asks;
//   - the Host header must name 127.0.0.1, localhost or [::1] (a web page cannot reach the server through a
//     DNS name that points to 127.0.0.1);
//   - a request from a web page (an Origin header, or the OPTIONS preflight) is refused: no CORS, ever;
//   - every request needs the token, in "X-Ampify-Token: <jeton>" or "Authorization: Bearer <jeton>". A token in the
//     address is refused, because addresses end up in histories and logs.

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace ampify::remote {

inline constexpr size_t kMaxHeaderBytes = 8 * 1024;
inline constexpr size_t kMaxBodyBytes = 4 * 1024;
inline constexpr uint16_t kDefaultPort = 8974;

struct Request {
    std::string method;   // "GET", "POST"...
    std::string target;   // as sent: "/volume?db=-3"
    std::string path;     // "/volume"
    std::map<std::string, std::string> query;    // decoded query parameters
    std::map<std::string, std::string> headers;  // names in lowercase
    std::string body;
};

enum class ParseStatus { Incomplete, Complete, Bad, TooLarge };

// Reads one request from the start of `buffer`. Complete fills `request`. Incomplete asks for more bytes. Bad and
// TooLarge end the connection with 400 or 413.
ParseStatus ParseRequest(std::string_view buffer, Request& request);

struct Response {
    int status = 200;
    std::string body;  // JSON
};

// The bytes of a response: status line, Content-Type JSON, Content-Length, Connection: close, no-store.
std::string Serialize(const Response& response);

// {"ok":false,"error":message}
Response ErrorResponse(int status, const std::string& message);

// The answer to a request that could not be read: 413 for TooLarge, 400 otherwise.
Response UnreadableResponse(ParseStatus status);

// 127.0.0.0/8 and ::1, as text ("127.0.0.1", "::1", "::ffff:127.0.0.1"). Anything else, including 0.0.0.0, is false.
bool IsLoopbackText(std::string_view address);
// The same for an IPv4 address in host byte order.
bool IsLoopbackIpv4(uint32_t address);

// Compares in a time that does not depend on where the first difference is.
bool TokenMatches(std::string_view expected, std::string_view provided);

// A token is 32 to 128 characters among [A-Za-z0-9_-].
bool IsValidToken(std::string_view token);

// A new random token of 40 characters, from std::random_device (the system's random source on Linux and Windows).
std::string NewToken();

// The security rules above, in order. The refusal to send, or nothing when the request may be routed.
// `peerLoopback` comes from the socket, never from a header.
std::optional<Response> Authorize(const Request& request, bool peerLoopback, std::string_view token);

}  // namespace ampify::remote
