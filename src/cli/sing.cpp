// `ampify sing install | status | remove`: the files the studio voice remover needs (src/core/sing_pack.hpp), kept in
// the kit's `sing` folder. The audio process runs `install` when Sing's studio mode is first asked for.

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

// A stalled connection gives up after this; 67 MB still come in over 0.3 Mbit/s.
constexpr int kDownloadLimitSeconds = 30 * 60;

bool PackReady(const fs::path& folder) { return sing_pack::Ready(folder); }

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

int SingInstall(Context& ctx) {
    const fs::path folder = SingFolder(ctx);
    if (PackReady(folder)) {
        ctx.out << "Sing's studio files are already in " << Utf8(folder) << "\n";
        return 0;
    }
    fs::path wheel, model;
    std::string error = Fetch(ctx, sing_pack::kRuntime, wheel);
    if (error.empty()) error = Fetch(ctx, sing_pack::kModel, model);
    if (!error.empty()) return Failure(ctx, error);

    const fs::path staging = ctx.kit.Root() / ".sing.staging";
    RemoveQuietly(staging);
    std::error_code ec;
    fs::create_directories(staging, ec);
    error = UnpackRuntime(wheel, staging);
    if (error.empty()) {
        fs::copy_file(model, staging / std::string(sing_pack::kModelFile), fs::copy_options::overwrite_existing, ec);
        if (ec) error = ec.message();
    }
    if (error.empty()) {
        std::ofstream(staging / std::string(sing_pack::kReadyFile)) << sing_pack::kReadyText << "\n";
        RemoveQuietly(folder);
        fs::rename(staging, folder, ec);
        if (ec) error = ec.message();
    }
    RemoveQuietly(staging);
    RemoveQuietly(wheel);
    RemoveQuietly(model);
    if (!error.empty()) return Failure(ctx, error);
    ctx.out << "Sing's studio files installed in " << Utf8(folder) << "\n";
    return 0;
}

}  // namespace

int CmdSing(Context& ctx) {
    const std::vector<std::string>& args = ctx.options.positional;
    const std::string what = args.size() > 1 ? args[1] : "status";
    if (what == "install") return SingInstall(ctx);
    if (what == "remove") {
        RemoveQuietly(SingFolder(ctx));
        ctx.out << "Sing's studio files removed\n";
        return 0;
    }
    if (what == "status") {
        ctx.out << (PackReady(SingFolder(ctx)) ? "installed" : "not installed") << "\n";
        return 0;
    }
    return Usage(ctx, "ampify sing install | status | remove");
}

}  // namespace ampify::cli
