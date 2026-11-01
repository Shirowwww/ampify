#pragma once

// Network calls of an element finish on a worker thread and resolve on a later Pump: a test pumps until what it
// expects shows up.

#include <chrono>
#include <functional>
#include <thread>

#include "../src/core/script_extension.hpp"

namespace ampify::test {

inline bool PumpUntil(ScriptExtension& element, const std::function<bool()>& done,
                      std::chrono::milliseconds limit = std::chrono::seconds(5)) {
    auto deadline = std::chrono::steady_clock::now() + limit;
    for (;;) {
        element.Pump();
        if (done()) return true;
        if (std::chrono::steady_clock::now() > deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

}  // namespace ampify::test
