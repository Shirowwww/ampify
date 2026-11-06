#include "remote_http.hpp"

#include <algorithm>
#include <cctype>
#include <random>

#include "json.hpp"
#include "text_util.hpp"

namespace ampify::remote {

namespace {

constexpr size_t kMaxTargetBytes = 1024;

constexpr size_t DecimalDigits(size_t number) {
    size_t digits = 1;
    for (; number >= 10; number /= 10) digits++;
    return digits;
}

// A Content-Length longer than this is not a length: 400. A shorter one above kMaxBodyBytes is 413.
constexpr size_t kMaxLengthDigits = DecimalDigits(kMaxBodyBytes) + 2;

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

// "127.0.0.1:8974" -> "127.0.0.1", "[::1]:8974" -> "[::1]".
std::string HostWithoutPort(std::string_view value) {
    value = TrimSpace(value);
    if (!value.empty() && value.front() == '[') {
        size_t close = value.find(']');
        if (close == std::string_view::npos) return {};
        return ToLowerAscii(value.substr(0, close + 1));
    }
    size_t colon = value.find(':');
    return ToLowerAscii(value.substr(0, colon));
}

// The query parameters of `query` (the text after '?'), decoded into `out`.
bool ParseQuery(std::string_view query, Request& out) {
    while (!query.empty()) {
        size_t amp = query.find('&');
        std::string_view pair = query.substr(0, amp);
        query = amp == std::string_view::npos ? std::string_view() : query.substr(amp + 1);
        if (pair.empty()) continue;
        size_t eq = pair.find('=');
        std::optional<std::string> key = PercentDecode(pair.substr(0, eq));
        std::optional<std::string> value =
            PercentDecode(eq == std::string_view::npos ? std::string_view() : pair.substr(eq + 1));
        if (!key || !value) return false;
        out.query[*key] = *value;
    }
    return true;
}

// METHOD SP target SP HTTP/1.x, then the path and the decoded query into `out`.
bool ParseRequestLine(std::string_view requestLine, Request& out) {
    size_t firstSpace = requestLine.find(' ');
    size_t secondSpace = firstSpace == std::string_view::npos ? firstSpace : requestLine.find(' ', firstSpace + 1);
    if (secondSpace == std::string_view::npos) return false;
    std::string_view method = requestLine.substr(0, firstSpace);
    std::string_view target = requestLine.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    std::string_view version = requestLine.substr(secondSpace + 1);
    if (method.empty() || method.size() > 10) return false;
    for (char c : method) {
        if (c < 'A' || c > 'Z') return false;
    }
    if (version != "HTTP/1.1" && version != "HTTP/1.0") return false;
    if (target.empty() || target.front() != '/' || target.size() > kMaxTargetBytes) return false;
    for (char c : target) {
        unsigned char u = static_cast<unsigned char>(c);
        if (u <= 0x20 || u >= 0x7F) return false;
    }
    out.method = std::string(method);
    out.target = std::string(target);
    size_t question = target.find('?');
    out.path = std::string(target.substr(0, question));
    return question == std::string_view::npos || ParseQuery(target.substr(question + 1), out);
}

std::optional<size_t> ParseContentLength(std::string_view value) {
    if (value.empty() || value.size() > kMaxLengthDigits) return std::nullopt;
    size_t length = 0;
    for (char c : value) {
        if (c < '0' || c > '9') return std::nullopt;
        length = length * 10 + static_cast<size_t>(c - '0');
    }
    return length;
}

// Header fields into `out.headers`. Folded lines and chunked bodies are refused: the server has no use for them,
// and accepting them is how request smuggling starts. `length` is the declared body size.
bool ParseHeaders(std::string_view rest, Request& out, size_t& length) {
    bool sawLength = false;
    while (!rest.empty()) {
        size_t end = rest.find("\r\n");
        std::string_view line = rest.substr(0, end);
        rest = end == std::string_view::npos ? std::string_view() : rest.substr(end + 2);
        if (line.empty() || line.front() == ' ' || line.front() == '\t') return false;
        size_t colon = line.find(':');
        if (colon == std::string_view::npos || colon == 0) return false;
        std::string_view name = line.substr(0, colon);
        for (char c : name) {
            if (!IsTokenChar(c)) return false;
        }
        // Checked before trimming: TrimSpace also drops CR and LF, which must refuse the request here.
        std::string_view raw = line.substr(colon + 1);
        for (char c : raw) {
            unsigned char u = static_cast<unsigned char>(c);
            if ((u < 0x20 && c != '\t') || u == 0x7F) return false;
        }
        std::string_view value = TrimSpace(raw);
        std::string key = ToLowerAscii(name);
        if (key == "transfer-encoding") return false;
        if (key == "content-length") {
            std::optional<size_t> parsed = sawLength ? std::nullopt : ParseContentLength(value);
            if (!parsed) return false;
            length = *parsed;
            sawLength = true;
        }
        if (out.headers.count(key) && key == "host") return false;
        out.headers[key] = std::string(value);
    }
    return true;
}

const std::string* Header(const Request& request, const char* name) {
    auto found = request.headers.find(name);
    return found == request.headers.end() ? nullptr : &found->second;
}

// A browser adds Origin to cross-site requests and sends OPTIONS before a cross-origin one. The remote control is
// for local tools, not for web pages.
bool IsBrowserRequest(const Request& request) { return Header(request, "origin") || request.method == "OPTIONS"; }

bool IsLoopbackHost(const Request& request) {
    const std::string* host = Header(request, "host");
    std::string name = host ? HostWithoutPort(*host) : std::string();
    return name == "127.0.0.1" || name == "localhost" || name == "[::1]";
}

std::string ProvidedToken(const Request& request) {
    if (const std::string* value = Header(request, "x-ampify-token")) return *value;
    const std::string* auth = Header(request, "authorization");
    constexpr std::string_view kBearer = "bearer ";
    if (auth && ToLowerAscii(std::string_view(*auth).substr(0, kBearer.size())) == kBearer) {
        return auth->substr(kBearer.size());
    }
    return {};
}

}  // namespace

ParseStatus ParseRequest(std::string_view buffer, Request& request) {
    size_t headerEnd = buffer.find("\r\n\r\n");
    if (headerEnd == std::string_view::npos) {
        return buffer.size() > kMaxHeaderBytes ? ParseStatus::TooLarge : ParseStatus::Incomplete;
    }
    if (headerEnd + 4 > kMaxHeaderBytes) return ParseStatus::TooLarge;

    Request out;
    std::string_view head = buffer.substr(0, headerEnd);
    size_t lineEnd = head.find("\r\n");
    std::string_view rest = lineEnd == std::string_view::npos ? std::string_view() : head.substr(lineEnd + 2);
    size_t length = 0;
    if (!ParseRequestLine(head.substr(0, lineEnd), out) || !ParseHeaders(rest, out, length)) return ParseStatus::Bad;
    if (length > kMaxBodyBytes) return ParseStatus::TooLarge;
    size_t bodyStart = headerEnd + 4;
    if (buffer.size() < bodyStart + length) return ParseStatus::Incomplete;
    out.body = std::string(buffer.substr(bodyStart, length));
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

Response ErrorResponse(int status, const std::string& message) {
    Json body(Json::Object{});
    body.Set("ok", Json(false));
    body.Set("error", Json(message));
    return Response{status, body.Dump()};
}

Response UnreadableResponse(ParseStatus status) {
    if (status == ParseStatus::TooLarge) return Response{413, R"({"ok":false,"error":"request too large"})"};
    return Response{400, R"({"ok":false,"error":"unreadable HTTP request"})"};
}

bool IsLoopbackIpv4(uint32_t address) { return (address >> 24) == 127; }

bool IsLoopbackText(std::string_view address) {
    std::string text = ToLowerAscii(TrimSpace(address));
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

std::optional<Response> Authorize(const Request& request, bool peerLoopback, std::string_view token) {
    if (!peerLoopback) return ErrorResponse(403, "only local connections (127.0.0.1) are accepted");
    if (!IsLoopbackHost(request)) return ErrorResponse(403, "Host header refused: use http://127.0.0.1:<port>");
    if (IsBrowserRequest(request)) return ErrorResponse(403, "request from a web page refused");
    if (request.query.count("token")) {
        return ErrorResponse(400, "the token goes in the X-Ampify-Token header, not in the address");
    }
    if (!IsValidToken(token)) return ErrorResponse(503, "no token is set (setting remote.token)");
    if (!TokenMatches(token, ProvidedToken(request))) return ErrorResponse(401, "token missing or wrong");
    return std::nullopt;
}

}  // namespace ampify::remote
