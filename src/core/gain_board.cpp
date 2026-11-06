#include "gain_board.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace ampify {

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

double GainCeilingDb(const std::optional<std::string>& setting) {
    constexpr double kDefaultDb = 6.0;
    constexpr double kHighestDb = 12.0;
    if (!setting) return kDefaultDb;
    return std::clamp(std::strtod(setting->c_str(), nullptr), 0.0, kHighestDb);
}

}  // namespace ampify
