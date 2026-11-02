// ampify theme import <folder|zip|address> [id]: takes up a Spicetify theme. Its color.ini works as is (the
// kernel translates its keys to Apple Music's elements, src/core/theme/spicetify.cpp); its user.css aims at Spotify's
// web classes, so it is kept aside as spicetify-user.css and never applied. An Ampify theme (a folder
// with a manifest.json of kind theme) is copied as it is.
#include <algorithm>
#include <cctype>
#include <set>

#include "../core/theme/files.hpp"
#include "../core/theme/theme.hpp"
#include "archive.hpp"
#include "internal.hpp"
#include "platform.hpp"

namespace ampify::cli {

namespace {

// What a source address names: the archive to fetch and, for a GitHub folder, the path inside the repository.
struct ImportSource {
    std::string url;      // https address of a zip
    std::string subPath;  // a folder of the repository, such as "Sleek"; empty for the whole archive
    std::string owner;    // the GitHub account, for the manifest's author
};

std::vector<std::string> SplitPath(std::string_view text) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('/', start);
        if (end == std::string_view::npos) end = text.size();
        if (end > start) parts.emplace_back(text.substr(start, end - start));
        start = end + 1;
    }
    return parts;
}

// https://github.com/<owner>/<repo>[/tree/<branch>/<folder...>] gives the repository's zip; another https address
// must name a zip itself.
std::optional<ImportSource> ResolveAddress(const std::string& address, std::string& error) {
    if (!address.starts_with("https://")) {
        error = "address refused (https only): " + address;
        return std::nullopt;
    }
    const std::string github = "https://github.com/";
    if (!address.starts_with(github)) {
        if (!address.ends_with(".zip")) {
            error = "unexpected address: a .zip archive, or a github.com page of a theme";
            return std::nullopt;
        }
        return ImportSource{address, "", ""};
    }
    std::vector<std::string> parts = SplitPath(std::string_view(address).substr(github.size()));
    if (parts.size() < 2) {
        error = "incomplete GitHub address: https://github.com/<account>/<repository>";
        return std::nullopt;
    }
    std::string repo = parts[1].ends_with(".git") ? parts[1].substr(0, parts[1].size() - 4) : parts[1];
    ImportSource source;
    source.owner = parts[0];
    source.url = github + parts[0] + "/" + repo + "/archive/HEAD.zip";
    if (parts.size() >= 4 && parts[2] == "tree") {
        source.url = github + parts[0] + "/" + repo + "/archive/" + parts[3] + ".zip";
        for (size_t i = 4; i < parts.size(); i++) source.subPath += (source.subPath.empty() ? "" : "/") + parts[i];
    }
    return source;
}

// `folder` below `root`, with forward slashes, in UTF-8.
std::string RelativeText(const std::filesystem::path& folder, const std::filesystem::path& root) {
    std::string text = Utf8(folder.lexically_relative(root));
    std::replace(text.begin(), text.end(), '\\', '/');
    return text;
}

bool IsThemeFolder(const std::filesystem::path& folder) {
    std::error_code ec;
    for (const char* name : {"color.ini", "colors.ini", "user.css"}) {
        if (std::filesystem::is_regular_file(folder / name, ec)) return true;
    }
    return false;
}

// The theme folders under `root` (root included), at most four levels down, skipping hidden folders.
std::vector<std::filesystem::path> FindThemeFolders(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> found;
    std::error_code ec;
    if (IsThemeFolder(root)) found.push_back(root);
    std::filesystem::recursive_directory_iterator it(root, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        if (it.depth() > 3) {
            it.disable_recursion_pending();
            continue;
        }
        if (!it->is_directory(ec)) continue;
        if (Utf8(it->path().filename()).starts_with(".")) {
            it.disable_recursion_pending();
            continue;
        }
        if (IsThemeFolder(it->path())) found.push_back(it->path());
    }
    return found;
}

// An Ampify identifier from a folder name: "Sleek" gives "sleek", "Dribbblish Dynamic" gives "dribbblish-dynamic".
std::string IdFromName(const std::string& name) {
    std::string id;
    for (unsigned char c : name) {
        if (std::isalnum(c)) id += static_cast<char>(std::tolower(c));
        else if (!id.empty() && id.back() != '-') id += '-';
    }
    while (!id.empty() && id.back() == '-') id.pop_back();
    if (id.size() > 40) id.resize(40);
    if (id.size() < 2) id = "theme-" + id;
    return id;
}

std::string JsonText(const std::string& text) {
    std::string quoted = "\"";
    for (char c : text) {
        if (c == '"' || c == '\\') quoted += '\\';
        if (static_cast<unsigned char>(c) >= 0x20) quoted += c;
    }
    return quoted + "\"";
}

bool CopyIfPresent(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(from, ec)) return false;
    return std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, ec);
}

// Writes the kit theme `id` in `target` from the Spicetify theme in `source`.
bool ConvertSpicetifyTheme(const std::filesystem::path& source, const std::filesystem::path& target, const std::string& id,
                           const std::string& author, const std::string& origin, std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(target, ec);
    if (ec) {
        error = "cannot create folder: " + Utf8(target);
        return false;
    }
    bool colors = CopyIfPresent(source / "color.ini", target / "color.ini") ||
                  CopyIfPresent(source / "colors.ini", target / "color.ini");
    bool spotifyCss = CopyIfPresent(source / "user.css", target / "spicetify-user.css");
    CopyIfPresent(source / "theme.js", target / "spicetify-theme.js");
    for (const auto& entry : std::filesystem::directory_iterator(source, ec)) {
        std::string name = Utf8(entry.path().filename());
        std::string lower = ToLowerAscii(name);
        if (lower.starts_with("license") || lower.starts_with("readme")) CopyIfPresent(entry.path(), target / name);
    }
    if (!colors) {
        error = "no color.ini: this Spicetify theme only changes Spotify web classes, nothing to take over";
        return false;
    }

    theme::FolderFiles files(target);
    std::vector<std::string> schemes = theme::ReadSchemes(files);
    std::ofstream css(target / "user.css", std::ios::binary);
    css << "/* Theme imported from Spicetify (" << origin << ").\n"
        << "   color.ini works as is: its keys (main, sidebar, player, text...) color the real Apple Music\n"
        << "   elements. Add your own rules here (XAML selectors, see docs/themes/format.md); colors\n"
        << "   are written $main or var(--spice-main).\n"
        << (spotifyCss ? "   The original user.css targets Spotify's web classes: it is kept in spicetify-user.css, not\n"
                         "   applied.\n"
                       : "")
        << "*/\n";
    std::ofstream manifest(target / "manifest.json", std::ios::binary);
    manifest << "{\n  \"id\": " << JsonText(id) << ",\n  \"name\": " << JsonText(Utf8(source.filename()))
             << ",\n  \"version\": \"1.0.0\",\n  \"kind\": \"theme\",\n  \"author\": " << JsonText(author)
             << ",\n  \"description\": " << JsonText("Imported from Spicetify: the colors of color.ini on Apple Music.")
             << ",\n  \"kitApi\": \"1.0\",\n  \"colors\": \"color.ini\"";
    if (!schemes.empty()) manifest << ",\n  \"defaultScheme\": " << JsonText(schemes.front());
    manifest << "\n}\n";
    if (!css || !manifest) {
        error = "cannot write in " + Utf8(target);
        return false;
    }
    return true;
}

// Downloads or extracts `argument` into `work` when needed. Returns the folder to look into.
std::optional<std::filesystem::path> Unpack(Context& ctx, const std::string& argument, const std::filesystem::path& work,
                                            ImportSource& source, std::string& error) {
    std::filesystem::path local = PathArgument(argument);
    std::error_code ec;
    std::filesystem::path archive;
    if (argument.starts_with("https://")) {
        std::optional<ImportSource> resolved = ResolveAddress(argument, error);
        if (!resolved) return std::nullopt;
        source = *resolved;
        archive = work / "source.zip";
        ctx.out << "Downloading " << source.url << "\n";
        if (!FetchTo(source.url, archive, error, 120)) return std::nullopt;
    } else if (std::filesystem::is_directory(local, ec)) {
        return local;
    } else if (std::filesystem::is_regular_file(local, ec) && Utf8(local.extension()) == ".zip") {
        archive = local;
    } else {
        error = "source not found: " + argument + " (a folder, a .zip archive or an https address)";
        return std::nullopt;
    }

    std::vector<std::string> entries;
    if (!platform::ListZip(archive, entries, error)) return std::nullopt;
    if (std::string problem = CheckArchiveListing(entries); !problem.empty()) {
        error = problem;
        return std::nullopt;
    }
    std::filesystem::path extracted = work / "extracted";
    std::filesystem::create_directories(extracted, ec);
    if (!platform::ExtractZip(archive, extracted, error)) return std::nullopt;
    if (ContainsLink(extracted)) {
        error = "symbolic link not allowed in the archive";
        return std::nullopt;
    }
    return extracted;
}

}  // namespace

int CmdThemeImport(Context& ctx) {
    const std::vector<std::string>& args = ctx.options.positional;
    if (args.size() < 3 || args.size() > 4) {
        return Usage(ctx, "theme import needs a folder, a .zip archive or an address, then an id (optional)");
    }
    std::error_code ec;
    std::filesystem::path work = ctx.kit.Downloads() / "theme-import";
    std::filesystem::remove_all(work, ec);
    std::filesystem::create_directories(work, ec);

    ImportSource source;
    std::string error;
    std::optional<std::filesystem::path> root = Unpack(ctx, args[2], work, source, error);
    if (!root) {
        std::filesystem::remove_all(work, ec);
        return Failure(ctx, error);
    }

    std::vector<std::filesystem::path> candidates = FindThemeFolders(*root);
    if (!source.subPath.empty()) {
        std::erase_if(candidates, [&](const std::filesystem::path& folder) {
            return !RelativeText(folder, *root).ends_with(source.subPath);
        });
    }
    if (candidates.size() != 1) {
        std::filesystem::remove_all(work, ec);
        if (candidates.empty()) return Failure(ctx, "no theme in " + args[2] + " (neither color.ini nor user.css)");
        ctx.err << "ampify: several themes in " << args[2] << ", name the folder:\n";
        for (const auto& folder : candidates) ctx.err << "  " << RelativeText(folder, *root) << "\n";
        return 1;
    }
    const std::filesystem::path& folder = candidates.front();

    // A theme at the top of an archive is named after the archive, not after the work folder it was extracted to.
    std::string name = Utf8(folder.filename());
    if (folder == work / "extracted") {
        std::string last = SplitPath(args[2]).empty() ? args[2] : SplitPath(args[2]).back();
        name = Utf8(PathArgument(last).stem());
    }
    std::string id = args.size() == 4 ? args[3] : IdFromName(name);
    if (!IsValidId(id)) {
        std::filesystem::remove_all(work, ec);
        return Failure(ctx, "invalid id: " + id + " (lowercase letters, digits and hyphens)");
    }
    std::filesystem::path target = ctx.kit.Root() / "themes" / id;
    if (std::filesystem::exists(target, ec)) {
        if (!ctx.options.yes) {
            std::filesystem::remove_all(work, ec);
            return Failure(ctx, "the theme " + id + " already exists: use --yes to replace it, or give another id");
        }
        std::filesystem::remove_all(target, ec);
    }

    bool kitTheme = std::filesystem::is_regular_file(folder / "manifest.json", ec);
    if (kitTheme) {
        std::filesystem::copy(folder, target, std::filesystem::copy_options::recursive, ec);
        if (ec) error = "cannot copy: " + ec.message();
    } else {
        std::string author = source.owner.empty() ? "spicetify" : source.owner;
        if (!ConvertSpicetifyTheme(folder, target, id, author, source.url.empty() ? args[2] : source.url, error)) {
            std::filesystem::remove_all(target, ec);
        }
    }
    std::filesystem::remove_all(work, ec);
    if (!error.empty()) return Failure(ctx, error);

    theme::FolderFiles files(target);
    theme::Theme loaded = theme::LoadTheme(id, target.generic_string(), files);
    if (!loaded.usable) {
        std::filesystem::remove_all(target, ec);
        return Failure(ctx, "theme refused by the theme engine: " +
                                (loaded.problems.empty() ? std::string("unknown reason") : loaded.problems.front()));
    }
    for (const std::string& problem : loaded.problems) ctx.out << "Warning: " << problem << "\n";
    ctx.out << "Theme " << id << " imported" << (kitTheme ? "" : " (Spicetify colors)") << ".\n";
    if (!loaded.schemes.empty()) {
        std::string list;
        for (const std::string& scheme : loaded.schemes) list += (list.empty() ? "" : ", ") + scheme;
        ctx.out << "Schemes: " << list << ".\n";
    }
    ctx.out << "To use it: ampify theme set " << id
            << (loaded.schemes.empty() ? std::string() : " --scheme " + loaded.schemes.front()) << "\n";
    return 0;
}

}  // namespace ampify::cli
