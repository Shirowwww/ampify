#include "watch.hpp"

#include <cstdint>
#include <filesystem>
#include <system_error>

namespace ampify {

namespace {

// FNV-1a: small, fast and stable across runs, which is all a change detector needs.
void Mix(std::uint64_t& hash, std::string_view text) {
    for (unsigned char byte : text) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    hash ^= 0xFF;
    hash *= 1099511628211ULL;
}

void MixFile(std::uint64_t& hash, const std::filesystem::path& file, const std::filesystem::path& base) {
    std::error_code ec;
    std::string relative = std::filesystem::relative(file, base, ec).generic_string();
    Mix(hash, relative);
    Mix(hash, std::to_string(std::filesystem::file_size(file, ec)));
    auto stamp = std::filesystem::last_write_time(file, ec).time_since_epoch().count();
    Mix(hash, std::to_string(stamp));
}

void MixTree(std::uint64_t& hash, const std::filesystem::path& folder, const std::filesystem::path& base) {
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec)) return;
    for (auto it = std::filesystem::recursive_directory_iterator(folder, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (it->is_directory(ec) && it->path().filename() == "node_modules") {
            it.disable_recursion_pending();  // an author's dependencies are not part of the element
            continue;
        }
        if (it->is_regular_file(ec)) MixFile(hash, it->path(), base);
    }
}

std::string Hex(std::uint64_t hash) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string text(16, '0');
    for (int i = 15; i >= 0; i--) {
        text[static_cast<size_t>(i)] = kHex[hash & 0xF];
        hash >>= 4;
    }
    return text;
}

}  // namespace

std::string KitFingerprint(const KitLayout& kit) {
    std::uint64_t hash = 14695981039346656037ULL;
    std::error_code ec;

    if (std::filesystem::is_regular_file(kit.Settings(), ec)) MixFile(hash, kit.Settings(), kit.Root());
    for (std::string_view kind : kElementKindNames) MixTree(hash, kit.Folder(kind), kit.Root());
    return Hex(hash);
}

std::string FolderFingerprint(const std::filesystem::path& folder) {
    std::uint64_t hash = 14695981039346656037ULL;
    MixTree(hash, folder, folder);
    return Hex(hash);
}

}  // namespace ampify
