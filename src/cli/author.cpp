// Commands for the people who write elements: build an archive, inspect one, and write the type declarations.

#include <algorithm>
#include <cstdint>
#include <optional>
#include <sstream>

#include "archive.hpp"
#include "element.hpp"
#include "internal.hpp"
#include "../core/json.hpp"
#include "messages.hpp"
#include "platform.hpp"
#include "sha256.hpp"

namespace ampify::cli {

std::vector<ZipSource> FilesUnder(const std::filesystem::path& folder, const std::string& prefix) {
    std::vector<ZipSource> files;
    std::error_code ec;
    auto it = std::filesystem::recursive_directory_iterator(folder, ec);
    for (; !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        const std::filesystem::path& path = it->path();
        std::string leaf = Utf8(path.filename());
        if (!leaf.empty() && leaf[0] == '.') {
            if (it->is_directory(ec)) it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(ec)) continue;

        std::string name = prefix;
        for (const auto& part : std::filesystem::relative(path, folder, ec)) {
            if (!name.empty()) name += '/';
            name += Utf8(part);
        }
        files.push_back({name, path, {}});
    }
    return files;
}

namespace {

// The entry that goes into catalog.json, with a placeholder for the address of the release.
std::string CatalogEntryText(const ElementManifest& manifest, const std::string& digest) {
    std::ostringstream out;
    out << "{\n"
        << "  \"id\": " << Json::Quote(manifest.id) << ",\n"
        << "  \"name\": " << Json::Quote(manifest.name) << ",\n"
        << "  \"version\": " << Json::Quote(manifest.version) << ",\n"
        << "  \"kind\": " << Json::Quote(manifest.kind) << ",\n"
        << "  \"author\": " << Json::Quote(manifest.author) << ",\n"
        << "  \"description\": " << Json::Quote(manifest.description) << ",\n"
        << "  \"kitApi\": " << Json::Quote(manifest.kitApi) << ",\n";
    // No translations: Ampify 1.1.0 refuses the whole catalog for a field it does not know. The archive keeps them.
    if (!manifest.tags.empty()) {
        out << "  \"tags\": [";
        for (size_t i = 0; i < manifest.tags.size(); i++) out << (i ? ", " : "") << Json::Quote(manifest.tags[i]);
        out << "],\n";
    }
    out << "  \"permissions\": [";
    for (size_t i = 0; i < manifest.permissions.size(); i++) {
        out << (i ? ", " : "") << Json::Quote(manifest.permissions[i]);
    }
    out << "],\n"
        << "  \"url\": \"https://...\",\n"
        << "  \"sha256\": " << Json::Quote(digest) << "\n"
        << "}\n";
    return out.str();
}

void PrintProblems(Context& ctx, const std::filesystem::path& folder, const FolderValidation& validation) {
    ctx.err << Utf8(folder) << ": invalid, nothing was written\n";
    for (const std::string& problem : validation.errors) ctx.err << "  - " << problem << "\n";
}

}  // namespace

int CmdBuild(Context& ctx) {
    const std::vector<std::string>& args = ctx.options.positional;
    if (args.size() < 2 || args.size() > 3) {
        return Usage(ctx, "build needs a folder, and optionally the .zip file to write");
    }

    std::filesystem::path folder = PathArgument(args[1]);
    FolderValidation validation = ValidateElementFolder(folder);
    if (!validation.ok() || !validation.errors.empty() || !validation.manifest) {
        PrintProblems(ctx, folder, validation);
        return 1;
    }
    const ElementManifest& manifest = *validation.manifest;

    std::filesystem::path archive = args.size() == 3 ? PathArgument(args[2])
                                                     : PathArgument(manifest.id + "-" + manifest.version + ".zip");
    std::error_code ec;
    if (std::filesystem::exists(archive, ec)) return Failure(ctx, "the file already exists: " + Utf8(archive));

    std::vector<ZipSource> files = FilesUnder(folder, "");
    std::string error;
    if (!WriteZip(archive, files, error)) return Failure(ctx, error);

    // Read the archive back with the tool that `ampify install` uses, so a broken archive is caught here.
    std::vector<std::string> entries;
    if (!platform::ListZip(archive, entries, error)) return Failure(ctx, "cannot read the zip back: " + error);
    std::string refusal = CheckArchiveListing(entries);
    if (!refusal.empty()) return Failure(ctx, refusal);
    if (entries.size() != files.size()) {
        return Failure(ctx, "the zip read back does not contain the " + std::to_string(files.size()) + " expected files");
    }

    std::optional<std::string> digest = Sha256File(archive);
    if (!digest) return Failure(ctx, "cannot compute the SHA-256 of " + Utf8(archive));
    std::uintmax_t size = std::filesystem::file_size(archive, ec);

    ctx.out << "Element built: " << Utf8(archive) << "\n"
            << "  files     " << files.size() << "\n"
            << "  size      " << size << " bytes\n"
            << "  SHA-256   " << *digest << "\n\n"
            << "Catalog entry. Replace the address with the one of the release, then add the entry to catalog.json in ampify-catalog:\n"
            << CatalogEntryText(manifest, *digest);
    return 0;
}

int CmdInspect(Context& ctx) {
    const std::vector<std::string>& args = ctx.options.positional;
    std::filesystem::path folder;
    bool installed = false;

    if (args.size() == 2) {
        folder = PathArgument(args[1]);
    } else if (args.size() == 3) {
        if (FolderOfKind(args[1]).empty()) {
            return Failure(ctx, "unknown type: " + args[1] + " (extension, theme, snippet or app)");
        }
        if (!IsValidId(args[2])) return Failure(ctx, Tr("invalidId", {{"id", args[2]}}));
        folder = ctx.kit.Element(args[1], args[2]);
        installed = true;
    } else {
        return Usage(ctx, "inspect needs a folder, or a type and an installed id");
    }

    FolderValidation validation = ValidateElementFolder(folder);
    if (!validation.ok() || !validation.errors.empty() || !validation.manifest) {
        PrintProblems(ctx, folder, validation);
        return 1;
    }
    const ElementManifest& manifest = *validation.manifest;

    ctx.out << manifest.name << " (" << manifest.id << " " << manifest.version << ")\n"
            << "  type         " << manifest.kind << "\n"
            << "  author       " << manifest.author << "\n"
            << "  Ampify API   " << manifest.kitApi << "\n"
            << "  description  " << manifest.description << "\n"
            << "  folder       " << Utf8(folder) << "\n"
            << "  permissions  ";
    if (manifest.permissions.empty()) ctx.out << "none";
    for (size_t i = 0; i < manifest.permissions.size(); i++) {
        ctx.out << (i ? ", " : "") << manifest.permissions[i];
    }
    ctx.out << "\n  processes    ";
    if (manifest.processes.empty()) ctx.out << "ui, engine (default)";
    for (size_t i = 0; i < manifest.processes.size(); i++) ctx.out << (i ? ", " : "") << manifest.processes[i];
    ctx.out << "\n";
    if (installed) {
        Settings settings;
        std::string error;
        if (!LoadSettings(ctx, settings, error)) return Failure(ctx, error);
        ctx.out << "  enabled      " << (IsEnabled(settings, manifest.kind, manifest.id) ? "yes" : "no") << "\n";
    }

    std::vector<ZipSource> files = FilesUnder(folder, "");
    std::sort(files.begin(), files.end(), [](const ZipSource& a, const ZipSource& b) { return a.name < b.name; });
    ctx.out << "\nFiles:\n";
    std::uintmax_t total = 0;
    std::error_code ec;
    for (const ZipSource& file : files) {
        std::uintmax_t size = std::filesystem::file_size(file.file, ec);
        total += size;
        std::optional<std::string> digest = Sha256File(file.file);
        ctx.out << "  " << Pad(std::to_string(size), 10) << (digest ? *digest : std::string(64, '?')) << "  " << file.name
                << "\n";
    }
    ctx.out << "  " << files.size() << " file(s), " << total << " bytes in total\n";
    return 0;
}

int CmdTypes(Context& ctx) {
    const std::vector<std::string>& args = ctx.options.positional;
    if (args.size() > 2) return Usage(ctx, "types takes a file, or nothing for ampify.d.ts in the current folder");

    std::filesystem::path target = args.size() == 2 ? PathArgument(args[1]) : std::filesystem::current_path() / "ampify.d.ts";
    std::error_code ec;
    if (std::filesystem::is_directory(target, ec)) target /= "ampify.d.ts";
    if (std::filesystem::exists(target, ec) && !ctx.options.yes) {
        return Failure(ctx, "the file already exists: " + Utf8(target) + " (add --yes to replace it)");
    }

    std::string error;
    if (!WriteTextFile(target, std::string(AmpifyDeclarations()), error)) return Failure(ctx, error);
    ctx.out << "Types written: " << Utf8(target) << "\n";
    return 0;
}

}  // namespace ampify::cli
