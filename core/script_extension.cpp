#include "script_extension.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string_view>

#include "amk_prelude.hpp"
#include "json.hpp"

namespace amk {

namespace {

constexpr size_t kMaxTimerDispatchesPerPump = 256;  // a repeating timer cannot starve the other work
constexpr double kDefaultGainCeilingDb = 6.0;
constexpr size_t kMaxNotifyTitle = 120;
constexpr size_t kMaxNotifyText = 500;

std::optional<std::string> ReadFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::nullopt;
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// Keys look like `theme.scheme`: letters, digits, '.', '-' and '_', starting with a letter or a digit.
bool ValidConfigKey(const std::string& key) {
    if (key.empty() || key.size() > 64 || key == "enabled") return false;
    if (key.find("..") != std::string::npos) return false;
    if (!std::isalnum(static_cast<unsigned char>(key[0]))) return false;
    return std::all_of(key.begin(), key.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_';
    });
}

std::map<std::string, std::string> ParseStorage(const std::optional<std::string>& text) {
    std::map<std::string, std::string> values;
    if (!text) return values;
    std::optional<Json> root = Json::Parse(*text);
    if (!root || !root->AsObject()) return values;
    for (const auto& [key, value] : *root->AsObject()) values[key] = value.Dump();
    return values;
}

std::string DumpStorage(const std::map<std::string, std::string>& values) {
    std::string out = "{";
    bool first = true;
    for (const auto& [key, raw] : values) {
        if (!first) out += ',';
        first = false;
        out += Json(key).Dump() + ':' + raw;
    }
    return out + '}';
}

Json PlayerJson(const PlayerSnapshot& snapshot) {
    Json::Object members;
    members.emplace_back("playing", Json(snapshot.playing));
    members.emplace_back("positionMs", Json(static_cast<double>(snapshot.positionMs)));
    members.emplace_back("durationMs", Json(static_cast<double>(snapshot.durationMs)));
    if (!snapshot.track) {
        members.emplace_back("track", Json());
    } else {
        const TrackInfo& t = *snapshot.track;
        Json::Object track;
        track.emplace_back("id", Json(t.id));
        track.emplace_back("title", Json(t.title));
        track.emplace_back("artist", Json(t.artist));
        track.emplace_back("album", Json(t.album));
        track.emplace_back("variantId", Json(t.variantId));
        track.emplace_back("atmos", Json(t.atmos));
        track.emplace_back("lossless", Json(t.lossless));
        members.emplace_back("track", Json(std::move(track)));
    }
    return Json(std::move(members));
}

// The host of an https URL, lowercase, or empty when the URL has another form.
std::string HttpsHost(const std::string& url) {
    constexpr std::string_view kPrefix = "https://";
    if (url.compare(0, kPrefix.size(), kPrefix) != 0) return {};
    size_t end = url.find_first_of("/?#", kPrefix.size());
    std::string host = url.substr(kPrefix.size(), end == std::string::npos ? std::string::npos : end - kPrefix.size());
    if (host.empty() || host.find_first_of("@:[") != std::string::npos) return {};
    std::transform(host.begin(), host.end(), host.begin(), [](unsigned char c) { return std::tolower(c); });
    return host;
}

}  // namespace

// The natives that the prelude calls. Each one checks the permission it needs, then acts through the host.
// A failed check throws an Error named PermissionDenied, which says which permission is missing.
struct ScriptBridge {
    static ScriptExtension* Owner(JSContext* ctx) {
        return static_cast<ScriptExtension*>(js::Runtime::FromContext(ctx)->UserData());
    }

    static JSValue Throw(JSContext* ctx, const char* name, const std::string& message) {
        return JS_Throw(ctx, js::NewNamedError(ctx, name, message));
    }

    static std::optional<std::string> String(JSContext* ctx, int argc, JSValueConst* argv, int index) {
        if (index >= argc) return std::nullopt;
        const char* text = JS_ToCString(ctx, argv[index]);
        if (!text) return std::nullopt;
        std::string out(text);
        JS_FreeCString(ctx, text);
        return out;
    }

    static double Number(JSContext* ctx, int argc, JSValueConst* argv, int index) {
        double value = 0;
        if (index < argc) JS_ToFloat64(ctx, &value, argv[index]);
        return value;
    }

    // Returns an exception value when `permission` is not declared, or JS_UNDEFINED when it is.
    static bool Require(JSContext* ctx, ScriptExtension* ext, const std::string& permission, JSValue* failure) {
        if (ext->manifest().HasPermission(permission)) return true;
        *failure = Throw(ctx, "PermissionDenied",
                         "permission manquante : " + permission + " (à déclarer dans manifest.json)");
        return false;
    }

#define AMK_NATIVE(name) static JSValue name([[maybe_unused]] JSContext* ctx, [[maybe_unused]] JSValueConst self, [[maybe_unused]] int argc, [[maybe_unused]] JSValueConst* argv)

    AMK_NATIVE(Log) {
        ScriptExtension* ext = Owner(ctx);
        std::string message = String(ctx, argc, argv, 0).value_or("");
        ext->m_journal.Write("[" + ext->id() + "] " + message);
        return JS_UNDEFINED;
    }

    AMK_NATIVE(Require) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, String(ctx, argc, argv, 0).value_or(""), &failure)) return failure;
        return JS_UNDEFINED;
    }

    AMK_NATIVE(ConfigGet) {
        ScriptExtension* ext = Owner(ctx);
        std::string key = String(ctx, argc, argv, 0).value_or("");
        if (!ValidConfigKey(key)) return Throw(ctx, "TypeError", "clé de réglage invalide : " + key);
        std::optional<std::string> value = ext->m_host.ReadSetting(ext->ConfigPrefix() + ext->id() + "." + key);
        return value ? JS_NewStringLen(ctx, value->data(), value->size()) : JS_UNDEFINED;
    }

    AMK_NATIVE(ConfigSet) {
        ScriptExtension* ext = Owner(ctx);
        std::string key = String(ctx, argc, argv, 0).value_or("");
        std::string value = String(ctx, argc, argv, 1).value_or("");
        if (!ValidConfigKey(key)) return Throw(ctx, "TypeError", "clé de réglage invalide : " + key);
        ext->m_host.WriteSetting(ext->ConfigPrefix() + ext->id() + "." + key, value);
        return JS_UNDEFINED;
    }

    AMK_NATIVE(StorageGet) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "storage", &failure)) return failure;
        auto found = ext->m_storage.find(String(ctx, argc, argv, 0).value_or(""));
        if (found == ext->m_storage.end()) return JS_UNDEFINED;
        return JS_NewStringLen(ctx, found->second.data(), found->second.size());
    }

    AMK_NATIVE(StorageSet) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "storage", &failure)) return failure;
        std::string key = String(ctx, argc, argv, 0).value_or("");
        std::string raw = String(ctx, argc, argv, 1).value_or("");
        if (key.empty() || key.size() > 128) return Throw(ctx, "TypeError", "clé de stockage invalide");
        if (!Json::Parse(raw)) return Throw(ctx, "TypeError", "valeur de stockage non JSON");

        std::map<std::string, std::string> next = ext->m_storage;
        next[key] = raw;
        if (DumpStorage(next).size() > kStorageQuotaBytes) {
            return Throw(ctx, "QuotaExceeded", "stockage plein : " + std::to_string(kStorageQuotaBytes) + " octets au plus");
        }
        ext->m_storage = std::move(next);
        ext->PersistStorage();
        return JS_UNDEFINED;
    }

    AMK_NATIVE(StorageRemove) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "storage", &failure)) return failure;
        if (ext->m_storage.erase(String(ctx, argc, argv, 0).value_or("")) > 0) ext->PersistStorage();
        return JS_UNDEFINED;
    }

    AMK_NATIVE(PlayerState) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "player.read", &failure)) return failure;
        std::string json = PlayerJson(ext->m_host.Player()).Dump();
        return JS_NewStringLen(ctx, json.data(), json.size());
    }

    AMK_NATIVE(PlayerControl) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "player.control", &failure)) return failure;
        std::string name = String(ctx, argc, argv, 0).value_or("");
        int64_t seek = static_cast<int64_t>(Number(ctx, argc, argv, 1));
        PlayerCommand command;
        if (name == "play") command = PlayerCommand::Play;
        else if (name == "pause") command = PlayerCommand::Pause;
        else if (name == "next") command = PlayerCommand::Next;
        else if (name == "previous") command = PlayerCommand::Previous;
        else if (name == "seek") command = PlayerCommand::Seek;
        else return Throw(ctx, "TypeError", "commande de lecteur inconnue : " + name);
        return JS_NewBool(ctx, ext->m_host.Control(command, seek));
    }

    AMK_NATIVE(SetGain) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "audio.gain", &failure)) return failure;
        // The ceiling belongs to the kernel's settings, never to the extension.
        double ceiling = kDefaultGainCeilingDb;
        if (std::optional<std::string> raw = ext->m_host.ReadSetting("audio.maxGainDb")) {
            ceiling = std::clamp(std::strtod(raw->c_str(), nullptr), 0.0, 12.0);
        }
        double db = std::clamp(Number(ctx, argc, argv, 0), -ceiling, ceiling);
        ext->m_gainDb = db;
        ext->m_host.SetGainDb(ext->id(), db);
        return JS_NewFloat64(ctx, db);
    }

    AMK_NATIVE(Notify) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "notifications", &failure)) return failure;
        std::string title = String(ctx, argc, argv, 0).value_or("").substr(0, kMaxNotifyTitle);
        std::string body = String(ctx, argc, argv, 1).value_or("").substr(0, kMaxNotifyText);
        ext->m_host.Notify(ext->id(), title, body);
        return JS_UNDEFINED;
    }

    AMK_NATIVE(BindKey) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "ui.keyboard", &failure)) return failure;
        return JS_NewBool(ctx, ext->m_host.RegisterKey(ext->id(), String(ctx, argc, argv, 0).value_or("")));
    }

    AMK_NATIVE(HttpGet) {
        ScriptExtension* ext = Owner(ctx);
        std::string url = String(ctx, argc, argv, 0).value_or("");
        std::string host = HttpsHost(url);
        if (host.empty()) return Throw(ctx, "TypeError", "seules les URL https:// sont autorisées");
        std::vector<std::string> allowed = ext->manifest().NetworkHosts();
        if (std::find(allowed.begin(), allowed.end(), host) == allowed.end()) {
            return Throw(ctx, "PermissionDenied", "permission manquante : network:" + host + " (à déclarer dans manifest.json)");
        }
        HttpResponse response = ext->m_host.HttpGet(url);
        JSValue object = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, object, "status", JS_NewInt32(ctx, response.status));
        JS_SetPropertyStr(ctx, object, "text", JS_NewStringLen(ctx, response.text.data(), response.text.size()));
        return object;
    }

    AMK_NATIVE(Schedule) {
        ScriptExtension* ext = Owner(ctx);
        uint32_t id = static_cast<uint32_t>(Number(ctx, argc, argv, 0));
        auto delay = std::chrono::milliseconds(std::max<int64_t>(1, static_cast<int64_t>(Number(ctx, argc, argv, 1))));
        // A timer id is scheduled once: rescheduling replaces the previous due time.
        for (auto it = ext->m_timers.begin(); it != ext->m_timers.end();) {
            it = it->second == id ? ext->m_timers.erase(it) : std::next(it);
        }
        ext->m_timers.emplace(ext->m_clock() + delay, id);
        return JS_UNDEFINED;
    }

    AMK_NATIVE(Cancel) {
        ScriptExtension* ext = Owner(ctx);
        uint32_t id = static_cast<uint32_t>(Number(ctx, argc, argv, 0));
        for (auto it = ext->m_timers.begin(); it != ext->m_timers.end();) {
            it = it->second == id ? ext->m_timers.erase(it) : std::next(it);
        }
        return JS_UNDEFINED;
    }

#undef AMK_NATIVE

    // The object passed to the prelude. Its fields are the only way down to the kernel.
    static JSValue MakeNatives(JSContext* ctx, ScriptExtension* ext) {
        JSValue natives = JS_NewObject(ctx);
        auto define = [&](const char* name, JSCFunction* fn, int length) {
            JS_SetPropertyStr(ctx, natives, name, JS_NewCFunction(ctx, fn, name, length));
        };
        define("log", Log, 1);
        define("require", Require, 1);
        define("configGet", ConfigGet, 1);
        define("configSet", ConfigSet, 2);
        define("storageGet", StorageGet, 1);
        define("storageSet", StorageSet, 2);
        define("storageRemove", StorageRemove, 1);
        define("playerState", PlayerState, 0);
        define("playerCommand", PlayerControl, 2);
        define("setGain", SetGain, 1);
        define("notify", Notify, 2);
        define("bindKey", BindKey, 1);
        define("httpGet", HttpGet, 1);
        define("schedule", Schedule, 2);
        define("cancel", Cancel, 1);
        JS_SetPropertyStr(ctx, natives, "version", JS_NewString(ctx, kKernelVersion));
        JS_SetPropertyStr(ctx, natives, "kitApi", JS_NewString(ctx, kKitApi));
        JS_SetPropertyStr(ctx, natives, "process", JS_NewString(ctx, ext->m_host.Process().c_str()));
        JS_SetPropertyStr(ctx, natives, "locale", JS_NewString(ctx, ext->m_host.Locale().c_str()));
        return natives;
    }
};

ScriptExtension::ScriptExtension(Manifest manifest, std::filesystem::path folder, Host& host, Journal& journal,
                                 Clock clock, js::Limits limits)
    : m_manifest(std::move(manifest)),
      m_folder(std::move(folder)),
      m_host(host),
      m_journal(journal),
      m_clock(clock ? std::move(clock) : [] { return std::chrono::steady_clock::now(); }),
      m_limits(limits) {}

ScriptExtension::~ScriptExtension() { Teardown(); }

std::string ScriptExtension::ConfigPrefix() const {
    return m_manifest.kind == ElementKind::App ? "apps." : "extensions.";
}

bool ScriptExtension::Start() {
    if (m_running) return true;

    auto fail = [&](const std::string& reason) {
        m_lastError = reason;
        m_journal.Write("[" + id() + "] " + reason);
        Teardown();
        return false;
    };

    std::optional<std::string> source = ReadFile(m_folder / m_manifest.main.value_or("main.js"));
    if (!source) return fail("main.js illisible");

    m_storage = ParseStorage(m_host.ReadStorage(id()));
    m_runtime = std::make_unique<js::Runtime>(m_limits);
    m_runtime->SetUserData(this);

    std::string error;
    JSContext* ctx = m_runtime->context();
    JSValue prelude = JS_UNDEFINED;
    if (!m_runtime->Eval(kAmkPrelude, "amk-prelude.js", &error, &prelude)) return fail("API AMK : " + error);

    JSValue natives = ScriptBridge::MakeNatives(ctx, this);
    JSValue dispatch = JS_UNDEFINED;
    bool built = m_runtime->Call(prelude, 1, &natives, &dispatch, &error);
    m_runtime->Free(natives);
    m_runtime->Free(prelude);
    if (!built) return fail("API AMK : " + error);
    m_dispatch = dispatch;

    if (!m_runtime->Eval(*source, id() + "/main.js", &error)) return fail(error);
    if (!m_runtime->RunJobs(&error)) return fail(error);

    m_running = true;
    return true;
}

void ScriptExtension::Stop() {
    if (!m_running && !m_runtime) return;
    {
        // Set under the queue lock, so that a Post racing with Stop cannot leave a task behind.
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_running = false;
        m_queue.clear();
    }
    m_timers.clear();

    if (m_runtime) CallDispatch("stop", 0, nullptr);
    if (m_gainDb != 0) m_host.SetGainDb(id(), 0);
    m_host.UnregisterKeys(id());
    Teardown();
}

void ScriptExtension::Teardown() {
    if (m_runtime) {
        m_runtime->Free(m_dispatch);
        m_dispatch = JS_UNDEFINED;
    }
    m_runtime.reset();
    m_running = false;
    m_gainDb = 0;
    m_timers.clear();
}

bool ScriptExtension::CallDispatch(const char* entry, int argc, JSValueConst* argv) {
    JSContext* ctx = m_runtime->context();
    JSValue fn = JS_GetPropertyStr(ctx, m_dispatch, entry);
    JSValue result = JS_UNDEFINED;
    std::string error;
    bool ok = m_runtime->Call(fn, argc, argv, &result, &error) && m_runtime->RunJobs(&error);
    m_runtime->Free(result);
    m_runtime->Free(fn);
    if (!ok) m_journal.Write("[" + id() + "] " + error);
    return ok;
}

void ScriptExtension::Pump() {
    if (!m_running) return;

    std::vector<std::function<void()>> tasks;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        tasks.swap(m_queue);
    }
    for (auto& task : tasks) {
        if (!m_running) return;
        task();
    }

    auto now = m_clock();
    size_t dispatched = 0;
    while (m_running && !m_timers.empty() && m_timers.begin()->first <= now &&
           dispatched < kMaxTimerDispatchesPerPump) {
        uint32_t timer = m_timers.begin()->second;
        m_timers.erase(m_timers.begin());
        DispatchTimer(timer);
        dispatched++;
    }
}

void ScriptExtension::Post(std::function<void()> task) {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    if (m_running) m_queue.push_back(std::move(task));
}

void ScriptExtension::DeliverPlayer(const std::string& event, const PlayerSnapshot& snapshot) {
    std::string json = PlayerJson(snapshot).Dump();
    Post([this, event, json] { DispatchPlayer(event, json); });
}

void ScriptExtension::DeliverKey(const std::string& combo) {
    Post([this, combo] { DispatchKey(combo); });
}

void ScriptExtension::DispatchPlayer(const std::string& event, const std::string& stateJson) {
    JSContext* ctx = m_runtime->context();
    JSValue args[2] = {JS_NewString(ctx, event.c_str()), JS_NewStringLen(ctx, stateJson.data(), stateJson.size())};
    CallDispatch("player", 2, args);
    m_runtime->Free(args[0]);
    m_runtime->Free(args[1]);
}

void ScriptExtension::DispatchKey(const std::string& combo) {
    JSContext* ctx = m_runtime->context();
    JSValue arg = JS_NewString(ctx, combo.c_str());
    CallDispatch("key", 1, &arg);
    m_runtime->Free(arg);
}

void ScriptExtension::DispatchTimer(uint32_t id) {
    JSContext* ctx = m_runtime->context();
    JSValue arg = JS_NewInt32(ctx, static_cast<int32_t>(id));
    CallDispatch("timer", 1, &arg);
    m_runtime->Free(arg);
}

void ScriptExtension::PersistStorage() { m_host.WriteStorage(id(), DumpStorage(m_storage)); }

}  // namespace amk
