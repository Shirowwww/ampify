#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <typeindex>
#include <vector>

#include "host.hpp"
#include "http_worker.hpp"
#include "journal.hpp"
#include "js_runtime.hpp"
#include "manifest.hpp"

namespace ampify {

// Version of the kernel, as Ampify.version. Same number as the kit (kKitVersion), checked by CMakeLists.txt. The API version is Ampify.kitApi.
inline constexpr const char* kKernelVersion = "1.1.0";
inline constexpr const char* kKitApi = "1.0";

using Clock = std::function<std::chrono::steady_clock::time_point()>;

// One extension, theme-less app or script element. It owns a JavaScript runtime, and it runs only on the
// thread that calls Start, Stop and Pump. Post and the Deliver calls may come from any thread.
class ScriptExtension {
public:
    ScriptExtension(Manifest manifest, std::filesystem::path folder, Host& host, Journal& journal,
                    Clock clock = {}, js::Limits limits = {});
    ~ScriptExtension();

    ScriptExtension(const ScriptExtension&) = delete;
    ScriptExtension& operator=(const ScriptExtension&) = delete;

    const Manifest& manifest() const { return m_manifest; }
    const std::string& id() const { return m_manifest.id; }
    bool running() const { return m_running; }
    // Network requests that have not come back yet, including those of a run that was stopped.
    size_t httpInFlight() const { return m_http.InFlight(); }
    const std::string& lastError() const { return m_lastError; }

    // Reads main.js, runs the API and the script. False on any error, which is logged and kept in lastError.
    bool Start();

    // Calls the onStop handlers in reverse order, releases the shortcuts and the gain, then frees the runtime.
    void Stop();

    // Runs the queued tasks and the timers that are due. Call it regularly from the owner thread.
    void Pump();

    // Queues a player event for the next Pump. Dropped when the element is not running.
    void DeliverPlayer(const std::string& event, const PlayerSnapshot& snapshot);

    // Queues a shortcut press, in the normalized form given to Host::RegisterKey.
    void DeliverKey(const std::string& combo);

    // Queues a click on a control of the interface that this element added.
    void DeliverClick(int controlId);

    // Queues a call to the prelude's dispatch.<entry>(payload), for a module that sends its own events (the
    // fragment adds that entry point; the payload is usually JSON text). Nothing happens if no module defined it,
    // except a line in the journal.
    void DeliverEvent(const std::string& entry, const std::string& payload);

    // Settings prefix of the element: "extensions." or "apps.".
    std::string ConfigPrefix() const;

private:
    friend struct ScriptBridge;  // the natives of the API modules (src/core/api/bridge.hpp)

    void Post(std::function<void()> task);
    // Runs `request` on a worker thread and delivers its answer to dispatch.http on a later Pump, unless the element
    // stopped meanwhile. Returns the id of the request, 0 at the cap (kMaxHttpInFlight). `request` runs off-thread.
    uint32_t SubmitHttp(std::function<HttpResponse()> request);
    void Teardown();
    bool CallDispatch(const char* entry, int argc, JSValueConst* argv);
    void DispatchPlayer(const std::string& event, const std::string& stateJson);
    void DispatchKey(const std::string& combo);
    void DispatchClick(int controlId);
    void DispatchEvent(const std::string& entry, const std::string& payload);
    void DispatchTimer(uint32_t id);

    Manifest m_manifest;
    std::filesystem::path m_folder;
    Host& m_host;
    Journal& m_journal;
    Clock m_clock;
    js::Limits m_limits;

    std::unique_ptr<js::Runtime> m_runtime;
    JSValue m_dispatch = JS_UNDEFINED;  // the object returned by the prelude

    std::map<std::string, std::string> m_storage;  // key -> JSON text of the value
    std::multimap<std::chrono::steady_clock::time_point, uint32_t> m_timers;
    double m_gainDb = 0;
    std::map<std::type_index, std::shared_ptr<void>> m_moduleState;  // see ScriptBridge::State

    std::mutex m_queueMutex;
    std::vector<std::function<void()>> m_queue;
    bool m_accepting = false;  // under m_queueMutex: from the start of main.js to Stop, so an early answer waits its turn
    std::atomic<bool> m_running{false};
    std::string m_lastError;
    uint32_t m_lastHttpId = 0;
    HttpWorker m_http;  // its threads call Post, so ~ScriptExtension joins them first
};

// Bytes of storage one element may keep. Above it, storage.set throws a QuotaExceeded error.
inline constexpr size_t kStorageQuotaBytes = size_t{1} << 20;

}  // namespace ampify
