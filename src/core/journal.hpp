#pragma once

#include <functional>
#include <mutex>
#include <ostream>
#include <string>
#include <string_view>

namespace ampify {

// Journal lines look like `[13:09:27.041] message`. The clock is injectable so tests stay deterministic.
// Write is safe to call from several threads: the kernel's worker and the interface thread both log.
class Journal {
public:
    using Stamp = std::function<std::string()>;

    // `out` must outlive the journal. The kernel opens it in append mode.
    Journal(std::ostream& out, Stamp stamp = &Journal::LocalTimeStamp);

    void Write(std::string_view message);

    // Formats `HH:MM:SS.mmm` from the local clock. The default stamp.
    static std::string LocalTimeStamp();

private:
    std::ostream& m_out;
    Stamp m_stamp;
    std::mutex m_mutex;
};

}  // namespace ampify
