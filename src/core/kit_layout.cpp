#include "kit_layout.hpp"

#include <algorithm>
#include <system_error>

#include "text_util.hpp"

namespace ampify {

std::vector<std::string_view> InstalledFiles() {
    return {kLauncherFile, kKernelFile, kSetupFile, kCliFile};
}

bool IsUpdateLeftover(std::string_view fileName) {
    const std::string name = ToLowerAscii(fileName);
    if (name == "build_info.txt" || name == "readme.txt" || name == "sha256sums.txt") return true;
    constexpr std::string_view kOld = ".old";
    return name.size() > kOld.size() && name.compare(name.size() - kOld.size(), kOld.size(), kOld) == 0;
}

bool IsKitDataFolder(std::string_view path) {
    // Trailing separators do not count: "C:\Users\User\Ampify\" is the same folder.
    while (!path.empty() && (path.back() == '\\' || path.back() == '/')) path.remove_suffix(1);
    return ToLowerAscii(FileName(path)) == ToLowerAscii(kKitFolderName);
}

namespace {

const KitElement* FindKitElement(std::string_view folder, std::string_view id) {
    for (const KitElement& element : kKitElements) {
        if (element.folder == folder && element.id == id) return &element;
    }
    return nullptr;
}

}  // namespace

std::vector<std::string> BundledElements() {
    std::vector<std::string> bundled;
    for (const KitElement& element : kKitElements) {
        if (!element.builtIn && !element.catalog) {
            bundled.push_back(std::string(element.folder) + "/" + std::string(element.id));
        }
    }
    return bundled;
}

bool IsBundledElement(std::string_view folder, std::string_view id) {
    const KitElement* element = FindKitElement(folder, id);
    return element != nullptr && !element->builtIn && !element->catalog;
}

std::vector<ElementRef> CatalogStarters() {
    std::vector<ElementRef> starters;
    for (const KitElement& element : kKitElements) {
        if (!element.catalog || !element.startsOn) continue;
        if (std::optional<ElementKind> kind = KindNamed(element.kind)) {
            starters.push_back(ElementRef{*kind, std::string(element.id)});
        }
    }
    return starters;
}

bool IsOnByDefault(std::string_view folder, std::string_view id) {
    const KitElement* element = FindKitElement(folder, id);
    return element != nullptr && element->startsOn;
}

bool IsFirstParty(std::string_view id) {
    return std::any_of(std::begin(kKitElements), std::end(kKitElements),
                       [&](const KitElement& element) { return element.firstParty && element.id == id; });
}

std::vector<std::string> InstallBundledElements(const std::filesystem::path& package, const std::filesystem::path& kit) {
    namespace fs = std::filesystem;
    std::vector<std::string> failures;
    for (const std::string& element : BundledElements()) {
        fs::path source = package / "examples" / fs::path(element);
        fs::path destination = kit / fs::path(element);
        std::error_code error;
        if (!fs::is_regular_file(source / "manifest.json", error)) {
            failures.push_back(element + ": not in the package");
            continue;
        }
        // The old copy goes first, so a file the new version dropped does not stay behind.
        fs::remove_all(destination, error);
        if (!error) fs::create_directories(destination.parent_path(), error);
        if (!error) fs::copy(source, destination, fs::copy_options::recursive, error);
        if (error) failures.push_back(element + ": " + error.message());
    }
    return failures;
}

namespace {

std::vector<std::string_view> PathEntries(std::string_view path) {
    std::vector<std::string_view> entries;
    while (!path.empty()) {
        size_t end = path.find(';');
        std::string_view entry = path.substr(0, end);
        if (!entry.empty()) entries.push_back(entry);
        path = end == std::string_view::npos ? std::string_view() : path.substr(end + 1);
    }
    return entries;
}

bool SameFolder(std::string_view a, std::string_view b) {
    while (!a.empty() && (a.back() == '\\' || a.back() == '/')) a.remove_suffix(1);
    while (!b.empty() && (b.back() == '\\' || b.back() == '/')) b.remove_suffix(1);
    return ToLowerAscii(a) == ToLowerAscii(b);
}

std::string JoinPath(const std::vector<std::string_view>& entries) {
    std::string joined;
    for (std::string_view entry : entries) {
        if (!joined.empty()) joined += ';';
        joined += entry;
    }
    return joined;
}

}  // namespace

std::string PathWithFolder(std::string_view path, std::string_view folder) {
    std::vector<std::string_view> entries = PathEntries(path);
    bool present = std::any_of(entries.begin(), entries.end(), [&](std::string_view e) { return SameFolder(e, folder); });
    if (!present) entries.push_back(folder);
    return present ? std::string(path) : JoinPath(entries);
}

std::string PathWithoutFolder(std::string_view path, std::string_view folder) {
    std::vector<std::string_view> entries = PathEntries(path);
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](std::string_view e) { return SameFolder(e, folder); }),
                  entries.end());
    return JoinPath(entries);
}

}  // namespace ampify
