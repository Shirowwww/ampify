#pragma once

#include <functional>
#include <ostream>
#include <string>
#include <string_view>

namespace amk {

// Journal lines look like `[13:09:27.041] message`. The clock is injectable so tests stay deterministic.
class Journal {
public:
    using Stamp = std::function<std::string()>;

    // `out` must outlive the journal. The kernel opens it in append mode.
    Journal(std::ostream& out, Stamp stamp);

    void Write(std::string_view message);

    // Formats `HH:MM:SS.mmm` from the local clock. Used by default.
    static std::string LocalTimeStamp();

private:
    std::ostream& m_out;
    Stamp m_stamp;
};

}  // namespace amk
