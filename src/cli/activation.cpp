// What the kit does to Apple Music: write what should load, turn the loading at each start on and off, start the app
// with the kernel, keep snapshots, and watch the folders while a theme or an extension is edited.

#include <chrono>
#include <thread>

#include "../core/launch_flags.hpp"
#include "backup.hpp"
#include "internal.hpp"
#include "kit.hpp"
#include "platform.hpp"
#include "watch.hpp"

namespace ampify::cli {

namespace {

constexpr auto kMinimumInterval = std::chrono::milliseconds(100);

}  // namespace

int ApplyNow(Context& ctx, const std::vector<std::string>& restarts) {
    Settings settings;
    std::string error;
    if (!LoadSettings(ctx, settings, error)) return Failure(ctx, error);

    ActiveReport report = ComputeActive(ctx.kit, settings);
    if (!WriteTextFile(ctx.kit.Active(), FormatActive(report), error)) return Failure(ctx, error);

    // The kernel and the interface watch this file to know that something changed: a counter line, then the restart
    // requests of ampify dev. Lines are added, so a request that the kernel has not read yet stays in the file.
    auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
    std::vector<std::string> requests = {std::to_string(stamp.count())};
    for (const std::string& target : restarts) requests.push_back("restart " + target);
    if (!AppendReloadRequests(ctx.kit, requests, error)) return Failure(ctx, error);

    for (const ActiveItem& item : report.items) {
        ctx.out << "  loads     " << item.kind << " " << item.id << " " << item.version;
        if (!item.scheme.empty()) ctx.out << " (scheme " << item.scheme << ")";
        ctx.out << "\n";
    }
    for (const std::string& problem : report.problems) ctx.err << "  ignored   " << problem << "\n";
    if (report.items.empty() && report.problems.empty()) ctx.out << "  nothing enabled\n";
    return report.problems.empty() ? 0 : 1;
}

void RequestReload(Context& ctx, const std::vector<std::string>& lines) {
    std::string error;
    if (!AppendReloadRequests(ctx.kit, lines, error)) ctx.err << "ampify: " << error << "\n";
}

namespace {

// apply turns the automatic start on, from the installed copy only: a build folder or an unzipped package must not
// register itself at sign-in. Returns 1 when it was asked for and failed.
int TurnAutoStartOn(Context& ctx) {
    platform::AutoStart state = platform::AutoStartState();
    if (!state.supported || ctx.options.dryRun) return 0;
    std::filesystem::path folder = platform::ExecutableDirectory();
    if (!platform::IsInstallDirectory(folder)) {
        ctx.out << "This ampify is not the installed one: loading with Apple Music stays "
                << (state.registered ? "on" : "off") << ".\n";
        return 0;
    }
    std::filesystem::path launcher = folder / "ampify_launcher.exe";
    std::error_code ec;
    if (!std::filesystem::is_regular_file(launcher, ec)) {
        return Failure(ctx, "ampify_launcher.exe not found next to ampify: " + Utf8(launcher));
    }
    std::string error;
    if (!platform::EnableAutoStart(launcher, error)) return Failure(ctx, error);
    ctx.out << "Ampify loads with Apple Music each time it starts (ampify restore turns it off).\n";
    return 0;
}

}  // namespace

int CmdApply(Context& ctx) {
    int code = ApplyNow(ctx);
    int started = TurnAutoStartOn(ctx);
    ctx.out << "Apple Music picks the changes up while it runs, or at its next start.\n";
    return code != 0 ? code : started;
}

int CmdLaunch(Context& ctx) {
    // A problem with one element is printed, and the app still starts: the kernel skips what cannot load.
    ApplyNow(ctx);

    std::filesystem::path launcher = platform::ExecutableDirectory() / "ampify_launcher.exe";
    // The launcher reads app_launch_flags itself and hands it to Apple Music. Shown here so the user sees what goes.
    Settings settings;
    settings.Load(ctx.kit.Settings().string());
    std::string flagsError;
    std::string flags = AppLaunchArguments(settings.GetString(std::string(kLaunchFlagsKey), ""), &flagsError);
    if (!flagsError.empty()) ctx.err << "ampify: " << flagsError << ": no argument will be passed to Apple Music\n";
    if (ctx.options.dryRun) {
        ctx.out << "Command: " << Utf8(launcher) << "\n";
        if (!flags.empty()) ctx.out << "Arguments for Apple Music (app_launch_flags): " << flags << "\n";
        return 0;
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(launcher, ec)) {
        return Failure(ctx, "ampify_launcher.exe not found next to ampify: " + Utf8(launcher));
    }
    if (!platform::StartDetached({Utf8(launcher)})) return Failure(ctx, "cannot start: " + Utf8(launcher));
    ctx.out << "Apple Music is starting with the kernel.\n";
    return 0;
}

int CmdRestore(Context& ctx) {
    const std::vector<std::string>& args = ctx.options.positional;
    if (args.size() > 1) return Usage(ctx, "restore takes --backup <name>, not an argument");

    if (ctx.options.backup) {
        std::string error;
        if (!RestoreBackup(ctx.kit, *ctx.options.backup, error)) return Failure(ctx, error);
        ctx.out << "Settings restored from backup " << *ctx.options.backup
                << ". The previous state is backed up too. Run ampify apply to apply them.\n";
        return 0;
    }

    // Ampify stops loading with Apple Music. Its files and settings stay: ampify apply turns it back on.
    if (!platform::AutoStartState().supported) {
        ctx.out << "Apple Music for Windows does not run on this system: nothing to turn off.\n";
        return 0;
    }
    if (ctx.options.dryRun) {
        ctx.out << "Would stop the background launcher and remove it from Windows sign-in.\n";
        return 0;
    }
    std::string error;
    if (!platform::DisableAutoStart(error)) return Failure(ctx, error);
    ctx.out << "Ampify no longer loads with Apple Music. If Apple Music is open, close it to finish."
               "\nNothing in Apple Music's package was changed. Run ampify apply to turn Ampify back on.\n";
    return 0;
}

int CmdBackup(Context& ctx) {
    const std::vector<std::string>& args = ctx.options.positional;
    if (args.size() == 2 && args[1] == "list") {
        std::vector<std::string> names = ListBackups(ctx.kit);
        if (names.empty()) ctx.out << "No backups.\n";
        for (const std::string& name : names) ctx.out << name << "\n";
        return 0;
    }
    if (args.size() != 1) return Usage(ctx, "backup takes 'list' or nothing");

    std::string error;
    std::optional<std::string> name = CreateBackup(ctx.kit, error);
    if (!name) return Failure(ctx, error);
    ctx.out << "Backup created: " << *name << " (" << Utf8(ctx.kit.Backups() / *name) << ")\n";
    return 0;
}

int CmdWatch(Context& ctx) {
    std::chrono::milliseconds interval(ctx.options.intervalMs);
    if (interval < kMinimumInterval) interval = kMinimumInterval;

    ctx.out << "ampify watch: watching " << Utf8(ctx.kit.Root()) << ". Press Ctrl+C to stop.\n" << std::flush;
    std::string last;
    int passes = 0;
    while (true) {
        std::string fingerprint = KitFingerprint(ctx.kit);
        if (fingerprint != last) {
            ctx.out << "[" << ClockText() << "] " << (last.empty() ? "initial apply" : "changes detected")
                    << "\n";
            last = fingerprint;
            ApplyNow(ctx);
            ctx.out << std::flush;
        }

        passes++;
        if (ctx.options.maxCycles > 0 && passes >= ctx.options.maxCycles) break;
        std::this_thread::sleep_for(interval);
    }
    return 0;
}

}  // namespace ampify::cli
