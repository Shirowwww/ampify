// `ampify sing install | status [standard | precise] | remove`: the files the studio voice remover needs for one of its
// models (src/core/sing_pack.hpp), kept in the kit's `sing` folder. The audio process runs `install` when studio mode
// is first asked for with that model.

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "../core/sing_pack.hpp"
#include "archive.hpp"
#include "internal.hpp"
#include "kit.hpp"
#include "platform.hpp"
#include "sha256.hpp"

namespace ampify::cli {

namespace {

namespace fs = std::filesystem;

fs::path SingFolder(const Context& ctx) { return ctx.kit.Root() / "sing"; }

// A stalled connection gives up after this; the precise model still comes in over 2.1 Mbit/s.
constexpr int kDownloadLimitSeconds = 30 * 60;

void RemoveQuietly(const fs::path& path) {
    std::error_code ec;
    fs::remove_all(path, ec);
}

// Downloads one file to the downloads folder and checks its size and SHA-256. Empty on success, else why not.
std::string Fetch(const Context& ctx, const sing_pack::Download& item, fs::path& file) {
    std::error_code ec;
    fs::create_directories(ctx.kit.Downloads(), ec);
    file = ctx.kit.Downloads() / std::string(item.name);
    fs::path part = file;
    part += ".part";
    RemoveQuietly(part);
    std::string error;
    if (!platform::DownloadHttps(std::string(item.url), part, error, kDownloadLimitSeconds)) return error;
    const std::uintmax_t size = fs::file_size(part, ec);
    const std::optional<std::string> digest = Sha256File(part);
    if (ec || size != item.size || !digest || *digest != item.sha256) {
        RemoveQuietly(part);
        return std::string(item.name) + ": size or SHA-256 differs from the expected one, file refused";
    }
    RemoveQuietly(file);
    fs::rename(part, file, ec);
    return ec ? ec.message() : std::string();
}

// The libraries of the wheel, copied into `staging`.
std::string UnpackRuntime(const fs::path& wheel, const fs::path& staging) {
    const fs::path unpacked = staging / ".wheel";
    std::error_code ec;
    fs::create_directories(unpacked, ec);
    std::string error;
    if (!platform::ExtractZip(wheel, unpacked, error)) return error;
    if (ContainsLink(unpacked)) return "symbolic link not allowed in the wheel";
    for (std::string_view entry : sing_pack::kRuntimeEntries) {
        const fs::path from = unpacked / fs::path(std::string(entry));
        fs::copy_file(from, staging / from.filename(), fs::copy_options::overwrite_existing, ec);
        if (ec) return std::string(entry) + ": " + ec.message();
    }
    RemoveQuietly(unpacked);
    return {};
}

// Files of older packs, from before each part had its own marker.
constexpr std::string_view kOldFiles[] = {"ready.txt", "vocals.onnx"};

// The runtime, unless it is in place: the wheel's libraries into `folder`, then its marker.
std::string InstallRuntime(const Context& ctx, const fs::path& folder) {
    if (sing_pack::RuntimeReady(folder)) return {};
    fs::path wheel;
    std::string error = Fetch(ctx, sing_pack::kRuntime, wheel);
    if (!error.empty()) return error;
    const fs::path staging = ctx.kit.Root() / ".sing.staging";
    RemoveQuietly(staging);
    std::error_code ec;
    fs::create_directories(staging, ec);
    error = UnpackRuntime(wheel, staging);
    RemoveQuietly(folder / std::string(sing_pack::kRuntimeMarker));
    for (std::string_view entry : sing_pack::kRuntimeEntries) {
        if (!error.empty()) break;
        const fs::path name = fs::path(std::string(entry)).filename();
        fs::copy_file(staging / name, folder / name, fs::copy_options::overwrite_existing, ec);
        if (ec) error = Utf8(name) + ": " + ec.message();
    }
    if (error.empty()) {
        std::ofstream(folder / std::string(sing_pack::kRuntimeMarker)) << sing_pack::kRuntime.sha256 << "\n";
    }
    RemoveQuietly(staging);
    RemoveQuietly(wheel);
    return error;
}

// The model's file, unless it is in place, then its marker.
std::string InstallModel(const Context& ctx, const fs::path& folder, const sing_pack::Model& model) {
    if (sing_pack::ModelReady(folder, model)) return {};
    fs::path file;
    std::string error = Fetch(ctx, model.download, file);
    if (error.empty()) {
        RemoveQuietly(folder / std::string(model.marker));
        std::error_code ec;
        fs::copy_file(file, folder / std::string(model.file), fs::copy_options::overwrite_existing, ec);
        if (ec) error = std::string(model.file) + ": " + ec.message();
        else std::ofstream(folder / std::string(model.marker)) << model.download.sha256 << "\n";
    }
    RemoveQuietly(file);
    return error;
}

int SingInstall(Context& ctx, const sing_pack::Model& model) {
    const fs::path folder = SingFolder(ctx);
    if (sing_pack::Ready(folder, model)) {
        ctx.out << "Sing's " << model.id << " files are already in " << Utf8(folder) << "\n";
        return 0;
    }
    std::error_code ec;
    fs::create_directories(folder, ec);
    for (std::string_view old : kOldFiles) RemoveQuietly(folder / std::string(old));
    std::string error = InstallRuntime(ctx, folder);
    if (error.empty()) error = InstallModel(ctx, folder, model);
    if (!error.empty()) return Failure(ctx, error);
    ctx.out << "Sing's " << model.id << " files installed in " << Utf8(folder) << "\n";
    return 0;
}

}  // namespace

constexpr const char* kUsage = "ampify sing install | status [standard | precise] | remove";

int CmdSing(Context& ctx) {
    const std::vector<std::string>& args = ctx.options.positional;
    const std::string what = args.size() > 1 ? args[1] : "status";
    const sing_pack::Model* model = sing_pack::FindModel(args.size() > 2 ? args[2] : "standard");
    if (!model) return Usage(ctx, kUsage);
    if (what == "install") return SingInstall(ctx, *model);
    if (what == "remove") {
        RemoveQuietly(SingFolder(ctx));
        ctx.out << "Sing's studio files removed\n";
        return 0;
    }
    if (what == "status") {
        ctx.out << (sing_pack::Ready(SingFolder(ctx), *model) ? "installed" : "not installed") << "\n";
        return 0;
    }
    return Usage(ctx, kUsage);
}

}  // namespace ampify::cli
