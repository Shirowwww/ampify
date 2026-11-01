#include "script_extension.hpp"

#include <fstream>
#include <sstream>

#include "api/module.hpp"
#include "json.hpp"
#include "natives/player.hpp"

// The API itself is made of modules: core/api/modules.cpp lists them, core/prelude holds their JavaScript and
// core/natives the C++ functions that JavaScript calls. This file runs an element: its runtime, queue and timers.

namespace ampify {

namespace {

constexpr size_t kMaxTimerDispatchesPerPump = 256;  // a repeating timer cannot starve the other work

std::optional<std::string> ReadFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::nullopt;
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

}  // namespace

ScriptExtension::ScriptExtension(Manifest manifest, std::filesystem::path folder, Host& host, Journal& journal,
                                 Clock clock, js::Limits limits)
    : m_manifest(std::move(manifest)),
      m_folder(std::move(folder)),
      m_host(host),
      m_journal(journal),
      m_clock(clock ? std::move(clock) : [] { return std::chrono::steady_clock::now(); }),
      m_limits(limits) {}

ScriptExtension::~ScriptExtension() {
    Teardown();
    m_http.Join();  // bounded by the host's request timeout
}

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

    std::string mainName = m_manifest.main.value_or("main.js");
    std::optional<std::string> source = ReadFile(m_folder / mainName);
    if (!source) return fail("main.js unreadable");

    for (const api::ApiModule& module : api::Modules()) {
        if (module.start) module.start(*this);
    }
    m_runtime = std::make_unique<js::Runtime>(m_limits);
    m_runtime->SetUserData(this);
    m_runtime->SetModuleRoot(m_folder);  // import reads the element's own folder, and nothing else

    std::string error;
    JSContext* ctx = m_runtime->context();
    JSValue prelude = JS_UNDEFINED;
    if (!m_runtime->Eval(api::Prelude(), "ampify-prelude.js", &error, &prelude)) return fail("Ampify API: " + error);

    JSValue natives = api::MakeNatives(ctx, *this);
    JSValue dispatch = JS_UNDEFINED;
    bool built = m_runtime->Call(prelude, 1, &natives, &dispatch, &error);
    m_runtime->Free(natives);
    m_runtime->Free(prelude);
    if (!built) return fail("Ampify API: " + error);
    m_dispatch = dispatch;
    {
        // A request made by main.js can be answered before main.js ends: the answer waits for the first Pump.
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_accepting = true;
    }

    // "type": "module" makes main.js an ES module, with static imports. A classic script may still use import().
    bool evaluated = m_manifest.module ? m_runtime->EvalModule(*source, mainName, &error)
                                       : m_runtime->Eval(*source, mainName, &error);
    if (!evaluated) return fail(error);
    if (!m_runtime->RunJobs(&error)) return fail(error);

    m_running = true;
    DeliverEvent("ready", "");  // Ampify.ready resolves on the first Pump (core/prelude/react.cpp)
    return true;
}

void ScriptExtension::Stop() {
    if (!m_running && !m_runtime) return;
    {
        // Set under the queue lock, so that a Post racing with Stop cannot leave a task behind.
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_running = false;
        m_accepting = false;
        m_queue.clear();
    }
    m_timers.clear();

    if (m_runtime) CallDispatch("stop", 0, nullptr);
    // Each module releases what the element holds in the host: gain, shortcuts, interface.
    for (const api::ApiModule& module : api::Modules()) {
        if (module.stop) module.stop(*this);
    }
    Teardown();
}

void ScriptExtension::Teardown() {
    if (m_runtime) {
        m_runtime->Free(m_dispatch);
        m_dispatch = JS_UNDEFINED;
    }
    m_runtime.reset();
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_running = false;
        m_accepting = false;
        m_queue.clear();
    }
    m_http.Cancel();
    m_gainDb = 0;
    m_timers.clear();
    m_moduleState.clear();
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
    if (m_accepting) m_queue.push_back(std::move(task));
}

uint32_t ScriptExtension::SubmitHttp(std::function<HttpResponse()> request) {
    uint32_t id = m_lastHttpId + 1;
    uint64_t generation = m_http.Generation();
    bool started = m_http.Submit([this, id, generation, request = std::move(request)] {
        HttpResponse response;
        try {
            response = request();
        } catch (const std::exception& error) {
            response = HttpResponse{0, std::string("network: ") + error.what()};
        }
        std::string payload = Json(Json::Object{{"id", static_cast<double>(id)},
                                                {"status", static_cast<double>(response.status)},
                                                {"text", response.text}})
                                  .Dump();
        // The generation changes at Stop, so an answer of an earlier run never reaches a restarted element.
        Post([this, generation, payload = std::move(payload)] {
            if (generation == m_http.Generation()) DispatchEvent("http", payload);
        });
    });
    if (!started) return 0;
    m_lastHttpId = id;
    return id;
}

void ScriptExtension::DeliverPlayer(const std::string& event, const PlayerSnapshot& snapshot) {
    std::string json = natives::PlayerJson(snapshot).Dump();
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

void ScriptExtension::DeliverEvent(const std::string& entry, const std::string& payload) {
    Post([this, entry, payload] { DispatchEvent(entry, payload); });
}

void ScriptExtension::DispatchEvent(const std::string& entry, const std::string& payload) {
    JSValue arg = JS_NewStringLen(m_runtime->context(), payload.data(), payload.size());
    CallDispatch(entry.c_str(), 1, &arg);
    m_runtime->Free(arg);
}

void ScriptExtension::DeliverClick(int controlId) {
    Post([this, controlId] { DispatchClick(controlId); });
}

void ScriptExtension::DispatchClick(int controlId) {
    JSValue arg = JS_NewInt32(m_runtime->context(), controlId);
    CallDispatch("click", 1, &arg);
    m_runtime->Free(arg);
}

void ScriptExtension::DispatchTimer(uint32_t id) {
    JSContext* ctx = m_runtime->context();
    JSValue arg = JS_NewInt32(ctx, static_cast<int32_t>(id));
    CallDispatch("timer", 1, &arg);
    m_runtime->Free(arg);
}

}  // namespace ampify
