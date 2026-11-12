#pragma once

// How often the periodic pass of a theme must go over the whole window. Every tick used to walk the window and apply
// the set again, about 20 ms of the interface thread every 3 s, even when nothing moved. A pass after a change (a new
// page, rows built while scrolling) already applies the whole set, so the tick after it has nothing to add; and when
// periodic passes keep writing nothing, nothing fights the theme, so they come less often, up to every fourth tick.
// A new request (settings, files, scheme, artwork colours), an inspection or the window showing again applies at once.

#include <algorithm>

namespace ampify::theme {

class TickPace {
public:
    // At most this many ticks in a row are skipped once periodic passes keep writing nothing.
    static constexpr int kMaxSkips = 3;

    // A tick is due. True when it must apply the set; false when it can be skipped.
    bool Due(bool requestChanged) {
        if (requestChanged || m_forced) {
            m_forced = false;
            m_skipped = 0;
            m_fresh = false;
            return true;
        }
        if (m_fresh) {  // a pass after a change went over everything since the last tick
            m_fresh = false;
            m_skipped = 0;
            return false;
        }
        if (m_skipped < std::min(m_quiet, kMaxSkips)) {
            m_skipped++;
            return false;
        }
        m_skipped = 0;
        return true;
    }

    // A periodic pass ran and wrote `applied` values.
    void Ticked(int applied) { m_quiet = applied > 0 ? 0 : m_quiet + 1; }

    // A pass after a change ran and wrote `applied` values: something moved, the next periodic passes come at the
    // usual pace again, but the one right after it is not needed.
    void PassedAfterChange(int applied) {
        m_fresh = true;
        if (applied > 0) m_quiet = 0;
    }

    // The next tick applies whatever it finds (the window shows again after a time hidden).
    void Force() { m_forced = true; }

private:
    int m_quiet = 0;    // periodic passes in a row that wrote nothing
    int m_skipped = 0;  // ticks skipped since the last pass
    bool m_fresh = false;
    bool m_forced = false;
};

}  // namespace ampify::theme
