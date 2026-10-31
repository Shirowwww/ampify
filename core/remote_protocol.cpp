#include "remote_protocol.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <random>

#include "json.hpp"

namespace ampify::remote {

namespace {

constexpr size_t kMaxTargetBytes = 1024;

std::string Lower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
    return out;
}

std::string_view Trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.remove_suffix(1);
    return text;
}

bool IsTokenChar(char c) {
    unsigned char u = static_cast<unsigned char>(c);
    if (std::isalnum(u)) return true;
    return std::string_view("!#$%&'*+-.^_`|~").find(c) != std::string_view::npos;
}

int HexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::optional<std::string> PercentDecode(std::string_view text) {
    std::string out;
    for (size_t i = 0; i < text.size(); i++) {
        char c = text[i];
        if (c == '+') {
            out.push_back(' ');
        } else if (c == '%') {
            if (i + 2 >= text.size()) return std::nullopt;
            int high = HexValue(text[i + 1]);
            int low = HexValue(text[i + 2]);
            if (high < 0 || low < 0) return std::nullopt;
            out.push_back(static_cast<char>(high * 16 + low));
            i += 2;
        } else {
            out.push_back(c);
        }
    }
    return out;
}

const char* Reason(int status) {
    switch (status) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 413: return "Payload Too Large";
        case 503: return "Service Unavailable";
        default: return "Error";
    }
}

Response Error(int status, const std::string& message) {
    Json body(Json::Object{});
    body.Set("ok", Json(false));
    body.Set("error", Json(message));
    return Response{status, body.Dump()};
}

Response Ok(bool done) {
    Json body(Json::Object{});
    body.Set("ok", Json(done));
    if (!done) body.Set("error", Json("Apple Music cannot do this right now"));
    return Response{done ? 200 : 409, body.Dump()};
}

// "127.0.0.1:8974" -> "127.0.0.1", "[::1]:8974" -> "[::1]".
std::string HostWithoutPort(std::string_view value) {
    value = Trim(value);
    if (!value.empty() && value.front() == '[') {
        size_t close = value.find(']');
        if (close == std::string_view::npos) return {};
        return Lower(value.substr(0, close + 1));
    }
    size_t colon = value.find(':');
    return Lower(value.substr(0, colon));
}

}  // namespace

ParseStatus ParseRequest(std::string_view buffer, Request& request, size_t* consumed) {
    size_t headerEnd = buffer.find("\r\n\r\n");
    if (headerEnd == std::string_view::npos) {
        return buffer.size() > kMaxHeaderBytes ? ParseStatus::TooLarge : ParseStatus::Incomplete;
    }
    if (headerEnd + 4 > kMaxHeaderBytes) return ParseStatus::TooLarge;

    Request out;
    std::string_view head = buffer.substr(0, headerEnd);
    size_t lineEnd = head.find("\r\n");
    std::string_view requestLine = head.substr(0, lineEnd);
    std::string_view rest = lineEnd == std::string_view::npos ? std::string_view() : head.substr(lineEnd + 2);

    // METHOD SP target SP HTTP/1.x
    size_t firstSpace = requestLine.find(' ');
    size_t secondSpace = firstSpace == std::string_view::npos ? firstSpace : requestLine.find(' ', firstSpace + 1);
    if (secondSpace == std::string_view::npos) return ParseStatus::Bad;
    std::string_view method = requestLine.substr(0, firstSpace);
    std::string_view target = requestLine.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    std::string_view version = requestLine.substr(secondSpace + 1);
    if (method.empty() || method.size() > 10) return ParseStatus::Bad;
    for (char c : method) {
        if (c < 'A' || c > 'Z') return ParseStatus::Bad;
    }
    if (version != "HTTP/1.1" && version != "HTTP/1.0") return ParseStatus::Bad;
    if (target.empty() || target.front() != '/' || target.size() > kMaxTargetBytes) return ParseStatus::Bad;
    for (char c : target) {
        unsigned char u = static_cast<unsigned char>(c);
        if (u <= 0x20 || u >= 0x7F) return ParseStatus::Bad;
    }
    out.method = std::string(method);
    out.target = std::string(target);
    size_t question = target.find('?');
    out.path = std::string(target.substr(0, question));
    if (question != std::string_view::npos) {
        std::string_view query = target.substr(question + 1);
        while (!query.empty()) {
            size_t amp = query.find('&');
            std::string_view pair = query.substr(0, amp);
            query = amp == std::string_view::npos ? std::string_view() : query.substr(amp + 1);
            if (pair.empty()) continue;
            size_t eq = pair.find('=');
            std::optional<std::string> key = PercentDecode(pair.substr(0, eq));
            std::optional<std::string> value =
                PercentDecode(eq == std::string_view::npos ? std::string_view() : pair.substr(eq + 1));
            if (!key || !value) return ParseStatus::Bad;
            out.query[*key] = *value;
        }
    }

    // Header fields. Folded lines and chunked bodies are refused: the server has no use for them, and accepting
    // them is how request smuggling starts.
    bool sawLength = false;
    size_t length = 0;
    while (!rest.empty()) {
        size_t end = rest.find("\r\n");
        std::string_view line = rest.substr(0, end);
        rest = end == std::string_view::npos ? std::string_view() : rest.substr(end + 2);
        if (line.empty() || line.front() == ' ' || line.front() == '\t') return ParseStatus::Bad;
        size_t colon = line.find(':');
        if (colon == std::string_view::npos || colon == 0) return ParseStatus::Bad;
        std::string_view name = line.substr(0, colon);
        for (char c : name) {
            if (!IsTokenChar(c)) return ParseStatus::Bad;
        }
        std::string_view value = Trim(line.substr(colon + 1));
        for (char c : value) {
            unsigned char u = static_cast<unsigned char>(c);
            if ((u < 0x20 && c != '\t') || u == 0x7F) return ParseStatus::Bad;
        }
        std::string key = Lower(name);
        if (key == "transfer-encoding") return ParseStatus::Bad;
        if (key == "content-length") {
            if (sawLength || value.empty() || value.size() > 6) return ParseStatus::Bad;
            for (char c : value) {
                if (c < '0' || c > '9') return ParseStatus::Bad;
            }
            length = static_cast<size_t>(std::strtoul(std::string(value).c_str(), nullptr, 10));
            sawLength = true;
        }
        if (out.headers.count(key) && key == "host") return ParseStatus::Bad;
        out.headers[key] = std::string(value);
    }
    if (length > kMaxBodyBytes) return ParseStatus::TooLarge;
    size_t bodyStart = headerEnd + 4;
    if (buffer.size() < bodyStart + length) return ParseStatus::Incomplete;
    out.body = std::string(buffer.substr(bodyStart, length));
    if (consumed) *consumed = bodyStart + length;
    request = std::move(out);
    return ParseStatus::Complete;
}

std::string Serialize(const Response& response) {
    std::string out = "HTTP/1.1 " + std::to_string(response.status) + " " + Reason(response.status) + "\r\n";
    out += "Content-Type: application/json; charset=utf-8\r\n";
    out += "Content-Length: " + std::to_string(response.body.size()) + "\r\n";
    out += "Cache-Control: no-store\r\n";
    out += "X-Content-Type-Options: nosniff\r\n";
    out += "Connection: close\r\n\r\n";
    out += response.body;
    return out;
}

bool IsLoopbackIpv4(uint32_t address) { return (address >> 24) == 127; }

bool IsLoopbackText(std::string_view address) {
    std::string text = Lower(Trim(address));
    if (text == "::1" || text == "[::1]") return true;
    constexpr std::string_view kMapped = "::ffff:";
    std::string_view v4 = text;
    if (v4.substr(0, kMapped.size()) == kMapped) v4.remove_prefix(kMapped.size());
    // Dotted quad, four numbers from 0 to 255, the first one 127.
    int parts = 0;
    unsigned first = 0;
    while (true) {
        size_t dot = v4.find('.');
        std::string_view part = v4.substr(0, dot);
        if (part.empty() || part.size() > 3) return false;
        unsigned value = 0;
        for (char c : part) {
            if (c < '0' || c > '9') return false;
            value = value * 10 + static_cast<unsigned>(c - '0');
        }
        if (value > 255) return false;
        if (parts == 0) first = value;
        parts++;
        if (dot == std::string_view::npos) break;
        v4.remove_prefix(dot + 1);
    }
    return parts == 4 && first == 127;
}

bool TokenMatches(std::string_view expected, std::string_view provided) {
    // The length is not secret (it is fixed by IsValidToken), but the content is compared without an early exit.
    unsigned char difference = expected.size() == provided.size() ? 0 : 1;
    size_t n = std::max(expected.size(), provided.size());
    for (size_t i = 0; i < n; i++) {
        unsigned char a = i < expected.size() ? static_cast<unsigned char>(expected[i]) : 0;
        unsigned char b = i < provided.size() ? static_cast<unsigned char>(provided[i]) : 0;
        difference |= static_cast<unsigned char>(a ^ b);
    }
    return difference == 0 && !expected.empty();
}

bool IsValidToken(std::string_view token) {
    if (token.size() < 32 || token.size() > 128) return false;
    for (char c : token) {
        unsigned char u = static_cast<unsigned char>(c);
        if (!std::isalnum(u) && c != '-' && c != '_') return false;
    }
    return true;
}

std::string NewToken() {
    static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";  // 64 symbols
    std::random_device source;
    std::string token;
    while (token.size() < 40) {
        uint32_t bits = source();
        for (int i = 0; i < 5 && token.size() < 40; i++, bits >>= 6) token.push_back(kAlphabet[bits & 63]);
    }
    return token;
}

Response Router::Handle(const Request& request, bool peerLoopback, Backend& backend) const {
    if (!peerLoopback) return Error(403, "only local connections (127.0.0.1) are accepted");

    auto header = [&](const char* name) -> const std::string* {
        auto found = request.headers.find(name);
        return found == request.headers.end() ? nullptr : &found->second;
    };

    const std::string* host = header("host");
    std::string hostName = host ? HostWithoutPort(*host) : std::string();
    if (hostName != "127.0.0.1" && hostName != "localhost" && hostName != "[::1]") {
        return Error(403, "Host header refused: use http://127.0.0.1:<port>");
    }
    // A browser adds Origin to cross-site requests. The remote control is for local tools, not for web pages.
    if (header("origin")) return Error(403, "request from a web page refused");
    if (request.method == "OPTIONS") return Error(403, "request from a web page refused");

    if (request.query.count("token")) return Error(400, "the token goes in the X-Ampify-Token header, not in the address");
    if (!IsValidToken(m_token)) return Error(503, "no token is set (setting remote.token)");
    std::string provided;
    if (const std::string* value = header("x-ampify-token")) {
        provided = *value;
    } else if (const std::string* auth = header("authorization")) {
        constexpr std::string_view kBearer = "bearer ";
        if (Lower(std::string_view(*auth).substr(0, kBearer.size())) == kBearer) provided = auth->substr(kBearer.size());
    }
    if (!TokenMatches(m_token, provided)) return Error(401, "token missing or wrong");

    const std::string& path = request.path;
    if (path == "/status") {
        if (request.method != "GET") return Error(405, "GET expected for /status");
        return Response{200, backend.StatusJson()};
    }
    if (path == "/play" || path == "/pause" || path == "/next" || path == "/previous") {
        if (request.method != "POST") return Error(405, "POST expected for " + path);
        return Ok(backend.Command(std::string_view(path).substr(1)));
    }
    if (path == "/volume") {
        if (request.method != "POST") return Error(405, "POST expected for /volume");
        std::optional<double> db;
        auto found = request.query.find("db");
        if (found != request.query.end()) {
            char* end = nullptr;
            double value = std::strtod(found->second.c_str(), &end);
            if (end && *end == '\0' && !found->second.empty()) db = value;
        } else if (!request.body.empty()) {
            std::optional<Json> body = Json::Parse(request.body);
            const Json* field = body ? body->Find("db") : nullptr;
            if (field && field->AsNumber()) db = *field->AsNumber();
        }
        if (!db || !std::isfinite(*db)) return Error(400, "volume expects db, a number (e.g. /volume?db=-6)");
        std::optional<double> applied = backend.SetGainDb(std::clamp(*db, -60.0, 12.0));
        if (!applied) return Ok(false);
        Json body(Json::Object{});
        body.Set("ok", Json(true));
        body.Set("gainDb", Json(*applied));
        return Response{200, body.Dump()};
    }
    return Error(404, "unknown path: " + path);
}

}  // namespace ampify::remote
