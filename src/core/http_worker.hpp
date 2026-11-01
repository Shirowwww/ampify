#pragma once

// Runs the network requests of one element off its JavaScript thread: one thread per request, at most
// kMaxHttpInFlight at a time. The worker owns its threads and joins them when it is destroyed. Cancel never waits:
// it only makes the answers of the requests in flight stale, and the owner drops them by comparing generations.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

namespace ampify {

// Requests an element may have in flight. A call beyond it is refused with a RangeError, not queued.
inline constexpr size_t kMaxHttpInFlight = 4;

class HttpWorker {
public:
    HttpWorker() = default;
    ~HttpWorker() { Join(); }

    HttpWorker(const HttpWorker&) = delete;
    HttpWorker& operator=(const HttpWorker&) = delete;

    // Runs `job` on a new thread. False when the cap is reached or no thread could be started. The job must not
    // throw. Owner thread only.
    bool Submit(std::function<void()> job) {
        Reap();
        if (InFlight() >= kMaxHttpInFlight) return false;
        try {
            m_slots.reserve(m_slots.size() + 1);  // so that the push below cannot throw after the thread exists
            auto finished = std::make_shared<std::atomic<bool>>(false);
            std::thread thread([job = std::move(job), finished] {
                job();
                finished->store(true);
            });
            m_slots.push_back(Slot{std::move(thread), std::move(finished)});
        } catch (const std::exception&) {
            return false;
        }
        return true;
    }

    // Requests whose job has not returned yet.
    size_t InFlight() const {
        size_t count = 0;
        for (const Slot& slot : m_slots) count += slot.finished->load() ? 0 : 1;
        return count;
    }

    // Changes whenever the answers of the requests in flight stop being wanted.
    uint64_t Generation() const { return m_generation.load(); }
    void Cancel() { m_generation++; }

    // Waits for every thread. The owner calls it before the things the jobs touch are destroyed.
    void Join() {
        for (Slot& slot : m_slots) slot.thread.join();
        m_slots.clear();
    }

private:
    struct Slot {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> finished;
    };

    // Joins the threads that are done, which returns at once.
    void Reap() {
        for (auto it = m_slots.begin(); it != m_slots.end();) {
            if (it->finished->load()) {
                it->thread.join();
                it = m_slots.erase(it);
            } else {
                ++it;
            }
        }
    }

    std::vector<Slot> m_slots;
    std::atomic<uint64_t> m_generation{0};
};

}  // namespace ampify
