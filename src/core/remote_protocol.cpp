#include "remote_protocol.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "json.hpp"

namespace ampify::remote {

namespace {

constexpr double kMinGainDb = -60.0;
constexpr double kMaxGainDb = 12.0;

struct ActionRoute {
    std::string_view path;
    Action action;
};

constexpr ActionRoute kActionRoutes[] = {
    {"/play", Action::Play},
    {"/pause", Action::Pause},
    {"/next", Action::Next},
    {"/previous", Action::Previous},
};

Response Ok(bool done) {
    Json body(Json::Object{});
    body.Set("ok", Json(done));
    if (!done) body.Set("error", Json("Apple Music cannot do this right now"));
    return Response{done ? 200 : 409, body.Dump()};
}

// The "db" of the address, or of the JSON body when the address has none.
std::optional<double> RequestedGainDb(const Request& request) {
    auto found = request.query.find("db");
    if (found != request.query.end()) {
        char* end = nullptr;
        double value = std::strtod(found->second.c_str(), &end);
        if (end && *end == '\0' && !found->second.empty()) return value;
        return std::nullopt;
    }
    if (request.body.empty()) return std::nullopt;
    std::optional<Json> body = Json::Parse(request.body);
    const Json* field = body ? body->Find("db") : nullptr;
    if (field && field->AsNumber()) return *field->AsNumber();
    return std::nullopt;
}

Response HandleVolume(const Request& request, Backend& backend) {
    if (request.method != "POST") return ErrorResponse(405, "POST expected for /volume");
    std::optional<double> db = RequestedGainDb(request);
    if (!db || !std::isfinite(*db)) return ErrorResponse(400, "volume expects db, a number (e.g. /volume?db=-6)");
    std::optional<double> applied = backend.SetGainDb(std::clamp(*db, kMinGainDb, kMaxGainDb));
    if (!applied) return Ok(false);
    Json body(Json::Object{});
    body.Set("ok", Json(true));
    body.Set("gainDb", Json(*applied));
    return Response{200, body.Dump()};
}

}  // namespace

std::string_view ActionName(Action action) {
    for (const ActionRoute& route : kActionRoutes) {
        if (route.action == action) return route.path.substr(1);
    }
    return {};
}

Response Handle(const Request& request, bool peerLoopback, std::string_view token, Backend& backend) {
    if (std::optional<Response> refusal = Authorize(request, peerLoopback, token)) return *refusal;

    const std::string& path = request.path;
    if (path == "/status") {
        if (request.method != "GET") return ErrorResponse(405, "GET expected for /status");
        return Response{200, backend.StatusJson()};
    }
    for (const ActionRoute& route : kActionRoutes) {
        if (path != route.path) continue;
        if (request.method != "POST") return ErrorResponse(405, "POST expected for " + path);
        return Ok(backend.Command(route.action));
    }
    if (path == "/volume") return HandleVolume(request, backend);
    return ErrorResponse(404, "unknown path: " + path);
}

}  // namespace ampify::remote
