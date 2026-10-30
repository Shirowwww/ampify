#include "backup.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <system_error>

namespace amk {

namespace {

constexpr std::string_view kKindFolders[] = {"extensions", "themes", "apps", "snippets"};

// A name is only ever used as one folder name, so it must not carry a separator or a parent reference.
bool IsSafeBackupName(std::string_view name) {
    if (name.empty() || name == "." || name == "..") return false;
    return name.find_first_of("/\\:") == std::string_view::npos;
}

std::string TimeStamp() {
    using namespace std::chrono;
    std::time_t now = system_clock::to_time_t(system_clock::now());
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y%m%d-%H%M%S", &local);
    return buffer;
}

// Copies a file or a folder tree, one entry at a time. Symbolic links are skipped, so a link cannot pull
// files in from elsewhere. (A whole-tree std::filesystem::copy onto an existing folder fails on some Windows
// C++ runtimes, hence the explicit walk.)
bool CopyTree(const std::filesystem::path& from, const std::filesystem::path& to, std::string& error) {
    std::error_code ec;
    std::filesystem::file_status status = std::filesystem::symlink_status(from, ec);
    if (ec || status.type() == std::filesystem::file_type::not_found) return true;
    if (std::filesystem::is_symlink(status)) return true;

    if (std::filesystem::is_directory(status)) {
        std::filesystem::create_directories(to, ec);
        if (ec) {
            error = "dossier impossible à créer : " + to.string();
            return false;
        }
        for (const auto& entry : std::filesystem::directory_iterator(from, ec)) {
            if (!CopyTree(entry.path(), to / entry.path().filename(), error)) return false;
        }
        return true;
    }

    std::filesystem::create_directories(to.parent_path(), ec);
    std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        error = "copie impossible : " + from.string() + " (" + ec.message() + ")";
        return false;
    }
    return true;
}

// Empties the kit's configuration: settings.txt and the four element folders. Nothing else is touched.
bool ClearConfiguration(const KitLayout& kit, std::string& error) {
    std::error_code ec;
    std::filesystem::remove(kit.Settings(), ec);
    for (std::string_view folder : kKindFolders) {
        std::filesystem::remove_all(kit.Root() / folder, ec);
        if (ec) {
            error = "suppression impossible : " + (kit.Root() / folder).string();
            return false;
        }
    }
    return true;
}

}  // namespace

bool CopyElementFolder(const std::filesystem::path& from, const std::filesystem::path& to, std::string& error) {
    return CopyTree(from, to, error);
}

std::optional<std::string> CreateBackup(const KitLayout& kit, std::string& error) {
    std::string name = TimeStamp();
    std::filesystem::path target = kit.Backups() / name;
    for (int suffix = 2; std::filesystem::exists(target); suffix++) {
        name = TimeStamp() + "-" + std::to_string(suffix);
        target = kit.Backups() / name;
    }

    if (std::filesystem::exists(kit.Settings()) && !CopyTree(kit.Settings(), target / "settings.txt", error)) {
        return std::nullopt;
    }
    for (std::string_view folder : kKindFolders) {
        std::error_code ec;
        std::filesystem::create_directories(target / folder, ec);
        if (ec) {
            error = "dossier impossible à créer : " + (target / folder).string();
            return std::nullopt;
        }
        if (!CopyTree(kit.Root() / folder, target / folder, error)) return std::nullopt;
    }
    return name;
}

std::vector<std::string> ListBackups(const KitLayout& kit) {
    std::vector<std::string> names;
    std::error_code ec;
    if (!std::filesystem::is_directory(kit.Backups(), ec)) return names;
    for (const auto& entry : std::filesystem::directory_iterator(kit.Backups(), ec)) {
        if (entry.is_directory(ec)) names.push_back(entry.path().filename().string());
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool RestoreBackup(const KitLayout& kit, std::string_view name, std::string& error) {
    if (!IsSafeBackupName(name)) {
        error = "nom de sauvegarde invalide : " + std::string(name);
        return false;
    }
    std::filesystem::path source = kit.Backups() / std::string(name);
    std::error_code ec;
    if (!std::filesystem::is_directory(source, ec)) {
        error = "sauvegarde introuvable : " + std::string(name);
        return false;
    }

    // Keep the current state, so a restore that turns out to be wrong can itself be rolled back.
    if (!CreateBackup(kit, error)) return false;

    if (!ClearConfiguration(kit, error)) return false;
    if (std::filesystem::exists(source / "settings.txt") &&
        !CopyTree(source / "settings.txt", kit.Settings(), error)) {
        return false;
    }
    for (std::string_view folder : kKindFolders) {
        if (!CopyTree(source / folder, kit.Root() / folder, error)) return false;
    }
    return true;
}

}  // namespace amk
