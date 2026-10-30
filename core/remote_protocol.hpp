#pragma once

// The local remote control, without any socket: a small HTTP/1.1 request parser and the router that answers it.
// The Windows server (win/remote_server.*) listens on 127.0.0.1 only and hands each request here.
//
// Rules, all enforced here so that they are tested on Linux:
//   - a peer that is not a loopback address gets 403, whatever it asks;
//   - the Host header must name 127.0.0.1, localhost or [::1] (a web page cannot reach the server through a
//     DNS name that points to 127.0.0.1);
//   - every request needs the token, in "X-AMK-Token: <jeton>" or "Authorization: Bearer <jeton>". A token in the
//     address is refused, because addresses end up in histories and logs;
//   - no CORS: a browser page cannot add the token header without a preflight, and the preflight is refused.
//
// Routes: GET /status, POST /play, /pause, /next, /previous, and POST /volume?db=<n> (or the body {"db": n}). The
// volume is the kit's internal gain on the decoded samples, bounded by audio.maxGainDb; the slider of Apple Music
// is never moved.

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace amk::remote {

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

// Reads one request from the start of `buffer`. Complete fills `request` and `consumed` (bytes used). Incomplete
// asks for more bytes. Bad and TooLarge end the connection with 400 or 413.
ParseStatus ParseRequest(std::string_view buffer, Request& request, size_t* consumed = nullptr);

struct Response {
    int status = 200;
    std::string body;  // JSON
};

// The bytes of a response: status line, Content-Type JSON, Content-Length, Connection: close, no-store.
std::string Serialize(const Response& response);

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

// What the router asks of the kit. The Windows server calls it from its own thread, so it must be thread-safe.
class Backend {
public:
    virtual ~Backend() = default;
    // The JSON object of GET /status.
    virtual std::string StatusJson() = 0;
    // "play", "pause", "next" or "previous". False when the app cannot do it now.
    virtual bool Command(std::string_view action) = 0;
    // The internal gain in dB, from -60 to +12 before the kit's own ceiling. Returns the gain applied.
    virtual std::optional<double> SetGainDb(double db) = 0;
};

class Router {
public:
    explicit Router(std::string token) : m_token(std::move(token)) {}

    // The answer to one request. `peerLoopback` comes from the socket, never from a header.
    Response Handle(const Request& request, bool peerLoopback, Backend& backend) const;

private:
    std::string m_token;
};

}  // namespace amk::remote
