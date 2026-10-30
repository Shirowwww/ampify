#include "gain_board.hpp"

#include <algorithm>
#include <cmath>

namespace amk {

void GainBoard::Set(const std::string& elementId, double db, double ceilingDb) {
    if (db == 0) {
        m_gains.erase(elementId);
    } else {
        m_gains[elementId] = db;
    }
    double total = 0;
    for (const auto& [id, value] : m_gains) total += value;
    m_totalDb = std::clamp(total, -ceilingDb, ceilingDb);
    m_linear.store(static_cast<float>(std::pow(10.0, m_totalDb / 20.0)), std::memory_order_relaxed);
}

GainBoard& ElementGains() {
    static GainBoard board;
    return board;
}

}  // namespace amk
