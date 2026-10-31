#pragma once

#include <string>

namespace ampify {

// Safe mode: if Apple Music keeps dying or hanging right after the kernel loads, the next
// launches start without any extension, so the user gets the untouched app back.
//
// The count lives in a small settings file. It goes up at every launch and goes back to zero
// once the app has proven healthy (MarkHealthy), so a single bad update never locks the mods out.
class SafeMode {
public:
    explicit SafeMode(std::string statePath, int maxUnhealthyLaunches = 2);

    // Call once when the kernel starts. Returns true when extensions must stay off for this launch.
    bool BeginLaunch();

    // Call once the app has been up and responsive for a while (for example 20 seconds).
    void MarkHealthy();

    int UnhealthyLaunches() const;

    // Read-only version of BeginLaunch's answer: true while safe mode is on. Does not count a launch.
    bool IsActive() const;

private:
    std::string m_statePath;
    int m_maxUnhealthyLaunches;
};

}  // namespace ampify
