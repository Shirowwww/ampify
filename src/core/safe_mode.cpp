#include "safe_mode.hpp"

#include <utility>

#include "settings.hpp"

namespace ampify {

namespace {

constexpr const char* kUnhealthyKey = "unhealthy_launches";
constexpr const char* kSafeLaunchKey = "safe_launch";

}  // namespace

SafeMode::SafeMode(std::string statePath, int maxUnhealthyLaunches)
    : m_statePath(std::move(statePath)), m_maxUnhealthyLaunches(maxUnhealthyLaunches) {}

bool SafeMode::BeginLaunch() {
    Settings state;
    state.Load(m_statePath);

    int unhealthy = state.GetInt(kUnhealthyKey, 0);
    bool safe = unhealthy >= m_maxUnhealthyLaunches;

    state.SetInt(kUnhealthyKey, unhealthy + 1);
    state.SetInt(kSafeLaunchKey, safe ? 1 : 0);
    state.Save(m_statePath);
    return safe;
}

bool SafeMode::ThisLaunchSafe() const {
    Settings state;
    state.Load(m_statePath);
    return state.GetInt(kSafeLaunchKey, 0) != 0;
}

void SafeMode::MarkHealthy() {
    Settings state;
    state.Load(m_statePath);
    state.SetInt(kUnhealthyKey, 0);
    state.Save(m_statePath);
}

int SafeMode::UnhealthyLaunches() const {
    Settings state;
    state.Load(m_statePath);
    return state.GetInt(kUnhealthyKey, 0);
}

bool SafeMode::IsActive() const {
    return UnhealthyLaunches() >= m_maxUnhealthyLaunches;
}

}  // namespace ampify
