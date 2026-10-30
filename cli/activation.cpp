// What the kit does to Apple Music: write what should load, start the app with the kernel, start it without,
// keep snapshots, and watch the folders while a theme or an extension is edited.

#include <chrono>
#include <thread>

#include "../core/apple_music.hpp"
#include "backup.hpp"
#include "internal.hpp"
#include "kit.hpp"
#include "platform.hpp"
#include "watch.hpp"

namespace amk::cli {

namespace {

constexpr auto kMinimumInterval = std::chrono::milliseconds(100);

}  // namespace

int ApplyNow(Context& ctx, const std::vector<std::string>& restarts) {
    Settings settings;
    std::string error;
    if (!LoadSettings(ctx, settings, error)) return Failure(ctx, error);

    ActiveReport report = ComputeActive(ctx.kit, settings);
    if (!WriteTextFile(ctx.kit.Active(), FormatActive(report), error)) return Failure(ctx, error);

    // The kernel and the interface watch this file to know that something changed. Its content is only a counter.
    auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
    std::string requests = std::to_string(stamp.count()) + "\n";
    for (const std::string& id : restarts) requests += "restart " + id + "\n";
    if (!WriteTextFile(ctx.kit.Reload(), requests, error)) return Failure(ctx, error);

    for (const ActiveItem& item : report.items) {
        ctx.out << "  charge    " << item.kind << " " << item.id << " " << item.version;
        if (!item.scheme.empty()) ctx.out << " (schéma " << item.scheme << ")";
        ctx.out << "\n";
    }
    for (const std::string& problem : report.problems) ctx.err << "  ignoré    " << problem << "\n";
    if (report.items.empty() && report.problems.empty()) ctx.out << "  rien d'activé\n";
    return report.problems.empty() ? 0 : 1;
}

int CmdApply(Context& ctx) {
    int code = ApplyNow(ctx);
    ctx.out << "Apple Music lit ces réglages au démarrage : relance-le avec amk launch pour les appliquer.\n";
    return code;
}

int CmdLaunch(Context& ctx) {
    // A problem with one element is printed, and the app still starts: the kernel skips what cannot load.
    ApplyNow(ctx);

    std::filesystem::path launcher = platform::ExecutableDirectory() / "amk_launcher.exe";
    if (ctx.options.dryRun) {
        ctx.out << "Commande : " << Utf8(launcher) << "\n";
        return 0;
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(launcher, ec)) {
        return Failure(ctx, "amk_launcher.exe introuvable à côté d'amk : " + Utf8(launcher));
    }
    if (!platform::StartDetached({Utf8(launcher)})) return Failure(ctx, "lancement impossible : " + Utf8(launcher));
    ctx.out << "Apple Music démarre avec le noyau. Le lanceur reste ouvert tant que l'app tourne.\n";
    return 0;
}

int CmdRestore(Context& ctx) {
    const std::vector<std::string>& args = ctx.options.positional;
    if (args.size() > 1) return Usage(ctx, "restore prend --backup <nom>, pas un argument");

    if (ctx.options.backup) {
        std::string error;
        if (!RestoreBackup(ctx.kit, *ctx.options.backup, error)) return Failure(ctx, error);
        ctx.out << "Réglages remis depuis la sauvegarde " << *ctx.options.backup
                << ". L'état précédent est sauvegardé aussi. Lance amk apply pour les appliquer.\n";
        return 0;
    }

    std::string target = "shell:AppsFolder\\" + std::string(kAppUserModelId);
    if (ctx.options.dryRun) {
        ctx.out << "Commande : explorer.exe " << target << "\n";
        return 0;
    }
    if (!platform::StartDetached({"explorer.exe", target})) return Failure(ctx, "lancement impossible");
    ctx.out << "Apple Music démarre sans le noyau. Rien n'a été modifié dans le paquet.\n";
    return 0;
}

int CmdBackup(Context& ctx) {
    const std::vector<std::string>& args = ctx.options.positional;
    if (args.size() == 2 && args[1] == "list") {
        std::vector<std::string> names = ListBackups(ctx.kit);
        if (names.empty()) ctx.out << "Aucune sauvegarde.\n";
        for (const std::string& name : names) ctx.out << name << "\n";
        return 0;
    }
    if (args.size() != 1) return Usage(ctx, "backup prend 'list' ou rien");

    std::string error;
    std::optional<std::string> name = CreateBackup(ctx.kit, error);
    if (!name) return Failure(ctx, error);
    ctx.out << "Sauvegarde créée : " << *name << " (" << Utf8(ctx.kit.Backups() / *name) << ")\n";
    return 0;
}

int CmdWatch(Context& ctx) {
    std::chrono::milliseconds interval(ctx.options.intervalMs);
    if (interval < kMinimumInterval) interval = kMinimumInterval;

    ctx.out << "amk watch : surveillance de " << Utf8(ctx.kit.Root()) << ". Ctrl+C pour arrêter.\n" << std::flush;
    std::string last;
    int passes = 0;
    while (true) {
        std::string fingerprint = KitFingerprint(ctx.kit);
        if (fingerprint != last) {
            ctx.out << "[" << ClockText() << "] " << (last.empty() ? "application initiale" : "modifications détectées")
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

}  // namespace amk::cli
