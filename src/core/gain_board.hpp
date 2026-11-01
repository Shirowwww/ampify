#pragma once

#include <atomic>
#include <map>
#include <string>

namespace ampify {

// The gain that each element asks for, in dB, added together. The total is clamped to the ceiling, so several
// elements cannot push past it. Written on the owner thread of the elements; the audio threads read the linear
// total, which is published atomically. The gain goes into the decoded samples only, never the volume slider.
class GainBoard {
public:
    // Sets the gain of one element. 0 removes it. The total is then clamped to +/- ceilingDb.
    void Set(const std::string& elementId, double db, double ceilingDb);

    // The clamped total in dB, as set by the last call.
    double TotalDb() const { return m_totalDb; }

    // The linear factor of the total, for the audio threads. 1 when no element has a gain.
    float Linear() const { return m_linear.load(std::memory_order_relaxed); }

    // How many elements currently have a gain.
    size_t Count() const { return m_gains.size(); }

private:
    std::map<std::string, double> m_gains;
    double m_totalDb = 0;
    std::atomic<float> m_linear{1.0f};
};

// The gain board of this process. Elements reach it through the Windows host; the Atmos hooks apply it.
GainBoard& ElementGains();

}  // namespace ampify
