#include "injection_policy.hpp"

namespace amk {

PolicyVerdict DecideInjection(std::string_view appVersion, bool safeMode, std::span<const std::string_view> known) {
    if (safeMode) return {false, "mode sans échec actif : Apple Music démarre sans noyau"};
    if (appVersion.empty()) return {false, "version d'Apple Music introuvable : Apple Music démarre sans noyau"};
    if (!IsKnownAppVersion(appVersion, known)) {
        return {false, "version " + std::string(appVersion) + " non testée : Apple Music démarre sans noyau"};
    }
    return {true, "version " + std::string(appVersion) + " testée : noyau injecté"};
}

PolicyVerdict DecideKernel(ProcessRole role, std::string_view appVersion, std::span<const std::string_view> known) {
    if (role == ProcessRole::Unknown) return {false, "processus non concerné : noyau inactif"};
    if (appVersion.empty()) return {false, "version d'Apple Music introuvable : noyau inactif"};
    if (!IsKnownAppVersion(appVersion, known)) {
        return {false, "version " + std::string(appVersion) + " non testée : noyau inactif"};
    }
    return {true, "version " + std::string(appVersion) + " testée"};
}

}  // namespace amk
