// `ampify report`: one zip with what helps to understand a problem (state, settings, logs, recent crashes), with the
// names of the person, the computer and the music taken out (anonymize.hpp), ready to attach to an issue.

#include <chrono>
#include <ctime>
#include <system_error>

#include "anonymize.hpp"
#include "internal.hpp"
#include "platform.hpp"

namespace ampify::cli {

namespace {

// A log longer than this is cut to its end, which is where a problem shows.
constexpr size_t kMaxLogBytes = 4 * 1024 * 1024;

constexpr std::string_view kReadme =
    "Ampify report\n"
    "=============\n\n"
    "Written by `ampify report` to help someone understand a problem with Ampify.\n\n"
    "Taken out before writing: your user and computer names, your profile folder, e-mail addresses,\n"
    "the titles, artists and albums of what you played, artwork addresses, library ids, and the values\n"
    "of settings that look like secrets (tokens, passwords, sessions, keys).\n\n"
    "Nothing is sent anywhere. Look through the files before you attach this zip to an issue: an\n"
    "extension may write things in the log that the masking does not recognise.\n\n"
    "system.txt       versions of Ampify, Windows and Apple Music, what is running\n"
    "doctor.txt       the output of `ampify doctor`\n"
    "settings.txt     the settings, secret-looking values hidden\n"
    "active.txt       the elements the kernel loads\n"
    "logs/            the logs of the kernel, the launcher and the installer\n"
    "crashes.txt      recent crashes of Apple Music or Ampify reported by Windows (last 14 days)\n"
    "inspector.txt    the last interface tree (only with --tree)\n";

std::string Stamp(const char* format) {
    std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char text[32];
    std::strftime(text, sizeof(text), format, &local);
    return text;
}

// The output of a program, or an empty text when it cannot run.
std::string Captured(const std::vector<std::string>& command) {
    std::filesystem::path file = platform::TemporaryFile("ampify-report");
    std::string text;
    if (platform::Run(command, file) == 0) ReadWholeFile(file, text);
    std::error_code ec;
    std::filesystem::remove(file, ec);
    return text;
}

std::string Tail(std::string text) {
    if (text.size() <= kMaxLogBytes) return text;
    size_t start = text.find('\n', text.size() - kMaxLogBytes);
    start = start == std::string::npos ? text.size() - kMaxLogBytes : start + 1;
    return "(beginning cut: the log is " + std::to_string(text.size()) + " bytes)\n" + text.substr(start);
}

#ifdef _WIN32
// PowerShell prints UTF-8 here, so French or German Windows texts keep their accents. Single quotes only: the
// command line is quoted by platform::Run.
constexpr std::string_view kWindowsVersion =
    "[Console]::OutputEncoding = [Text.Encoding]::UTF8; "
    "$v = Get-ItemProperty 'HKLM:/SOFTWARE/Microsoft/Windows NT/CurrentVersion'; "
    "$name = $v.ProductName; if ([int]$v.CurrentBuild -ge 22000) { $name = $name -replace 'Windows 10', 'Windows 11' }; "
    "'Windows: ' + $name + ' ' + $v.DisplayVersion + ', build ' + $v.CurrentBuild + '.' + $v.UBR; "
    "'Language: ' + (Get-Culture).Name + ', interface ' + (Get-UICulture).Name; "
    "foreach ($c in Get-CimInstance Win32_VideoController) { if ($c.CurrentHorizontalResolution) { "
    "'Display: ' + $c.CurrentHorizontalResolution + ' x ' + $c.CurrentVerticalResolution + ', ' + $c.Name } }; "
    "$dpi = (Get-ItemProperty 'HKCU:/Control Panel/Desktop/WindowMetrics' -ErrorAction SilentlyContinue).AppliedDPI; "
    "if ($dpi) { 'Scale: ' + [math]::Round($dpi * 100 / 96) + '%' }";

constexpr std::string_view kCrashes =
    "[Console]::OutputEncoding = [Text.Encoding]::UTF8; "
    "$events = Get-WinEvent -ErrorAction SilentlyContinue -FilterHashtable @{LogName = 'Application'; "
    "ProviderName = 'Application Error', 'Application Hang'; StartTime = (Get-Date).AddDays(-14)} | "
    "Where-Object { $_.Message -match 'AppleMusic|AMPLibraryAgent|ampify' } | Select-Object -First 30; "
    "if (-not $events) { 'No crash of Apple Music or Ampify in the last 14 days.' } "
    "foreach ($e in $events) { '== ' + $e.TimeCreated.ToString('yyyy-MM-dd HH:mm:ss') + ' ' + $e.ProviderName; "
    "$e.Message; '' }";

std::string PowerShell(std::string_view script) {
    return Captured({"powershell.exe", "-NoProfile", "-NonInteractive", "-Command", std::string(script)});
}
#endif

std::string SystemText(const Context& ctx) {
    std::string text = "Ampify " AMPIFY_VERSION "\n";
    text += "Report written: " + Stamp("%Y-%m-%d %H:%M:%S") + "\n";
    text += "Ampify folder: " + Utf8(ctx.kit.Root()) + "\n";
    text += "ampify.exe folder: " + Utf8(platform::ExecutableDirectory()) + "\n";
#ifdef _WIN32
    text += PowerShell(kWindowsVersion);
#endif
    std::optional<std::filesystem::path> package = platform::AppPackageFolder();
    text += "Apple Music package: " + (package ? Utf8(package->filename()) : std::string("not found")) + "\n";
    for (const char* process : {"AppleMusic.exe", "AMPLibraryAgent.exe", "ampify_launcher.exe"}) {
        text += std::string(process) + ": " + (platform::IsProcessRunning(process) ? "running" : "not running") + "\n";
    }
    platform::AutoStart autoStart = platform::AutoStartState();
    if (autoStart.supported) {
        text += std::string("Start with Apple Music: ") + (autoStart.registered ? "on" : "off") +
                (autoStart.running ? ", background launcher running" : ", background launcher not running") + "\n";
    }
    return text;
}

std::string DoctorText(Context& ctx) {
    Options options = ctx.options;
    options.positional = {"doctor"};
    std::ostringstream out;
    Context doctor{options, ctx.kit, ctx.in, out, out};
    CmdDoctor(doctor);
    return out.str();
}

std::filesystem::path DefaultArchive(const Context& ctx) {
    return ctx.kit.Root() / "reports" / ("ampify-report-" + Stamp("%Y%m%d-%H%M%S") + ".zip");
}

}  // namespace

int CmdReport(Context& ctx) {
    const std::vector<std::string>& args = ctx.options.positional;
    if (args.size() > 2) return Usage(ctx, "report takes at most the .zip file to write");

    std::filesystem::path archive = args.size() == 2 ? PathArgument(args[1]) : DefaultArchive(ctx);
    std::error_code ec;
    if (std::filesystem::exists(archive, ec)) return Failure(ctx, "the file already exists: " + Utf8(archive));
    if (archive.has_parent_path()) std::filesystem::create_directories(archive.parent_path(), ec);

    Identity who = CurrentIdentity();
    std::vector<ZipSource> files;
    auto add = [&](const std::string& name, const std::string& text) { files.push_back({name, {}, Anonymize(text, who)}); };
    auto addFile = [&](const std::string& name, const std::filesystem::path& file, bool secrets) {
        std::string text;
        if (!ReadWholeFile(file, text)) return;
        add(name, secrets ? HideSecretSettings(text) : Tail(std::move(text)));
    };

    files.push_back({"README.txt", {}, std::string(kReadme)});
    add("system.txt", SystemText(ctx));
    add("doctor.txt", DoctorText(ctx));
    addFile("settings.txt", ctx.kit.Settings(), true);
    addFile("active.txt", ctx.kit.Active(), false);
    addFile("state.txt", ctx.kit.SafeModeState(), true);
    addFile("versions.txt", ctx.kit.Root() / "versions.txt", false);
    for (const std::filesystem::path& log : JournalFiles(ctx.kit)) addFile("logs/" + Utf8(log.filename()), log, false);
#ifdef _WIN32
    add("crashes.txt", PowerShell(kCrashes));
#endif
    if (ctx.options.tree) addFile("inspector.txt", ctx.kit.Journal() / "inspector.txt", false);

    std::string error;
    if (!WriteZip(archive, files, error)) return Failure(ctx, error);
    ctx.out << "Report written: " << Utf8(archive) << " (" << files.size() << " file(s)).\n"
            << "Names, folders, titles and secrets are masked. Look through it before you attach it to an issue.\n";
    if (ctx.options.dir) platform::StartDetached(platform::OpenFolderCommand(archive.parent_path()));
    return 0;
}

}  // namespace ampify::cli
