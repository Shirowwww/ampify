// ampify_setup.exe: installs Ampify and uninstalls it. The same file is copied into the install folder as the uninstaller.
//
//   ampify_setup.exe                          install, from its window
//   ampify_setup.exe /silent                  install without any dialog (tests, scripts)
//   ampify_setup.exe /uninstall               uninstall; its window asks whether to delete the user's data too (No)
//   ampify_setup.exe /uninstall /keepdata     uninstall and keep settings, logs and reports
//   ampify_setup.exe /uninstall /purgedata    uninstall and delete the user's data folder too (ticked in the window)
//   add /silent to skip the window (without a data choice, the data is kept); `ampify uninstall` runs these
//
// Exit codes: 0 done (or cancelled by the user), 1 error, 3 refused (Apple Music is open and could not or may not be closed).
//
// Ampify never writes into Apple Music's package folder. Installing copies the kit's files, puts the elements that ship
// with it (the Marketplace, Now Playing) in the user's Ampify folder, writes an uninstall entry, and starts the
// background launcher, now and at each Windows sign-in: from then on Apple Music starts with Ampify from its usual
// icon (src/win/background.hpp). Uninstalling stops it and removes all of it, so Apple Music starts as it did before.

#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <shellapi.h>
#include <exdisp.h>
#include <servprov.h>
#include <shldisp.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "../core/apple_music.hpp"
#include "../core/i18n.hpp"
#include "../core/journal.hpp"
#include "../core/kit_layout.hpp"
#include "../core/version_gate.hpp"
#include "../core/version_list.hpp"
#include "../win/background.hpp"
#include "../win/close_to_tray.hpp"
#include "../win/package.hpp"
#include "../win/win_common.hpp"
#include "setup_window.hpp"

#undef ShellExecute  // shellapi.h renames it to ShellExecuteW, which IShellDispatch2 does not have

namespace fs = std::filesystem;

namespace {

constexpr int kExitDone = 0;
constexpr int kExitError = 1;
constexpr int kExitRefused = 3;

constexpr DWORD kQuitWaitMs = 10000;       // Apple Music's time to quit by itself before it is ended
constexpr DWORD kEndWaitMs = 3000;         // time left to the ended processes to disappear
constexpr DWORD kPollMs = 200;
constexpr DWORD kLauncherWaitMs = 3000;    // for the launcher to watch for Apple Music before Apple Music opens
constexpr DWORD kInUseWaitMs = 10000;     // for a file of Ampify still in use (ampify.exe closing) to be freed

// The text in the display language, English for a language Ampify does not speak, with its placeholders filled.
std::wstring Tr(const char* key, ampify::i18n::Args args = {}) {
    return ampify::win::FromUtf8(ampify::i18n::Text(key, args));
}

bool HasFlag(int argc, wchar_t** argv, const wchar_t* flag) {
    for (int i = 1; i < argc; i++) {
        if (_wcsicmp(argv[i], flag) == 0) return true;
    }
    return false;
}

// Writes the log to a file. An empty path keeps it in memory: used when the data folder is about to be deleted,
// so the log does not survive in a file of its own.
class Log {
public:
    explicit Log(const fs::path& file)
        : m_file(file, std::ios::trunc),
          m_out(file.empty() ? static_cast<std::ostream*>(&m_memory) : &m_file),
          m_journal(*m_out) {}

    void Write(const std::string& line) { m_journal.Write(line); }

private:
    std::ofstream m_file;
    std::ostringstream m_memory;
    std::ostream* m_out;
    ampify::Journal m_journal;
};

// Versions up to 1.0.2 started Apple Music from their own Start menu shortcut. Removed on install and uninstall.
void RemoveOldShortcut(Log& log) {
    fs::path shortcut = ampify::win::KnownFolder(FOLDERID_Programs) / ampify::win::FromUtf8(ampify::kShortcutFile);
    std::error_code error;
    if (fs::remove(shortcut, error)) log.Write("Old Start menu shortcut removed");
}

void SetRegistryString(HKEY key, const wchar_t* name, const std::wstring& value) {
    RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                   static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
}

void SetRegistryDword(HKEY key, const wchar_t* name, DWORD value) {
    RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
}

// Reads the user's Path. Absent, it is empty and `type` is REG_EXPAND_SZ, the type Windows gives it. False when it
// cannot be read whole, so nothing is ever written from a partial value.
bool ReadUserPath(HKEY key, std::wstring& path, DWORD& type) {
    DWORD size = 0;
    LSTATUS status = RegQueryValueExW(key, L"Path", nullptr, &type, nullptr, &size);
    if (status == ERROR_FILE_NOT_FOUND) {
        type = REG_EXPAND_SZ;
        path.clear();
        return true;
    }
    if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) return false;
    path.assign(size / sizeof(wchar_t) + 1, L'\0');
    DWORD read = static_cast<DWORD>(path.size() * sizeof(wchar_t));
    if (RegQueryValueExW(key, L"Path", nullptr, &type, reinterpret_cast<BYTE*>(path.data()), &read) != ERROR_SUCCESS) {
        return false;
    }
    path.resize(wcsnlen(path.c_str(), path.size()));
    return true;
}

// Puts the install folder in the user's Path (HKCU\Environment), or takes it out, so `ampify` works in any new
// terminal. The value keeps its type, so %VARIABLES% in it stay unexpanded.
bool UpdateUserPath(const fs::path& folder, bool add) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Environment", 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }
    std::wstring current;
    DWORD type = REG_EXPAND_SZ;
    if (!ReadUserPath(key, current, type)) {
        RegCloseKey(key);
        return false;
    }
    std::string before = ampify::win::ToUtf8(current);
    std::string folderText = ampify::win::ToUtf8(folder.wstring());
    std::string after = add ? ampify::PathWithFolder(before, folderText) : ampify::PathWithoutFolder(before, folderText);
    bool ok = true;
    if (after != before) {
        std::wstring value = ampify::win::FromUtf8(after);
        ok = value.empty() ? RegDeleteValueW(key, L"Path") == ERROR_SUCCESS
                           : RegSetValueExW(key, L"Path", 0, type, reinterpret_cast<const BYTE*>(value.c_str()),
                                            static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
        // Tells Explorer, so terminals opened from now on see the new Path.
        DWORD_PTR ignored = 0;
        SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"Environment"),
                            SMTO_ABORTIFHUNG, 2000, &ignored);
    }
    RegCloseKey(key);
    return ok;
}

// Both the app and its library agent: the kernel is loaded in each.
std::vector<DWORD> AppleMusicProcesses() {
    std::vector<DWORD> ids = ampify::win::ProcessIdsNamed(ampify::kAppleMusicImageW);
    std::vector<DWORD> agent = ampify::win::ProcessIdsNamed(ampify::kLibraryAgentImageW);
    ids.insert(ids.end(), agent.begin(), agent.end());
    return ids;
}

bool AppleMusicRunning() { return !AppleMusicProcesses().empty(); }

bool IsOneOf(const std::vector<DWORD>& ids, HWND window) {
    DWORD owner = 0;
    GetWindowThreadProcessId(window, &owner);
    return std::find(ids.begin(), ids.end(), owner) != ids.end();
}

BOOL CALLBACK PostCloseToWindow(HWND window, LPARAM param) {
    if (IsOneOf(*reinterpret_cast<const std::vector<DWORD>*>(param), window)) PostMessageW(window, WM_CLOSE, 0, 0);
    return TRUE;
}

// Quits Apple Music the way its own menu does, then ends what is left. A window hidden in the notification area ignores
// WM_CLOSE, so it gets the tray's quit message too (only newer Ampify kernels know it; with an older one Apple Music is ended after the wait).
bool CloseAppleMusic(Log& log) {
    std::vector<DWORD> ids = AppleMusicProcesses();
    HWND tray = FindWindowW(ampify::win::kCloseToTrayWindowClass, nullptr);
    if (tray && IsOneOf(ids, tray)) PostMessageW(tray, ampify::win::kCloseToTrayQuitMessage, 0, 0);
    EnumWindows(PostCloseToWindow, reinterpret_cast<LPARAM>(&ids));

    ULONGLONG deadline = GetTickCount64() + kQuitWaitMs;
    while (AppleMusicRunning() && GetTickCount64() < deadline) Sleep(kPollMs);
    if (!AppleMusicRunning()) {
        log.Write("Apple Music closed by itself");
        return true;
    }

    log.Write("Apple Music did not close in time: ending it");
    for (DWORD id : AppleMusicProcesses()) {
        HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, id);
        if (!process) {
            log.Write("Could not open process " + std::to_string(id) + " (error " + std::to_string(GetLastError()) + ")");
            continue;
        }
        if (!TerminateProcess(process, 1)) {
            log.Write("Could not end process " + std::to_string(id) + " (error " + std::to_string(GetLastError()) + ")");
        }
        WaitForSingleObject(process, kEndWaitMs);
        CloseHandle(process);
    }
    return !AppleMusicRunning();
}

void LogComFailure(Log& log, const char* step, HRESULT result) {
    char code[16];
    std::snprintf(code, sizeof(code), "0x%08lX", static_cast<unsigned long>(result));
    log.Write(std::string("Starting through Explorer failed at ") + step + " (" + code + ")");
}

VARIANT TextVariant(const std::wstring& text) {
    VARIANT value;
    VariantInit(&value);
    if (!text.empty()) {
        value.vt = VT_BSTR;
        value.bstrVal = SysAllocString(text.c_str());
    }
    return value;
}

// Starts `file` through the desktop shell (Explorer), so it runs with Explorer's normal rights even when this process
// is an administrator one. Explorer's own COM objects do the launch: desktop window -> view -> IShellDispatch2.
bool ShellExecuteUnelevated(const std::wstring& file, const std::wstring& parameters, const std::wstring& folder,
                            int show, Log& log) {
    using Microsoft::WRL::ComPtr;
    ComPtr<IShellWindows> windows;
    HRESULT result = CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&windows));
    if (FAILED(result)) return LogComFailure(log, "ShellWindows", result), false;

    VARIANT location;
    VariantInit(&location);
    location.vt = VT_I4;
    location.lVal = CSIDL_DESKTOP;
    VARIANT root;
    VariantInit(&root);
    long desktopWindow = 0;
    ComPtr<IDispatch> desktop;
    result = windows->FindWindowSW(&location, &root, SWC_DESKTOP, &desktopWindow, SWFO_NEEDDISPATCH, &desktop);
    if (FAILED(result) || !desktop) return LogComFailure(log, "FindWindowSW", FAILED(result) ? result : E_FAIL), false;

    ComPtr<IServiceProvider> services;
    ComPtr<IShellBrowser> browser;
    ComPtr<IShellView> view;
    ComPtr<IDispatch> background;
    ComPtr<IShellFolderViewDual> folderView;
    ComPtr<IDispatch> application;
    ComPtr<IShellDispatch2> shell;
    if (FAILED(result = desktop.As(&services))) return LogComFailure(log, "IServiceProvider", result), false;
    if (FAILED(result = services->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&browser)))) {
        return LogComFailure(log, "IShellBrowser", result), false;
    }
    if (FAILED(result = browser->QueryActiveShellView(&view))) return LogComFailure(log, "IShellView", result), false;
    if (FAILED(result = view->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(&background)))) {
        return LogComFailure(log, "GetItemObject", result), false;
    }
    if (FAILED(result = background.As(&folderView))) return LogComFailure(log, "IShellFolderViewDual", result), false;
    if (FAILED(result = folderView->get_Application(&application))) {
        return LogComFailure(log, "get_Application", result), false;
    }
    if (FAILED(result = application.As(&shell))) return LogComFailure(log, "IShellDispatch2", result), false;

    BSTR fileText = SysAllocString(file.c_str());
    VARIANT arguments = TextVariant(parameters);
    VARIANT directory = TextVariant(folder);
    VARIANT verb;
    VariantInit(&verb);
    VARIANT window;
    VariantInit(&window);
    window.vt = VT_I4;
    window.lVal = show;
    result = shell->ShellExecute(fileText, arguments, directory, verb, window);
    SysFreeString(fileText);
    VariantClear(&arguments);
    VariantClear(&directory);
    if (FAILED(result)) return LogComFailure(log, "ShellExecute", result), false;
    return true;
}

// Opens Apple Music from its Start menu identity, with the user's normal rights. Waits a moment for the background
// launcher to be watching, or Apple Music would open without Ampify.
void OpenAppleMusic(Log& log) {
    ULONGLONG deadline = GetTickCount64() + kLauncherWaitMs;
    while (!ampify::win::BackgroundRunning() && GetTickCount64() < deadline) Sleep(kPollMs);

    std::wstring target = L"shell:AppsFolder\\" + ampify::win::FromUtf8(ampify::kAppUserModelId);
    bool opened;
    if (ampify::win::IsElevated()) {
        opened = ShellExecuteUnelevated(target, L"", L"", SW_SHOWNORMAL, log);
    } else {
        opened = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", target.c_str(), nullptr, nullptr,
                                                         SW_SHOWNORMAL)) > 32;
    }
    log.Write(opened ? "Apple Music opened" : "Could not open Apple Music");
}

struct AutoStartResult {
    bool registered = false;
    bool started = false;
};

// Registers the background launcher at sign-in and starts it. An installer run as administrator starts it through
// Explorer: a launcher with administrator rights could not be stopped by ampify restore or the uninstaller from a
// normal session.
AutoStartResult SetUpAutoStart(const fs::path& launcher, Log& log) {
    AutoStartResult result;
    result.registered = ampify::win::RegisterBackground(launcher);
    log.Write(result.registered ? "Background launcher registered: it starts at each Windows sign-in"
                                : "Could not register the background launcher at sign-in");
    if (ampify::win::IsElevated()) {
        if (ampify::win::BackgroundRunning()) {
            result.started = true;
        } else {
            result.started = ShellExecuteUnelevated(launcher.wstring(), ampify::win::kBackgroundArgument,
                                                    launcher.parent_path().wstring(), SW_HIDE, log);
        }
        log.Write(result.started ? "Installer run as administrator: background launcher started through Explorer"
                                 : "Installer run as administrator: could not start the background launcher");
        return result;
    }
    result.started = ampify::win::StartBackground(launcher);
    log.Write(result.started ? "Background launcher started" : "Could not start the background launcher");
    return result;
}

bool WriteUninstallEntry(const fs::path& target, DWORD sizeKb) {
    HKEY key = nullptr;
    LSTATUS status = RegCreateKeyExW(HKEY_CURRENT_USER, ampify::win::FromUtf8(ampify::kUninstallKey).c_str(), 0, nullptr,
                                     REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &key, nullptr);
    if (status != ERROR_SUCCESS) return false;

    std::wstring setup = (target / ampify::kSetupFile).wstring();
    SetRegistryString(key, L"DisplayName", ampify::win::FromUtf8(ampify::kDisplayName));
    SetRegistryString(key, L"DisplayVersion", ampify::win::FromUtf8(ampify::kKitVersion));
    SetRegistryString(key, L"Publisher", ampify::win::FromUtf8(ampify::kProjectOwner));
    SetRegistryString(key, L"InstallLocation", target.wstring());
    SetRegistryString(key, L"DisplayIcon", (target / ampify::kLauncherFile).wstring() + L",0");
    SetRegistryString(key, L"UninstallString", L"\"" + setup + L"\" /uninstall");
    SetRegistryString(key, L"QuietUninstallString", L"\"" + setup + L"\" /uninstall /silent /keepdata");
    SetRegistryDword(key, L"NoModify", 1);
    SetRegistryDword(key, L"NoRepair", 1);
    SetRegistryDword(key, L"EstimatedSize", sizeKb);
    RegCloseKey(key);
    return true;
}

DWORD InstalledSizeKb(const fs::path& directory) {
    std::uintmax_t bytes = 0;
    for (std::string_view name : ampify::InstalledFiles()) {
        std::error_code error;
        std::uintmax_t size = fs::file_size(directory / ampify::win::FromUtf8(name), error);
        if (!error) bytes += size;
    }
    return static_cast<DWORD>(bytes / 1024);
}

// Skips the copy when the source already is the destination.
bool CopyKitFile(const fs::path& source, const fs::path& destination, Log& log) {
    std::error_code error;
    if (fs::exists(destination, error) && fs::equivalent(source, destination, error)) return true;

    fs::copy_file(source, destination, fs::copy_options::overwrite_existing, error);
    if (error) {
        log.Write("Could not copy " + ampify::win::ToUtf8(destination.wstring()) + ": " + error.message() +
                  ". If the file is in use, close Apple Music and run the install again.");
        return false;
    }
    return true;
}

using ampify::setup::Choice;
using ampify::setup::Page;
using ampify::setup::Progress;
using ampify::setup::StepState;
using ampify::setup::Window;

// The steps a run shows, in their order: each is marked by its name, and one the page does not list is skipped.
template <typename Step>
class Steps {
public:
    Steps(Progress& progress, std::vector<Step> order) : m_progress(progress), m_order(std::move(order)) {}
    void Begin(Step step) { Set(step, StepState::Running); }
    void End(Step step, bool ok) { Set(step, ok ? StepState::Done : StepState::Failed); }

private:
    void Set(Step step, StepState state) {
        auto found = std::find(m_order.begin(), m_order.end(), step);
        if (found != m_order.end()) m_progress.Set(static_cast<size_t>(found - m_order.begin()), state);
    }
    Progress& m_progress;
    std::vector<Step> m_order;
};

struct Outcome {
    int code = kExitDone;
    std::wstring error;
};

void RunSteps(std::optional<Window>& window, const Page& page, const std::function<void(Progress&)>& work) {
    if (window) {
        window->Run(page, work);
        return;
    }
    ampify::setup::NoProgress none;
    work(none);
}

// The page of a failed run; returns `code`. A silent run has only the log.
int Failed(std::optional<Window>& window, const std::wstring& text, int code) {
    if (window) {
        Page page;
        page.title = Tr("setup.failedTitle");
        page.text = text;
        page.primary = Tr("setup.close");
        page.failure = true;
        window->Ask(page);
    }
    return code;
}

Page StepsPage(const std::wstring& title, const std::vector<std::wstring>& labels) {
    Page page;
    page.title = title;
    page.steps = labels;
    return page;
}

// Closing Apple Music is a step of its own, only when the person agreed to it on the first page.
Outcome CloseAppleMusicStep(bool agreed, const std::function<void(bool)>& end, Log& log) {
    if (!AppleMusicRunning()) {
        end(true);  // it closed by itself since the first page
        return {};
    }
    bool closed = agreed && CloseAppleMusic(log);
    if (!agreed) log.Write("Apple Music opened during the run: nothing more is changed");
    end(closed);
    if (!closed) return {kExitRefused, Tr("setup.closeFailed")};
    return {};
}

enum class InstallStep { CloseAppleMusic, Copy, Bundled, AutoStart, Entry };

// The Apple Music on this PC, and whether its family was tested: an untested one does not stop the install, the kernel
// stays off in it until it is tested (src/core/injection_policy.cpp).
struct AppleMusicCheck {
    bool found = false;
    std::string version;
    bool tested = true;
};

AppleMusicCheck CheckAppleMusic(Log& log) {
    AppleMusicCheck check;
    std::optional<ampify::launcher::InstalledPackage> package =
        ampify::launcher::FindInstalledPackage(ampify::win::FromUtf8(ampify::kPackageFamilyName).c_str());
    if (!package) {
        log.Write("Apple Music for Windows not found: Ampify installs anyway, but it will start nothing until Apple "
                  "Music is installed");
        return check;
    }
    check.found = true;
    check.version = ampify::AppleMusicVersion(ampify::win::ToUtf8(package->fullName));
    log.Write("Apple Music found: " + ampify::win::ToUtf8(package->fullName));
    if (check.version.empty()) return check;
    std::vector<std::string> extra = ampify::LoadVersionList(ampify::win::KitDirectory() / ampify::kVersionListFile);
    check.tested = ampify::IsKnownAppVersion(check.version, ampify::KnownAppVersionsWith(extra));
    log.Write(check.tested ? "Apple Music " + check.version + ": family " +
                                 std::string(ampify::AppVersionFamily(check.version)) + " tested"
                           : "Apple Music " + check.version + " not tested: Ampify installs anyway, and the kernel stays "
                                                              "off in it until it is tested");
    return check;
}

std::optional<std::string> MissingFile(const fs::path& source) {
    for (std::string_view name : ampify::InstalledFiles()) {
        std::error_code error;
        if (!fs::exists(source / ampify::win::FromUtf8(name), error)) return std::string(name);
    }
    return std::nullopt;
}

Page InstallWelcome(const fs::path& target, const AppleMusicCheck& check, bool appleMusicOpen) {
    Page page;
    page.title = Tr("setup.installTitle", {{"version", ampify::kKitVersion}});
    page.text = Tr("setup.installText", {{"folder", ampify::win::ToUtf8(target.wstring())}});
    if (!check.found) page.notices.push_back({Tr("setup.noAppleMusic"), true});
    if (check.found && !check.tested) page.notices.push_back({Tr("setup.untested", {{"version", check.version}}), true});
    if (appleMusicOpen) page.notices.push_back({Tr("setup.closesAppleMusic")});
    page.secondary = Tr("setup.cancel");
    page.primary = Tr("setup.install");
    return page;
}

std::vector<InstallStep> InstallOrder(bool appleMusicOpen) {
    std::vector<InstallStep> order;
    if (appleMusicOpen) order.push_back(InstallStep::CloseAppleMusic);
    order.insert(order.end(), {InstallStep::Copy, InstallStep::Bundled, InstallStep::AutoStart, InstallStep::Entry});
    return order;
}

std::vector<std::wstring> InstallLabels(const std::vector<InstallStep>& order) {
    std::vector<std::wstring> labels;
    for (InstallStep step : order) {
        switch (step) {
        case InstallStep::CloseAppleMusic: labels.push_back(Tr("setup.stepCloseAppleMusic")); break;
        case InstallStep::Copy: labels.push_back(Tr("setup.stepCopy")); break;
        case InstallStep::Bundled: labels.push_back(Tr("setup.stepBundled")); break;
        case InstallStep::AutoStart: labels.push_back(Tr("setup.stepAutoStart")); break;
        case InstallStep::Entry: labels.push_back(Tr("setup.stepEntry")); break;
        }
    }
    return labels;
}

struct InstallRun {
    Outcome outcome;
    AutoStartResult autoStart;
    bool bundledOk = true;
};

// Copies the programs. The background launcher runs from the install folder: it stops while its file is replaced, and
// starts again if the copy stops half way.
Outcome CopyProgramFiles(const fs::path& source, const fs::path& target, Log& log) {
    fs::path launcher = target / ampify::win::FromUtf8(ampify::kLauncherFile);
    bool wasRunning = ampify::win::BackgroundRunning();
    if (!ampify::win::StopBackground()) log.Write("The background launcher did not stop: its file may stay in use");
    auto stopHere = [&](int code, std::wstring text) {
        if (wasRunning) ampify::win::StartBackground(launcher);
        return Outcome{code, std::move(text)};
    };
    std::error_code error;
    fs::create_directories(target, error);
    if (error) {
        log.Write("Could not create the folder: " + error.message());
        return stopHere(kExitError, Tr("setup.folderFailed"));
    }
    for (std::string_view name : ampify::InstalledFiles()) {
        std::wstring wideName = ampify::win::FromUtf8(name);
        if (!CopyKitFile(source / wideName, target / wideName, log)) {
            return stopHere(kExitRefused, Tr("setup.copyFailed", {{"name", name}}));
        }
    }
    return {};
}

InstallRun RunInstall(const fs::path& source, const fs::path& target, Steps<InstallStep>& steps, bool agreedToClose,
                      Log& log) {
    InstallRun run;
    steps.Begin(InstallStep::CloseAppleMusic);
    run.outcome = CloseAppleMusicStep(agreedToClose, [&](bool ok) { steps.End(InstallStep::CloseAppleMusic, ok); }, log);
    if (run.outcome.code != kExitDone) return run;

    steps.Begin(InstallStep::Copy);
    run.outcome = CopyProgramFiles(source, target, log);
    steps.End(InstallStep::Copy, run.outcome.code == kExitDone);
    if (run.outcome.code != kExitDone) return run;

    // A copy left by an older version would keep its old page, so each install replaces them.
    steps.Begin(InstallStep::Bundled);
    std::vector<std::string> notInstalled = ampify::InstallBundledElements(source, ampify::win::KitDirectory());
    for (const std::string& line : notInstalled) log.Write("Not installed: " + line);
    run.bundledOk = notInstalled.empty();
    if (run.bundledOk) log.Write("Marketplace and Now Playing installed in the Ampify folder");
    RemoveOldShortcut(log);
    log.Write(UpdateUserPath(target, true) ? "Install folder in the user's Path: ampify works in a new terminal"
                                           : "Could not add the install folder to the user's Path");
    steps.End(InstallStep::Bundled, run.bundledOk);

    steps.Begin(InstallStep::AutoStart);
    run.autoStart = SetUpAutoStart(target / ampify::win::FromUtf8(ampify::kLauncherFile), log);
    steps.End(InstallStep::AutoStart, run.autoStart.registered && run.autoStart.started);

    steps.Begin(InstallStep::Entry);
    bool entryOk = WriteUninstallEntry(target, InstalledSizeKb(target));
    log.Write(entryOk ? "Uninstall entry written (Settings > Apps)" : "Could not write the uninstall entry");
    steps.End(InstallStep::Entry, entryOk);
    return run;
}

// The last page. True when the person asks to open Apple Music now.
bool ShowInstalled(Window& window, const InstallRun& run, bool appleMusicFound) {
    Page page;
    page.title = Tr("setup.installedTitle");
    bool background = run.autoStart.registered && run.autoStart.started;
    bool canOpen = appleMusicFound && background;
    if (canOpen) page.text = Tr("setup.installedText");
    if (!background) page.notices.push_back({Tr("setup.noBackground"), true});
    if (!appleMusicFound) page.notices.push_back({Tr("setup.noAppleMusic"), true});
    if (!run.bundledOk) page.notices.push_back({Tr("setup.bundledFailed"), true});
    if (canOpen) {
        page.secondary = Tr("setup.close");
        page.primary = Tr("setup.openAppleMusic");
    } else {
        page.primary = Tr("setup.done");
    }
    return window.Ask(page) == Choice::Primary && canOpen;
}

int Install(bool silent) {
    Log log(ampify::win::JournalFile("install.log"));
    fs::path source = ampify::win::ExecutablePath().parent_path();
    fs::path target = ampify::win::InstallDirectory();
    log.Write("Installing Ampify " + std::string(ampify::kKitVersion) + " from " + ampify::win::ToUtf8(source.wstring()));
    std::optional<Window> window;
    if (!silent) window.emplace(ampify::win::FromUtf8(ampify::kKitVersion));

    if (std::optional<std::string> missing = MissingFile(source)) {
        log.Write("File missing next to the installer: " + *missing);
        return Failed(window, Tr("setup.fileMissing", {{"name", *missing}}), kExitError);
    }
    AppleMusicCheck check = CheckAppleMusic(log);
    bool appleMusicOpen = AppleMusicRunning();
    if (appleMusicOpen && silent) {
        log.Write("Apple Music is open: nothing installed");
        return kExitRefused;
    }
    if (window && window->Ask(InstallWelcome(target, check, appleMusicOpen)) != Choice::Primary) {
        log.Write("Install cancelled: nothing changed");
        return kExitDone;
    }

    std::vector<InstallStep> order = InstallOrder(appleMusicOpen);
    InstallRun run;
    RunSteps(window, StepsPage(Tr("setup.installing"), InstallLabels(order)), [&](Progress& progress) {
        Steps<InstallStep> steps(progress, order);
        run = RunInstall(source, target, steps, appleMusicOpen, log);
    });
    if (run.outcome.code != kExitDone) return Failed(window, run.outcome.error, run.outcome.code);
    log.Write("Install finished in " + ampify::win::ToUtf8(target.wstring()));
    // The window goes first: opening Apple Music may wait a few seconds for the launcher, with nothing left to show.
    bool open = window && ShowInstalled(*window, run, check.found);
    window.reset();
    if (open) OpenAppleMusic(log);
    return kExitDone;
}

enum class UninstallStep { CloseAppleMusic, Stop, Remove, Clean, Data };

// A hidden cmd waits for this process to exit, then deletes the file and its folder (rmdir only removes an empty one).
// It starts in the parent folder, or it would hold the folder in use. If it cannot start, Windows does it at next boot.
void ScheduleSelfDelete(const fs::path& file, Log& log) {
    fs::path folder = file.parent_path();
    std::wstring command = L"cmd.exe /c ping -n 4 127.0.0.1 >nul & del /f /q \"" + file.wstring() + L"\" & rmdir \"" +
                           folder.wstring() + L"\"";
    std::wstring start = folder.parent_path().wstring();
    if (ampify::win::StartProcess(nullptr, command, CREATE_NO_WINDOW, start.c_str(), true)) {
        log.Write("The uninstaller deletes itself after it exits");
        return;
    }
    log.Write("Could not start the self-delete command: the uninstaller is deleted at next restart");
    if (!MoveFileExW(file.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT) ||
        !MoveFileExW(folder.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT)) {
        log.Write("Could not schedule the deletion at restart (error " + std::to_string(GetLastError()) + ")");
    }
}

// What `ampify update --apply` left beside the programs (old copies, the package's text files): without them the
// install folder can go. A file that stays (still in use) is logged and does not stop the uninstall.
void RemoveUpdateLeftovers(const fs::path& target, Log& log) {
    std::vector<fs::path> found;
    std::error_code error;
    for (const fs::directory_entry& entry : fs::directory_iterator(target, error)) {
        std::error_code typeError;
        if (!entry.is_regular_file(typeError)) continue;
        if (ampify::IsUpdateLeftover(ampify::win::ToUtf8(entry.path().filename().wstring()))) found.push_back(entry.path());
    }
    for (const fs::path& file : found) {
        std::error_code removeError;
        fs::remove(file, removeError);
        const std::string name = ampify::win::ToUtf8(file.filename().wstring());
        log.Write(removeError && fs::exists(file) ? "Could not remove " + name + ": " + removeError.message()
                                                  : "Removed: " + name);
    }
}

// Removes one file, waiting a moment while it is in use: `ampify uninstall` starts the uninstaller and exits, so
// ampify.exe may still be closing when its turn comes.
bool RemoveFileWhenFree(const fs::path& file) {
    ULONGLONG deadline = GetTickCount64() + kInUseWaitMs;
    while (true) {
        std::error_code error;
        fs::remove(file, error);
        if (!error || !fs::exists(file)) return true;
        if (GetTickCount64() >= deadline) return false;
        Sleep(kPollMs);
    }
}

// Files first: if one is in use, nothing else is removed yet, so the uninstall can simply be run again. Returns the
// name of a file that stayed.
std::optional<std::string> RemoveProgramFiles(const fs::path& target, Log& log) {
    for (std::string_view name : ampify::InstalledFiles()) {
        std::wstring wideName = ampify::win::FromUtf8(name);
        if (wideName == ampify::win::FromUtf8(ampify::kSetupFile)) continue;
        if (!RemoveFileWhenFree(target / wideName)) {
            log.Write("Could not remove " + std::string(name) + ": it is in use");
            return std::string(name);
        }
        log.Write("Removed: " + std::string(name));
    }
    return std::nullopt;
}

Page UninstallWelcome(bool appleMusicOpen, bool deleteData) {
    Page page;
    page.title = Tr("setup.uninstallTitle");
    page.text = Tr("setup.uninstallText");
    if (appleMusicOpen) page.notices.push_back({Tr("setup.closesAppleMusic")});
    page.checkbox = Tr("setup.deleteData", {{"folder", ampify::win::ToUtf8(ampify::win::KitDirectory().wstring())}});
    page.checked = deleteData;
    page.secondary = Tr("setup.cancel");
    page.primary = Tr("setup.uninstall");
    return page;
}

std::vector<UninstallStep> UninstallOrder(bool appleMusicOpen, bool deleteData) {
    std::vector<UninstallStep> order;
    if (appleMusicOpen) order.push_back(UninstallStep::CloseAppleMusic);
    order.insert(order.end(), {UninstallStep::Stop, UninstallStep::Remove, UninstallStep::Clean});
    if (deleteData) order.push_back(UninstallStep::Data);
    return order;
}

std::vector<std::wstring> UninstallLabels(const std::vector<UninstallStep>& order) {
    std::vector<std::wstring> labels;
    for (UninstallStep step : order) {
        switch (step) {
        case UninstallStep::CloseAppleMusic: labels.push_back(Tr("setup.stepCloseAppleMusic")); break;
        case UninstallStep::Stop: labels.push_back(Tr("setup.stepStop")); break;
        case UninstallStep::Remove: labels.push_back(Tr("setup.stepRemove")); break;
        case UninstallStep::Clean: labels.push_back(Tr("setup.stepClean")); break;
        case UninstallStep::Data: labels.push_back(Tr("setup.stepData")); break;
        }
    }
    return labels;
}

// The Path entry, the old shortcut and the entry of Settings > Apps, once the programs are gone.
void RemoveTraces(const fs::path& target, Log& log) {
    RemoveUpdateLeftovers(target, log);
    RemoveOldShortcut(log);
    log.Write(UpdateUserPath(target, false) ? "Install folder removed from the user's Path"
                                            : "Could not remove the install folder from the user's Path");
    LSTATUS status = RegDeleteTreeW(HKEY_CURRENT_USER, ampify::win::FromUtf8(ampify::kUninstallKey).c_str());
    log.Write(status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND ? "Uninstall entry removed"
                                                                        : "Uninstall entry: error");
}

// Deletes the user's Ampify folder. False when something stayed.
bool DeleteUserData() {
    fs::path data = ampify::win::KitDirectory();
    if (!ampify::IsKitDataFolder(ampify::win::ToUtf8(data.wstring()))) return false;
    std::error_code error;
    fs::remove_all(data, error);
    return !fs::exists(data, error);
}

struct UninstallRun {
    Outcome outcome;
    bool dataDeleted = false;
};

UninstallRun RunUninstall(const fs::path& target, Steps<UninstallStep>& steps, bool agreedToClose, bool deleteData,
                          Log& log) {
    UninstallRun run;
    steps.Begin(UninstallStep::CloseAppleMusic);
    run.outcome = CloseAppleMusicStep(agreedToClose, [&](bool ok) { steps.End(UninstallStep::CloseAppleMusic, ok); }, log);
    if (run.outcome.code != kExitDone) return run;

    // The background launcher goes first: from now on Apple Music starts without Ampify, even if a file stays.
    steps.Begin(UninstallStep::Stop);
    log.Write(ampify::win::UnregisterBackground() ? "Background launcher removed from sign-in"
                                                  : "Could not remove the background launcher from sign-in");
    if (!ampify::win::StopBackground()) {
        log.Write("The background launcher did not stop");
        steps.End(UninstallStep::Stop, false);
        run.outcome = {kExitRefused, Tr("setup.stopFailed")};
        return run;
    }
    steps.End(UninstallStep::Stop, true);

    steps.Begin(UninstallStep::Remove);
    if (std::optional<std::string> stayed = RemoveProgramFiles(target, log)) {
        steps.End(UninstallStep::Remove, false);
        run.outcome = {kExitRefused, Tr("setup.removeFailed", {{"name", *stayed}})};
        return run;
    }
    steps.End(UninstallStep::Remove, true);

    steps.Begin(UninstallStep::Clean);
    RemoveTraces(target, log);
    steps.End(UninstallStep::Clean, true);

    if (deleteData) {
        steps.Begin(UninstallStep::Data);
        run.dataDeleted = DeleteUserData();
        steps.End(UninstallStep::Data, run.dataDeleted);
    }
    return run;
}

void ShowUninstalled(Window& window, bool deleteData, bool dataDeleted) {
    std::string folder = ampify::win::ToUtf8(ampify::win::KitDirectory().wstring());
    Page page;
    page.title = Tr("setup.uninstalledTitle");
    page.text = Tr("setup.uninstalledText") + L"\n\n" +
                (!deleteData   ? Tr("setup.dataKept", {{"folder", folder}})
                 : dataDeleted ? Tr("setup.dataDeleted")
                               : Tr("setup.dataNotDeleted", {{"folder", folder}}));
    page.primary = Tr("setup.done");
    window.Ask(page);
}

int Uninstall(bool silent, bool purgeFlag) {
    fs::path target = ampify::win::InstallDirectory();
    bool appleMusicOpen = AppleMusicRunning();
    if (silent && appleMusicOpen) return kExitRefused;

    // Themes, extensions and settings stay unless the person asks for them to go; silent runs keep them by default.
    bool deleteData = purgeFlag;
    std::optional<Window> window;
    if (!silent) {
        window.emplace(ampify::win::FromUtf8(ampify::kKitVersion));
        if (window->Ask(UninstallWelcome(appleMusicOpen, deleteData), &deleteData) != Choice::Primary) return kExitDone;
    }

    Log log(deleteData ? fs::path() : ampify::win::JournalFile("uninstall.log"));
    log.Write("Uninstalling Ampify " + std::string(ampify::kKitVersion) + (deleteData ? " (data deleted)" : " (data kept)"));
    std::vector<UninstallStep> order = UninstallOrder(appleMusicOpen, deleteData);
    UninstallRun run;
    RunSteps(window, StepsPage(Tr("setup.uninstalling"), UninstallLabels(order)), [&](Progress& progress) {
        Steps<UninstallStep> steps(progress, order);
        run = RunUninstall(target, steps, appleMusicOpen, deleteData, log);
    });
    if (run.outcome.code != kExitDone) return Failed(window, run.outcome.error, run.outcome.code);

    // The installer cannot delete itself while it runs: only when it runs from the install folder does a short-lived
    // command do it after the final page, once this process has exited.
    fs::path selfFile = target / ampify::win::FromUtf8(ampify::kSetupFile);
    std::error_code selfError;
    fs::remove(selfFile, selfError);
    bool selfStays = fs::exists(selfFile, selfError);
    if (!selfStays) {
        std::error_code folderError;
        fs::remove(target, folderError);
    }
    log.Write("Uninstall finished");
    if (window) ShowUninstalled(*window, deleteData, run.dataDeleted);
    window.reset();
    if (selfStays) ScheduleSelfDelete(selfFile, log);
    return kExitDone;
}

}  // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    ampify::i18n::SetLanguage(ampify::i18n::LanguageCode(ampify::win::UserLocaleName()));

    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool silent = HasFlag(argc, argv, L"/silent");
    bool uninstall = HasFlag(argc, argv, L"/uninstall");
    // /keepdata is the default: it stays accepted, for the uninstall entry's quiet command.
    bool purge = HasFlag(argc, argv, L"/purgedata");
    LocalFree(argv);

    int code = uninstall ? Uninstall(silent, purge) : Install(silent);

    CoUninitialize();
    return code;
}
