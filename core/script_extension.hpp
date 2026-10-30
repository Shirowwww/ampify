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
#include <vector>

#include "host.hpp"
#include "journal.hpp"
#include "js_runtime.hpp"
#include "manifest.hpp"

namespace amk {

// Version of the kernel, as AMK.version. The API version is AMK.kitApi.
inline constexpr const char* kKernelVersion = "0.1.0";
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

    // Settings prefix of the element: "extensions." or "apps.".
    std::string ConfigPrefix() const;

private:
    friend struct ScriptBridge;

    void Post(std::function<void()> task);
    void Teardown();
    bool CallDispatch(const char* entry, int argc, JSValueConst* argv);
    void DispatchPlayer(const std::string& event, const std::string& stateJson);
    void DispatchKey(const std::string& combo);
    void DispatchClick(int controlId);
    void DispatchTimer(uint32_t id);
    void PersistStorage();

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

    std::mutex m_queueMutex;
    std::vector<std::function<void()>> m_queue;
    std::atomic<bool> m_running{false};
    std::string m_lastError;
};

// Bytes of storage one element may keep. Above it, storage.set throws a QuotaExceeded error.
inline constexpr size_t kStorageQuotaBytes = size_t{1} << 20;

}  // namespace amk
