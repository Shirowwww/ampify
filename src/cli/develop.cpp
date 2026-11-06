// `ampify dev` copies an element that is being written into the kit at every save. `ampify logs` prints the log.

#include <algorithm>
#include <charconv>
#include <chrono>
#include <fstream>
#include <map>
#include <optional>
#include <system_error>
#include <thread>

#include "archive.hpp"
#include "backup.hpp"
#include "compile.hpp"
#include "element.hpp"
#include "internal.hpp"
#include "watch.hpp"

namespace ampify::cli {

namespace {

constexpr auto kMinimumInterval = std::chrono::milliseconds(100);
constexpr int kDefaultLines = 50;

// Copies the author's folder into the kit. The copy is made beside the old one and then swapped in, so the kernel
// never sees an element that is half copied. The dot prefix keeps the staging folder out of `ampify list`.
bool SyncElement(const KitLayout& kit, const std::filesystem::path& source, const std::string& kind,
                 const std::string& id, std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(kit.Folder(kind), ec);
    std::filesystem::path staging = kit.Folder(kind) / (".dev-" + id);
    std::filesystem::path target = kit.Element(kind, id);

    std::filesystem::remove_all(staging, ec);
    if (!CopyElementFolder(source, staging, error)) {
        std::filesystem::remove_all(staging, ec);
        return false;
    }
    std::filesystem::remove_all(target, ec);
    if (ec) {
        error = "cannot replace in Ampify: " + id;
        return false;
    }
    std::filesystem::rename(staging, target, ec);
    if (ec) {
        error = "cannot put in place in Ampify: " + id;
        return false;
    }
    return true;
}

// One reload: check the folder, copy it into the kit, then write active.txt so the kernel picks it up.
// A broken edit is reported and the copy that was working stays in the kit.
void ReloadOnce(Context& ctx, const std::filesystem::path& source, const std::string& kind, const std::string& id) {
    std::string stamp = "[" + ClockText() + "] ";
    std::string compiled;
    CompileOutcome compile = CompileElementSources(source, compiled);
    if (compile == CompileOutcome::Failed) {
        ctx.err << stamp << compiled << "; Ampify keeps the previous version\n";
        return;
    }
    if (compile == CompileOutcome::Compiled) ctx.out << stamp << compiled << "\n";
    FolderValidation check = ValidateElementFolder(source);
    if (!check.ok() || !check.errors.empty() || !check.manifest) {
        ctx.err << stamp << "invalid, Ampify keeps the previous version:\n";
        for (const std::string& problem : check.errors) ctx.err << "  - " << problem << "\n";
        return;
    }
    if (check.manifest->id != id || check.manifest->kind != kind) {
        ctx.err << stamp << "the id or the type changed: run ampify dev again\n";
        return;
    }

    std::string error;
    if (!SyncElement(ctx.kit, source, kind, id, error)) {
        ctx.err << stamp << error << "\n";
        return;
    }
    ctx.out << stamp << id << " reloaded\n";
    // The kernel restarts the element on its JavaScript thread, from this request in reload.txt.
    ApplyNow(ctx, {ReloadTarget(kind, id)});
}

bool ParseCount(std::string_view text, int& value) {
    auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    return ec == std::errc() && end == text.data() + text.size() && value > 0;
}

}  // namespace

std::vector<std::filesystem::path> JournalFiles(const KitLayout& kit) {
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    if (!std::filesystem::is_directory(kit.Journal(), ec)) return files;
    for (const auto& entry : std::filesystem::directory_iterator(kit.Journal(), ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".log") files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    return files;
}

namespace {

// Prints what `file` holds from byte `from` on, or only its last `tail` lines when `tail` is not 0. Returns the
// size the file has now, which is where the next call starts.
std::uintmax_t PrintFrom(Context& ctx, const std::filesystem::path& file, std::uintmax_t from, int tail) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return from;
    in.seekg(static_cast<std::streamoff>(from));

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    size_t begin = (tail > 0 && lines.size() > static_cast<size_t>(tail)) ? lines.size() - static_cast<size_t>(tail) : 0;
    for (size_t i = begin; i < lines.size(); i++) ctx.out << lines[i] << "\n";

    std::error_code ec;
    std::uintmax_t size = std::filesystem::file_size(file, ec);
    return ec ? from : size;
}

}  // namespace

int CmdDev(Context& ctx) {
    const std::vector<std::string>& args = ctx.options.positional;
    if (args.size() != 2) return Usage(ctx, "dev needs the folder of the element to develop");

    std::filesystem::path source = Utf8ToPath(args[1]);
    std::string compiled;
    CompileOutcome compile = CompileElementSources(source, compiled);
    if (compile == CompileOutcome::Failed) return Failure(ctx, compiled);
    if (compile == CompileOutcome::Compiled) ctx.out << compiled << "\n";
    FolderValidation first = ValidateElementFolder(source);
    if (!first.ok() || !first.errors.empty() || !first.manifest) {
        ctx.err << Utf8(source) << ": invalid, nothing was copied\n";
        for (const std::string& problem : first.errors) ctx.err << "  - " << problem << "\n";
        return 1;
    }
    const std::string kind = first.manifest->kind;
    const std::string id = first.manifest->id;

    // Every reload replaces the copy in the kit, so the copy that is there now is saved first.
    std::error_code ec;
    if (std::filesystem::exists(ctx.kit.Element(kind, id), ec)) {
        std::string error;
        std::optional<std::string> snapshot = CreateBackup(ctx.kit, error);
        if (!snapshot) return Failure(ctx, error);
        ctx.out << "Ampify version backed up before development: " << *snapshot << "\n";
    }

    Settings settings;
    std::string error;
    if (!LoadSettings(ctx, settings, error)) return Failure(ctx, error);
    SetEnabled(settings, kind, id, true);
    if (!SaveSettings(ctx, settings, error)) return Failure(ctx, error);

    std::chrono::milliseconds interval(ctx.options.intervalMs);
    if (interval < kMinimumInterval) interval = kMinimumInterval;
    ctx.out << "ampify dev: " << id << " (" << kind << ") watched in " << Utf8(source) << ". Press Ctrl+C to stop.\n"
            << std::flush;

    std::string last;
    int passes = 0;
    while (true) {
        std::string fingerprint = FolderFingerprint(source);
        if (fingerprint != last) {
            last = fingerprint;
            ReloadOnce(ctx, source, kind, id);
            ctx.out << std::flush;
        }
        passes++;
        if (ctx.options.maxCycles > 0 && passes >= ctx.options.maxCycles) break;
        std::this_thread::sleep_for(interval);
    }
    return 0;
}

int CmdLogs(Context& ctx) {
    const std::vector<std::string>& args = ctx.options.positional;
    if (args.size() > 2) return Usage(ctx, "logs takes a number of lines, and --follow to keep following");
    int lines = kDefaultLines;
    if (args.size() == 2 && !ParseCount(args[1], lines)) return Usage(ctx, "invalid number of lines: " + args[1]);

    std::chrono::milliseconds interval(ctx.options.intervalMs);
    if (interval < kMinimumInterval) interval = kMinimumInterval;

    // Bytes already printed, per file. With --follow, only what is added after that is printed.
    std::map<std::filesystem::path, std::uintmax_t> printed;
    int passes = 0;
    while (true) {
        std::vector<std::filesystem::path> files = JournalFiles(ctx.kit);
        if (files.empty() && passes == 0) ctx.out << "No log yet: the kernel has not been started.\n";

        for (const std::filesystem::path& file : files) {
            std::error_code ec;
            std::uintmax_t size = std::filesystem::file_size(file, ec);
            if (ec) continue;

            auto known = printed.find(file);
            if (known == printed.end()) {
                if (files.size() > 1) ctx.out << "== " << Utf8(file.filename()) << " ==\n";
                printed[file] = PrintFrom(ctx, file, 0, lines);
            } else if (size != known->second) {
                // A shorter file was reopened by the launcher, so it is printed again from the start.
                std::uintmax_t from = size < known->second ? 0 : known->second;
                known->second = PrintFrom(ctx, file, from, 0);
            }
        }
        ctx.out << std::flush;

        if (!ctx.options.follow) return 0;
        passes++;
        if (ctx.options.maxCycles > 0 && passes >= ctx.options.maxCycles) return 0;
        std::this_thread::sleep_for(interval);
    }
}

}  // namespace ampify::cli
