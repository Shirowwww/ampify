#include "gain_board.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "settings_catalog.hpp"

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
    const KitSetting& row = *FindKitSetting(kGainCeilingSetting);
    if (!setting) return std::strtod(std::string(row.fallback).c_str(), nullptr);
    return std::clamp(std::strtod(setting->c_str(), nullptr), row.low, row.high);
}

}  // namespace ampify
