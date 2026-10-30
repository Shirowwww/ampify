#include "discord_frame.hpp"

#include <cmath>

namespace amk::discord {

namespace {

void PutLittleEndian(std::string& out, uint32_t value) {
    for (int i = 0; i < 4; i++) out.push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
}

uint32_t GetLittleEndian(std::string_view bytes) {
    uint32_t value = 0;
    for (int i = 3; i >= 0; i--) value = (value << 8) | static_cast<unsigned char>(bytes[static_cast<size_t>(i)]);
    return value;
}

bool Fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

}  // namespace

std::string EncodeFrame(Opcode opcode, std::string_view json) {
    std::string out;
    out.reserve(8 + json.size());
    PutLittleEndian(out, static_cast<uint32_t>(opcode));
    PutLittleEndian(out, static_cast<uint32_t>(json.size()));
    out.append(json);
    return out;
}

void FrameReader::Feed(std::string_view bytes) {
    if (!m_failed) m_buffer.append(bytes);
}

std::optional<Frame> FrameReader::Next() {
    if (m_failed || m_buffer.size() < 8) return std::nullopt;
    uint32_t opcode = GetLittleEndian(std::string_view(m_buffer).substr(0, 4));
    uint32_t length = GetLittleEndian(std::string_view(m_buffer).substr(4, 4));
    if (opcode > static_cast<uint32_t>(Opcode::Pong) || length > kMaxFrameBytes) {
        m_failed = true;
        m_buffer.clear();
        return std::nullopt;
    }
    if (m_buffer.size() < 8 + static_cast<size_t>(length)) return std::nullopt;
    Frame frame{static_cast<Opcode>(opcode), m_buffer.substr(8, length)};
    m_buffer.erase(0, 8 + static_cast<size_t>(length));
    return frame;
}

bool IsValidClientId(std::string_view id) {
    if (id.size() < 17 || id.size() > 20) return false;
    for (char c : id) {
        if (c < '0' || c > '9') return false;
    }
    return true;
}

std::string HandshakeJson(std::string_view clientId) {
    Json root(Json::Object{});
    root.Set("v", Json(1.0));
    root.Set("client_id", Json(std::string(clientId)));
    return root.Dump();
}

std::string SetActivityJson(uint32_t pid, const std::optional<Activity>& activity, std::string_view nonce) {
    Json args(Json::Object{});
    args.Set("pid", Json(static_cast<double>(pid)));
    if (activity) {
        Json body(Json::Object{});
        if (!activity->details.empty()) body.Set("details", Json(activity->details));
        if (!activity->state.empty()) body.Set("state", Json(activity->state));
        if (activity->startMs > 0 || activity->endMs > 0) {
            // Discord reads these timestamps in seconds.
            Json times(Json::Object{});
            if (activity->startMs > 0) times.Set("start", Json(static_cast<double>(activity->startMs / 1000)));
            if (activity->endMs > 0) times.Set("end", Json(static_cast<double>(activity->endMs / 1000)));
            body.Set("timestamps", std::move(times));
        }
        Json assets(Json::Object{});
        if (!activity->largeImage.empty()) assets.Set("large_image", Json(activity->largeImage));
        if (!activity->largeText.empty()) assets.Set("large_text", Json(activity->largeText));
        if (!activity->smallImage.empty()) assets.Set("small_image", Json(activity->smallImage));
        if (!activity->smallText.empty()) assets.Set("small_text", Json(activity->smallText));
        if (!assets.AsObject()->empty()) body.Set("assets", std::move(assets));
        body.Set("instance", Json(false));
        args.Set("activity", std::move(body));
    } else {
        args.Set("activity", Json());
    }
    Json root(Json::Object{});
    root.Set("cmd", Json("SET_ACTIVITY"));
    root.Set("args", std::move(args));
    root.Set("nonce", Json(std::string(nonce)));
    return root.Dump();
}

std::optional<Activity> ActivityFromJson(const Json& value, std::string* error) {
    const Json::Object* fields = value.AsObject();
    if (!fields) {
        Fail(error, "setActivity attend un objet");
        return std::nullopt;
    }
    Activity activity;
    for (const auto& [key, field] : *fields) {
        std::string* text = nullptr;
        size_t limit = kMaxActivityText;
        if (key == "details") text = &activity.details;
        else if (key == "state") text = &activity.state;
        else if (key == "largeImage") text = &activity.largeImage, limit = kMaxImageKey;
        else if (key == "largeText") text = &activity.largeText;
        else if (key == "smallImage") text = &activity.smallImage, limit = kMaxImageKey;
        else if (key == "smallText") text = &activity.smallText;

        if (text) {
            if (field.IsNull()) continue;
            const std::string* s = field.AsString();
            if (!s) {
                Fail(error, key + " doit être un texte");
                return std::nullopt;
            }
            // Discord counts bytes. A longer text is cut on a character boundary rather than refused.
            std::string cut = *s;
            if (cut.size() > limit) {
                size_t end = limit;
                while (end > 0 && (static_cast<unsigned char>(cut[end]) & 0xC0) == 0x80) end--;
                cut.resize(end);
            }
            *text = std::move(cut);
            continue;
        }
        if (key == "startMs" || key == "endMs") {
            if (field.IsNull()) continue;
            const double* n = field.AsNumber();
            if (!n || !std::isfinite(*n) || *n < 0 || *n > 9.0e15) {
                Fail(error, key + " doit être un nombre de millisecondes positif");
                return std::nullopt;
            }
            (key == "startMs" ? activity.startMs : activity.endMs) = static_cast<int64_t>(*n);
            continue;
        }
        if (key == "clientId") continue;  // read by the caller
        Fail(error, "champ inconnu dans setActivity : " + key);
        return std::nullopt;
    }
    // Discord ignores a details or state of a single byte: pad it so that the line still shows.
    for (std::string* line : {&activity.details, &activity.state}) {
        if (line->size() == 1) line->push_back(' ');
    }
    if (activity.details.empty() && activity.state.empty()) {
        Fail(error, "setActivity attend au moins details ou state");
        return std::nullopt;
    }
    return activity;
}

bool IsReady(const Frame& frame) {
    if (frame.opcode != Opcode::Frame) return false;
    std::optional<Json> root = Json::Parse(frame.json);
    if (!root) return false;
    const Json* cmd = root->Find("cmd");
    const Json* evt = root->Find("evt");
    return cmd && evt && cmd->AsString() && evt->AsString() && *cmd->AsString() == "DISPATCH" &&
           *evt->AsString() == "READY";
}

std::optional<std::string> ErrorOf(const Frame& frame) {
    std::optional<Json> root = Json::Parse(frame.json);
    auto messageOf = [](const Json* holder) -> std::string {
        if (!holder) return "erreur sans message";
        const Json* message = holder->Find("message");
        return message && message->AsString() ? *message->AsString() : "erreur sans message";
    };
    if (frame.opcode == Opcode::Close) return root ? messageOf(&*root) : std::string("connexion fermée par Discord");
    if (!root) return std::nullopt;
    const Json* evt = root->Find("evt");
    if (evt && evt->AsString() && *evt->AsString() == "ERROR") return messageOf(root->Find("data"));
    return std::nullopt;
}

}  // namespace amk::discord
