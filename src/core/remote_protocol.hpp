#pragma once

// The routes of the local remote control: what a request that passed remote_http's security rules asks of the kit.
// The Windows server (src/win/remote_server.*) listens on 127.0.0.1 only and hands each request here.
//
// Routes: GET /status, POST /play, /pause, /next, /previous, and POST /volume?db=<n> (or the body {"db": n}). The
// volume is the kit's internal gain on the decoded samples, bounded by audio.maxGainDb; the slider of Apple Music
// is never moved.

#include <optional>
#include <string>
#include <string_view>

#include "remote_http.hpp"

namespace ampify::remote {

// The playback commands a client can send. The backend never receives a name it has to guess at.
enum class Action { Play, Pause, Next, Previous };

// "play", "pause", "next" or "previous": the route name.
std::string_view ActionName(Action action);

// What the routes ask of the kit. The Windows server calls it from its own thread, so it must be thread-safe.
class Backend {
public:
    virtual ~Backend() = default;
    // The JSON object of GET /status.
    virtual std::string StatusJson() = 0;
    // False when the app cannot do it now.
    virtual bool Command(Action action) = 0;
    // The internal gain in dB, from -60 to +12 before the kit's own ceiling. Returns the gain applied.
    virtual std::optional<double> SetGainDb(double db) = 0;
};

// The answer to one request: refused by Authorize, or routed to the backend. `peerLoopback` comes from the socket.
Response Handle(const Request& request, bool peerLoopback, std::string_view token, Backend& backend);

}  // namespace ampify::remote
