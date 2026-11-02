#include "discord_frame.hpp"

#include <cmath>

namespace ampify::discord {

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

// Discord counts bytes. A longer text is cut on a character boundary rather than refused.
std::string CutToBytes(std::string text, size_t limit) {
    if (text.size() > limit) {
        size_t end = limit;
        while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) end--;
        text.resize(end);
    }
    return text;
}

bool ReadText(const std::string& key, const Json& field, size_t limit, std::string& out, std::string* error) {
    if (field.IsNull()) return true;
    const std::string* s = field.AsString();
    if (!s) return Fail(error, key + " must be a string");
    out = CutToBytes(*s, limit);
    return true;
}

// A link is not cut: half a link opens the wrong page.
bool ReadLink(const std::string& key, const Json& field, size_t limit, std::string& out, std::string* error) {
    if (field.IsNull()) return true;
    const std::string* s = field.AsString();
    if (!s || s->rfind("https://", 0) != 0 || s->size() > limit || s->size() <= 8) {
        return Fail(error, key + " must be an https link of " + std::to_string(limit) + " bytes at most");
    }
    out = *s;
    return true;
}

// `names[i]` is the word for the value `values[i]`.
template <size_t N>
bool ReadChoice(const std::string& key, const Json& field, const char* const (&names)[N], const int (&values)[N],
                int& out, std::string* error) {
    if (field.IsNull()) return true;
    const std::string* s = field.AsString();
    for (size_t i = 0; s && i < N; i++) {
        if (*s == names[i]) {
            out = values[i];
            return true;
        }
    }
    std::string list;
    for (size_t i = 0; i < N; i++) list += (i ? ", " : "") + std::string(names[i]);
    return Fail(error, key + " must be one of: " + list);
}

bool ReadButtons(const Json& field, std::vector<Button>& out, std::string* error) {
    if (field.IsNull()) return true;
    const Json::Array* items = field.AsArray();
    if (!items || items->size() > kMaxButtons) {
        return Fail(error, "buttons must be a list of " + std::to_string(kMaxButtons) + " buttons at most");
    }
    for (const Json& item : *items) {
        const Json::Object* fields = item.AsObject();
        if (!fields) return Fail(error, "each button must be an object { label, url }");
        Button button;
        for (const auto& [key, value] : *fields) {
            bool ok = key == "label"  ? ReadText("button label", value, kMaxButtonLabel, button.label, error)
                      : key == "url" ? ReadLink("button url", value, kMaxButtonUrl, button.url, error)
                                      : Fail(error, "unknown field in a button: " + key);
            if (!ok) return false;
        }
        if (button.label.empty() || button.url.empty()) return Fail(error, "each button needs a label and a url");
        out.push_back(std::move(button));
    }
    return true;
}

void SetIfAny(Json& object, const char* key, const std::string& value) {
    if (!value.empty()) object.Set(key, Json(value));
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
        if (activity->type != 0) body.Set("type", Json(static_cast<double>(activity->type)));
        if (activity->statusDisplay >= 0) {
            body.Set("status_display_type", Json(static_cast<double>(activity->statusDisplay)));
        }
        SetIfAny(body, "details", activity->details);
        SetIfAny(body, "details_url", activity->detailsUrl);
        SetIfAny(body, "state", activity->state);
        SetIfAny(body, "state_url", activity->stateUrl);
        if (activity->startMs > 0 || activity->endMs > 0) {
            // Discord reads these timestamps in seconds.
            Json times(Json::Object{});
            if (activity->startMs > 0) times.Set("start", Json(static_cast<double>(activity->startMs / 1000)));
            if (activity->endMs > 0) times.Set("end", Json(static_cast<double>(activity->endMs / 1000)));
            body.Set("timestamps", std::move(times));
        }
        Json assets(Json::Object{});
        SetIfAny(assets, "large_image", activity->largeImage);
        SetIfAny(assets, "large_text", activity->largeText);
        SetIfAny(assets, "large_url", activity->largeUrl);
        SetIfAny(assets, "small_image", activity->smallImage);
        SetIfAny(assets, "small_text", activity->smallText);
        SetIfAny(assets, "small_url", activity->smallUrl);
        if (!assets.AsObject()->empty()) body.Set("assets", std::move(assets));
        if (!activity->buttons.empty()) {
            Json::Array buttons;
            for (const Button& button : activity->buttons) {
                Json item(Json::Object{});
                item.Set("label", Json(button.label));
                item.Set("url", Json(button.url));
                buttons.push_back(std::move(item));
            }
            body.Set("buttons", Json(std::move(buttons)));
        }
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

namespace {

bool ReadTimestamp(const std::string& key, const Json& field, int64_t& out, std::string* error) {
    if (field.IsNull()) return true;
    const double* n = field.AsNumber();
    if (!n || !std::isfinite(*n) || *n < 0 || *n > 9.0e15) {
        Fail(error, key + " must be a positive number of milliseconds");
        return false;
    }
    out = static_cast<int64_t>(*n);
    return true;
}

// Reads one field of setActivity into `activity`. False, with `error` filled, on a bad value or an unknown key.
bool ReadActivityField(const std::string& key, const Json& field, Activity& activity, std::string* error) {
    static const char* const kTypeNames[] = {"playing", "listening", "watching", "competing"};
    static const int kTypeValues[] = {0, 2, 3, 5};
    static const char* const kDisplayNames[] = {"name", "state", "details"};
    static const int kDisplayValues[] = {0, 1, 2};
    if (key == "details") return ReadText(key, field, kMaxActivityText, activity.details, error);
    if (key == "state") return ReadText(key, field, kMaxActivityText, activity.state, error);
    if (key == "largeImage") return ReadText(key, field, kMaxImageKey, activity.largeImage, error);
    if (key == "largeText") return ReadText(key, field, kMaxActivityText, activity.largeText, error);
    if (key == "smallImage") return ReadText(key, field, kMaxImageKey, activity.smallImage, error);
    if (key == "smallText") return ReadText(key, field, kMaxActivityText, activity.smallText, error);
    if (key == "detailsUrl") return ReadLink(key, field, kMaxImageKey, activity.detailsUrl, error);
    if (key == "stateUrl") return ReadLink(key, field, kMaxImageKey, activity.stateUrl, error);
    if (key == "largeUrl") return ReadLink(key, field, kMaxImageKey, activity.largeUrl, error);
    if (key == "smallUrl") return ReadLink(key, field, kMaxImageKey, activity.smallUrl, error);
    if (key == "type") return ReadChoice(key, field, kTypeNames, kTypeValues, activity.type, error);
    if (key == "statusDisplay") return ReadChoice(key, field, kDisplayNames, kDisplayValues, activity.statusDisplay, error);
    if (key == "buttons") return ReadButtons(field, activity.buttons, error);
    if (key == "startMs") return ReadTimestamp(key, field, activity.startMs, error);
    if (key == "endMs") return ReadTimestamp(key, field, activity.endMs, error);
    if (key == "clientId") return true;  // read by the caller
    Fail(error, "unknown field in setActivity: " + key);
    return false;
}

}  // namespace

std::optional<Activity> ActivityFromJson(const Json& value, std::string* error) {
    const Json::Object* fields = value.AsObject();
    if (!fields) {
        Fail(error, "setActivity expects an object");
        return std::nullopt;
    }
    Activity activity;
    for (const auto& [key, field] : *fields) {
        if (!ReadActivityField(key, field, activity, error)) return std::nullopt;
    }
    // Discord ignores a details or state of a single byte: pad it so that the line still shows.
    for (std::string* line : {&activity.details, &activity.state}) {
        if (line->size() == 1) line->push_back(' ');
    }
    if (activity.details.empty() && activity.state.empty()) {
        Fail(error, "setActivity expects at least details or state");
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
        if (!holder) return "error without a message";
        const Json* message = holder->Find("message");
        return message && message->AsString() ? *message->AsString() : "error without a message";
    };
    if (frame.opcode == Opcode::Close) return root ? messageOf(&*root) : std::string("connection closed by Discord");
    if (!root) return std::nullopt;
    const Json* evt = root->Find("evt");
    if (evt && evt->AsString() && *evt->AsString() == "ERROR") return messageOf(root->Find("data"));
    return std::nullopt;
}

}  // namespace ampify::discord
