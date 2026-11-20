#pragma once

// Whether Sing's studio mode keeps up on this computer: the time of each model window against the model's budget
// (ModelSpec::RunBudget). A window over budget now and then is a busy moment, and StudioStream covers it with the
// song as it is. Most of the last windows over it is a graphics card too slow for the model: the voice then comes
// back for a moment every hop, which is no karaoke, and Sing goes to light mode instead. Pure C++.

#include <array>
#include <cstddef>

namespace ampify::sing {

class PaceWatch {
public:
    static constexpr size_t kWindows = 8;  // the last windows judged
    static constexpr size_t kSlowest = 5;  // over budget among them: too slow

    explicit PaceWatch(double budgetSeconds) : m_budget(budgetSeconds) {}

    void Add(double seconds) {
        m_over[m_next] = !(seconds <= m_budget);  // a time that is not a number is no proof of speed
        m_next = (m_next + 1) % kWindows;
        if (m_seen < kWindows) m_seen++;
    }

    bool TooSlow() const {
        size_t over = 0;
        for (size_t i = 0; i < m_seen; i++) over += m_over[i] ? 1 : 0;
        return m_seen == kWindows && over >= kSlowest;
    }

    void Clear() {
        m_seen = 0;
        m_next = 0;
    }

    double Budget() const { return m_budget; }

private:
    double m_budget;
    std::array<bool, kWindows> m_over{};
    size_t m_next = 0;
    size_t m_seen = 0;
};

}  // namespace ampify::sing
