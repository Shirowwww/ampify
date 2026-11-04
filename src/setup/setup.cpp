// ampify_setup.exe: installs Ampify and uninstalls it. The same file is copied into the install folder as the uninstaller.
//
//   ampify_setup.exe                          install, after a confirmation
//   ampify_setup.exe /silent                  install without any dialog (tests, scripts)
//   ampify_setup.exe /uninstall               uninstall, asks whether to keep the user's data
//   ampify_setup.exe /uninstall /keepdata     uninstall and keep settings, logs and reports
//   ampify_setup.exe /uninstall /purgedata    uninstall and delete the user's data folder too
//   add /silent to any uninstall to skip the dialogs (without a data choice, the data is kept)
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
#include "../win/background.hpp"
#include "../win/close_to_tray.hpp"
#include "../win/package.hpp"
#include "../win/win_common.hpp"

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

// One installer text in each language Ampify speaks, in UTF-8, with {name} placeholders. Logs stay in English.
struct Texts {
    const char* en;
    const char* fr;
    const char* de;
    const char* es;
    const char* pt;
    const char* ru;
    const char* ja;
    const char* zh;
};

constexpr Texts kAskCloseAppleMusic = {
    "Apple Music is open. Close it now and continue?",
    "Apple Music est ouvert. Le fermer maintenant et continuer ?",
    "Apple Music ist geöffnet. Jetzt schließen und fortfahren?",
    "Apple Music está abierto. ¿Cerrarlo ahora y continuar?",
    "O Apple Music está aberto. Fechar agora e continuar?",
    "Apple Music открыт. Закрыть его сейчас и продолжить?",
    "Apple Music が開いています。今すぐ閉じて続行しますか?",
    "Apple Music 正在运行。现在关闭并继续吗？",
};
constexpr Texts kCloseFailed = {
    "Apple Music could not be closed. Close it yourself, then try again.",
    "Apple Music n'a pas pu être fermé. Fermez-le vous-même, puis réessayez.",
    "Apple Music konnte nicht geschlossen werden. Schließen Sie es selbst und versuchen Sie es dann erneut.",
    "No se pudo cerrar Apple Music. Ciérralo tú mismo y vuelve a intentarlo.",
    "Não foi possível fechar o Apple Music. Feche-o você mesmo e tente de novo.",
    "Не удалось закрыть Apple Music. Закройте его сами и попробуйте снова.",
    "Apple Music を閉じられませんでした。手動で閉じてから、もう一度お試しください。",
    "无法关闭 Apple Music。请手动关闭后再试一次。",
};
constexpr Texts kInstalled = {
    "Ampify is installed.",
    "Ampify est installé.",
    "Ampify ist installiert.",
    "Ampify está instalado.",
    "O Ampify foi instalado.",
    "Ampify установлен.",
    "Ampify をインストールしました。",
    "Ampify 已安装。",
};
constexpr Texts kNoBackground = {
    "\n\nAmpify could not start in the background (details in logs\\install.log). Restart your PC, or run "
    "\"ampify apply\" to try again.",
    "\n\nAmpify n'a pas pu démarrer en arrière-plan (détails dans logs\\install.log). Redémarrez votre PC, ou lancez "
    "\"ampify apply\" pour réessayer.",
    "\n\nAmpify konnte nicht im Hintergrund starten (Details in logs\\install.log). Starten Sie den PC neu oder führen "
    "Sie „ampify apply“ aus, um es erneut zu versuchen.",
    "\n\nAmpify no pudo iniciarse en segundo plano (detalles en logs\\install.log). Reinicia el PC o ejecuta "
    "\"ampify apply\" para volver a intentarlo.",
    "\n\nO Ampify não conseguiu iniciar em segundo plano (detalhes em logs\\install.log). Reinicie o PC ou execute "
    "\"ampify apply\" para tentar de novo.",
    "\n\nAmpify не удалось запустить в фоне (подробности в logs\\install.log). Перезагрузите компьютер или выполните "
    "\"ampify apply\", чтобы попробовать снова.",
    "\n\nAmpify をバックグラウンドで起動できませんでした (詳細は logs\\install.log)。PC を再起動するか、"
    "\"ampify apply\" を実行してもう一度お試しください。",
    "\n\nAmpify 无法在后台启动（详情见 logs\\install.log）。请重启电脑，或运行 \"ampify apply\" 再试一次。",
};
constexpr Texts kNoAppleMusic = {
    "\n\nApple Music was not found: install it from the Microsoft Store.",
    "\n\nApple Music est introuvable : installez-le depuis le Microsoft Store.",
    "\n\nApple Music wurde nicht gefunden: Installieren Sie es aus dem Microsoft Store.",
    "\n\nNo se encontró Apple Music: instálalo desde Microsoft Store.",
    "\n\nO Apple Music não foi encontrado: instale-o pela Microsoft Store.",
    "\n\nApple Music не найден: установите его из Microsoft Store.",
    "\n\nApple Music が見つかりません。Microsoft Store からインストールしてください。",
    "\n\n未找到 Apple Music：请从 Microsoft Store 安装。",
};
constexpr Texts kBundledFailed = {
    "\n\nThe Marketplace or Now Playing could not be copied to your Ampify folder (details in logs\\install.log). Run "
    "the install again from the extracted folder.",
    "\n\nLe Marketplace ou « À l'écoute » n'a pas pu être copié dans votre dossier Ampify (détails dans "
    "logs\\install.log). Relancez l'installation depuis le dossier extrait.",
    "\n\nDer Marketplace oder „Jetzt läuft“ konnte nicht in Ihren Ampify-Ordner kopiert werden (Details in "
    "logs\\install.log). Starten Sie die Installation erneut aus dem entpackten Ordner.",
    "\n\nNo se pudo copiar el Marketplace o «Reproduciendo» a tu carpeta de Ampify (detalles en logs\\install.log). "
    "Vuelve a ejecutar la instalación desde la carpeta extraída.",
    "\n\nNão foi possível copiar o Marketplace ou o \"Tocando agora\" para a sua pasta do Ampify (detalhes em "
    "logs\\install.log). Execute a instalação de novo a partir da pasta extraída.",
    "\n\nНе удалось скопировать Marketplace или «Сейчас играет» в папку Ampify (подробности в logs\\install.log). "
    "Запустите установку снова из распакованной папки.",
    "\n\nMarketplace または「再生中」を Ampify フォルダーにコピーできませんでした (詳細は logs\\install.log)。"
    "展開したフォルダーからもう一度インストールしてください。",
    "\n\n无法将 Marketplace 或“正在播放”复制到你的 Ampify 文件夹（详情见 logs\\install.log）。请从解压后的文件夹"
    "重新运行安装。",
};
constexpr Texts kOpenNow = {
    "\n\nOpen Apple Music now? Ampify loads with it each time.",
    "\n\nOuvrir Apple Music maintenant ? Ampify se charge avec lui à chaque fois.",
    "\n\nApple Music jetzt öffnen? Ampify wird jedes Mal mitgeladen.",
    "\n\n¿Abrir Apple Music ahora? Ampify se carga con él cada vez.",
    "\n\nAbrir o Apple Music agora? O Ampify carrega junto toda vez.",
    "\n\nОткрыть Apple Music сейчас? Ampify будет загружаться вместе с ним каждый раз.",
    "\n\n今すぐ Apple Music を開きますか? Ampify は毎回一緒に読み込まれます。",
    "\n\n现在打开 Apple Music 吗？Ampify 每次都会随它一起加载。",
};
constexpr Texts kFileMissing = {
    "A file is missing next to the installer: {name}",
    "Un fichier manque à côté de l'installateur : {name}",
    "Neben dem Installationsprogramm fehlt eine Datei: {name}",
    "Falta un archivo junto al instalador: {name}",
    "Falta um arquivo ao lado do instalador: {name}",
    "Рядом с установщиком не хватает файла: {name}",
    "インストーラーと同じ場所にファイルがありません: {name}",
    "安装程序旁缺少一个文件：{name}",
};
constexpr Texts kFolderFailed = {
    "The install folder could not be created.",
    "Le dossier d'installation n'a pas pu être créé.",
    "Der Installationsordner konnte nicht erstellt werden.",
    "No se pudo crear la carpeta de instalación.",
    "Não foi possível criar a pasta de instalação.",
    "Не удалось создать папку установки.",
    "インストール先のフォルダーを作成できませんでした。",
    "无法创建安装文件夹。",
};
constexpr Texts kCopyFailed = {
    "The install could not copy {name}. Close Apple Music, then run the install again.",
    "L'installation n'a pas pu copier {name}. Fermez Apple Music, puis relancez l'installation.",
    "Die Installation konnte {name} nicht kopieren. Schließen Sie Apple Music und starten Sie die Installation erneut.",
    "La instalación no pudo copiar {name}. Cierra Apple Music y vuelve a ejecutar la instalación.",
    "A instalação não conseguiu copiar {name}. Feche o Apple Music e execute a instalação de novo.",
    "Установщику не удалось скопировать {name}. Закройте Apple Music и запустите установку снова.",
    "{name} をコピーできませんでした。Apple Music を閉じてから、もう一度インストールしてください。",
    "安装无法复制 {name}。请关闭 Apple Music，然后重新运行安装。",
};
constexpr Texts kAskUninstall = {
    "Uninstall Ampify for Apple Music?\n\nApple Music goes back to exactly how it was, without Ampify.",
    "Désinstaller Ampify pour Apple Music ?\n\nApple Music redevient exactement comme avant, sans Ampify.",
    "Ampify für Apple Music deinstallieren?\n\nApple Music wird wieder genau so wie vorher, ohne Ampify.",
    "¿Desinstalar Ampify para Apple Music?\n\nApple Music vuelve a quedar exactamente como estaba, sin Ampify.",
    "Desinstalar o Ampify do Apple Music?\n\nO Apple Music volta a ser exatamente como era, sem o Ampify.",
    "Удалить Ampify для Apple Music?\n\nApple Music вернётся в точности к прежнему виду, без Ampify.",
    "Apple Music 用の Ampify をアンインストールしますか?\n\nApple Music は Ampify を入れる前とまったく同じ状態に戻ります。",
    "要卸载 Apple Music 的 Ampify 吗？\n\nApple Music 将完全恢复原样，不再有 Ampify。",
};
constexpr Texts kAskKeepData = {
    "Keep your themes, extensions and settings?\n\nYes: they stay in {folder}.\nNo: they are deleted.",
    "Garder vos thèmes, extensions et réglages ?\n\nOui : ils restent dans {folder}.\nNon : ils sont supprimés.",
    "Ihre Designs, Erweiterungen und Einstellungen behalten?\n\nJa: Sie bleiben in {folder}.\nNein: Sie werden "
    "gelöscht.",
    "¿Conservar tus temas, extensiones y ajustes?\n\nSí: se quedan en {folder}.\nNo: se eliminan.",
    "Manter seus temas, extensões e ajustes?\n\nSim: eles ficam em {folder}.\nNão: eles são apagados.",
    "Сохранить ваши темы, расширения и настройки?\n\nДа: они останутся в {folder}.\nНет: они будут удалены.",
    "テーマ、拡張機能、設定を残しますか?\n\nはい: {folder} に残ります。\nいいえ: 削除されます。",
    "保留你的主题、扩展和设置吗？\n\n是：它们保留在 {folder}。\n否：它们将被删除。",
};
constexpr Texts kStopFailed = {
    "Ampify could not stop its background program. Try again.",
    "Ampify n'a pas pu arrêter son programme en arrière-plan. Réessayez.",
    "Ampify konnte sein Hintergrundprogramm nicht beenden. Versuchen Sie es erneut.",
    "Ampify no pudo detener su programa en segundo plano. Inténtalo de nuevo.",
    "O Ampify não conseguiu parar seu programa em segundo plano. Tente de novo.",
    "Ampify не удалось остановить свою фоновую программу. Попробуйте снова.",
    "Ampify のバックグラウンドプログラムを停止できませんでした。もう一度お試しください。",
    "Ampify 无法停止其后台程序。请再试一次。",
};
constexpr Texts kRemoveFailed = {
    "Could not remove {name}. Close Apple Music, then run the uninstall again.",
    "Impossible de supprimer {name}. Fermez Apple Music, puis relancez la désinstallation.",
    "{name} konnte nicht entfernt werden. Schließen Sie Apple Music und starten Sie die Deinstallation erneut.",
    "No se pudo eliminar {name}. Cierra Apple Music y vuelve a ejecutar la desinstalación.",
    "Não foi possível remover {name}. Feche o Apple Music e execute a desinstalação de novo.",
    "Не удалось удалить {name}. Закройте Apple Music и запустите удаление снова.",
    "{name} を削除できませんでした。Apple Music を閉じてから、もう一度アンインストールしてください。",
    "无法删除 {name}。请关闭 Apple Music，然后重新运行卸载。",
};
constexpr Texts kUninstalled = {
    "Ampify is uninstalled. Apple Music is back to normal.",
    "Ampify est désinstallé. Apple Music est revenu à la normale.",
    "Ampify ist deinstalliert. Apple Music ist wieder wie vorher.",
    "Ampify se ha desinstalado. Apple Music ha vuelto a la normalidad.",
    "O Ampify foi desinstalado. O Apple Music voltou ao normal.",
    "Ampify удалён. Apple Music снова работает как обычно.",
    "Ampify をアンインストールしました。Apple Music は元に戻りました。",
    "Ampify 已卸载。Apple Music 已恢复正常。",
};
constexpr Texts kDataKept = {
    "\n\nYour themes, extensions and settings were kept in {folder}.",
    "\n\nVos thèmes, extensions et réglages ont été gardés dans {folder}.",
    "\n\nIhre Designs, Erweiterungen und Einstellungen wurden in {folder} behalten.",
    "\n\nTus temas, extensiones y ajustes se conservaron en {folder}.",
    "\n\nSeus temas, extensões e ajustes foram mantidos em {folder}.",
    "\n\nВаши темы, расширения и настройки сохранены в {folder}.",
    "\n\nテーマ、拡張機能、設定は {folder} に残してあります。",
    "\n\n你的主题、扩展和设置已保留在 {folder}。",
};

// The Windows display language, as the kernel names it ("fr", "zh", "zh-hant"...).
std::string DisplayLanguage() {
    wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
    if (LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT), name, LOCALE_NAME_MAX_LENGTH, 0) == 0) {
        return "en";
    }
    return ampify::i18n::LanguageCode(ampify::win::ToUtf8(name));
}

// The text in the display language, English for a language Ampify does not speak, with its placeholders filled.
std::wstring Tr(const Texts& texts, ampify::i18n::Args args = {}) {
    static const std::string language = DisplayLanguage();
    const std::pair<std::string_view, const char*> columns[] = {
        {"fr", texts.fr}, {"de", texts.de}, {"es", texts.es}, {"pt", texts.pt},
        {"ru", texts.ru}, {"ja", texts.ja}, {"zh", texts.zh},
    };
    const char* chosen = texts.en;
    for (const auto& [code, text] : columns) {
        if (code == language) chosen = text;
    }
    return ampify::win::FromUtf8(ampify::i18n::Format(chosen, args));
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
          m_journal(*m_out, ampify::Journal::LocalTimeStamp) {}

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

// The kernel lives inside Apple Music: no file of Ampify may be replaced or deleted under it.
std::vector<DWORD> AppleMusicProcesses() {
    std::vector<DWORD> ids = ampify::win::ProcessIdsNamed(L"AppleMusic.exe");
    std::vector<DWORD> agent = ampify::win::ProcessIdsNamed(L"AMPLibraryAgent.exe");
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

// Apple Music is open: asks, then closes it. False when the user says no or it stays open (already told to the user).
bool CloseAppleMusicWithConsent(Log& log) {
    if (!ampify::win::AskYesNo(Tr(kAskCloseAppleMusic), L"Ampify")) {
        log.Write("Apple Music is open and the user chose to keep it: nothing changed");
        return false;
    }
    if (!CloseAppleMusic(log)) {
        log.Write("Apple Music could not be closed");
        ampify::win::ShowMessage(Tr(kCloseFailed), true);
        return false;
    }
    return true;
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

// What the install left of the automatic start, for the log and the final message.
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

// `asksToOpen` tells whether the message ends with the question "open Apple Music now?".
std::wstring InstalledMessage(const AutoStartResult& autoStart, bool appleMusicFound, bool bundledOk, bool& asksToOpen) {
    std::wstring text = Tr(kInstalled);
    if (!autoStart.registered || !autoStart.started) text += Tr(kNoBackground);
    if (!appleMusicFound) text += Tr(kNoAppleMusic);
    if (!bundledOk) text += Tr(kBundledFailed);
    asksToOpen = appleMusicFound && autoStart.registered && autoStart.started;
    if (asksToOpen) text += Tr(kOpenNow);
    return text;
}

// The entry that makes Ampify show in Settings > Apps, and gives the uninstall command.
bool WriteUninstallEntry(const fs::path& target, DWORD sizeKb) {
    HKEY key = nullptr;
    LSTATUS status = RegCreateKeyExW(HKEY_CURRENT_USER, ampify::win::FromUtf8(ampify::kUninstallKey).c_str(), 0, nullptr,
                                     REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &key, nullptr);
    if (status != ERROR_SUCCESS) return false;

    std::wstring setup = (target / ampify::kSetupFile).wstring();
    SetRegistryString(key, L"DisplayName", ampify::win::FromUtf8(ampify::kDisplayName));
    SetRegistryString(key, L"DisplayVersion", ampify::win::FromUtf8(ampify::kKitVersion));
    SetRegistryString(key, L"Publisher", ampify::win::FromUtf8(ampify::kPublisher));
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

// Sum of the installed files, in kilobytes, as Settings > Apps shows it.
DWORD InstalledSizeKb(const fs::path& directory) {
    std::uintmax_t bytes = 0;
    for (std::string_view name : ampify::InstalledFiles()) {
        std::error_code error;
        std::uintmax_t size = fs::file_size(directory / ampify::win::FromUtf8(name), error);
        if (!error) bytes += size;
    }
    return static_cast<DWORD>(bytes / 1024);
}

// Copies one file of Ampify. Skips the copy when the source already is the destination.
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

int Install(bool silent) {
    Log log(ampify::win::JournalFile("install.log"));
    fs::path source = ampify::win::ExecutablePath().parent_path();
    fs::path target = ampify::win::InstallDirectory();

    log.Write("Installing Ampify " + std::string(ampify::kKitVersion) + " from " + ampify::win::ToUtf8(source.wstring()));
    for (std::string_view name : ampify::InstalledFiles()) {
        std::error_code error;
        if (!fs::exists(source / ampify::win::FromUtf8(name), error)) {
            std::string message = "File missing next to the installer: " + std::string(name);
            log.Write(message);
            if (!silent) {
                ampify::win::ShowMessage(Tr(kFileMissing, {{"name", name}}), true);
            }
            return kExitError;
        }
    }

    std::optional<ampify::launcher::InstalledPackage> package =
        ampify::launcher::FindInstalledPackage(ampify::win::FromUtf8(ampify::kPackageFamilyName).c_str());
    std::string packageLine = package
                                  ? "Apple Music found: " + ampify::win::ToUtf8(package->fullName)
                                  : "Apple Music for Windows not found: Ampify installs anyway, but it will "
                                    "start nothing until Apple Music is installed";
    log.Write(packageLine);

    if (AppleMusicRunning()) {
        if (silent) {
            log.Write("Apple Music is open: nothing installed");
            return kExitRefused;
        }
        if (!CloseAppleMusicWithConsent(log)) return kExitRefused;
    }

    // The background launcher runs from the install folder: it stops while its file is replaced, and starts again if
    // the install stops half way.
    fs::path launcher = target / ampify::win::FromUtf8(ampify::kLauncherFile);
    bool wasRunning = ampify::win::BackgroundRunning();
    if (!ampify::win::StopBackground()) log.Write("The background launcher did not stop: its file may stay in use");
    auto stopHere = [&](int code) {
        if (wasRunning) ampify::win::StartBackground(launcher);
        return code;
    };

    std::error_code error;
    fs::create_directories(target, error);
    if (error) {
        log.Write("Could not create the folder: " + error.message());
        if (!silent) {
            ampify::win::ShowMessage(Tr(kFolderFailed), true);
        }
        return stopHere(kExitError);
    }

    for (std::string_view name : ampify::InstalledFiles()) {
        std::wstring wideName = ampify::win::FromUtf8(name);
        if (!CopyKitFile(source / wideName, target / wideName, log)) {
            if (!silent) {
                ampify::win::ShowMessage(Tr(kCopyFailed, {{"name", name}}), true);
            }
            return stopHere(kExitRefused);
        }
    }

    // A copy left by an older version would keep its old page, so each install replaces them.
    std::vector<std::string> notInstalled = ampify::InstallBundledElements(source, ampify::win::KitDirectory());
    for (const std::string& line : notInstalled) log.Write("Not installed: " + line);
    if (notInstalled.empty()) log.Write("Marketplace and Now Playing installed in the Ampify folder");

    RemoveOldShortcut(log);
    log.Write(UpdateUserPath(target, true) ? "Install folder in the user's Path: ampify works in a new terminal"
                                           : "Could not add the install folder to the user's Path");
    AutoStartResult autoStart = SetUpAutoStart(launcher, log);

    bool entryOk = WriteUninstallEntry(target, InstalledSizeKb(target));
    log.Write(entryOk ? "Uninstall entry written (Settings > Apps)"
                      : "Could not write the uninstall entry");

    log.Write("Install finished in " + ampify::win::ToUtf8(target.wstring()));
    if (!silent) {
        bool asksToOpen = false;
        std::wstring text = InstalledMessage(autoStart, package.has_value(), notInstalled.empty(), asksToOpen);
        if (!asksToOpen) {
            ampify::win::ShowMessage(text);
        } else if (ampify::win::AskYesNo(text, L"Ampify")) {
            OpenAppleMusic(log);
        }
    }
    return kExitDone;
}

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

int Uninstall(bool silent, bool keepFlag, bool purgeFlag) {
    fs::path target = ampify::win::InstallDirectory();

    if (silent && AppleMusicRunning()) return kExitRefused;

    if (!silent && !ampify::win::AskYesNo(Tr(kAskUninstall), L"Ampify")) {
        return kExitDone;
    }

    // Themes, extensions and settings stay unless the user asks for them to go. Silent runs keep them by default.
    bool keepData = true;
    if (purgeFlag) {
        keepData = false;
    } else if (!keepFlag && !silent) {
        std::string folder = ampify::win::ToUtf8(ampify::win::KitDirectory().wstring());
        keepData = ampify::win::AskYesNo(Tr(kAskKeepData, {{"folder", folder}}), L"Ampify");
    }

    Log log(keepData ? ampify::win::JournalFile("uninstall.log") : fs::path());
    log.Write("Uninstalling Ampify " + std::string(ampify::kKitVersion) +
              (keepData ? " (data kept)" : " (data deleted)"));

    if (!silent && AppleMusicRunning() && !CloseAppleMusicWithConsent(log)) return kExitRefused;

    // The background launcher goes first: from now on Apple Music starts without Ampify, even if a file stays.
    log.Write(ampify::win::UnregisterBackground() ? "Background launcher removed from sign-in"
                                                  : "Could not remove the background launcher from sign-in");
    if (!ampify::win::StopBackground()) {
        log.Write("The background launcher did not stop");
        if (!silent) {
            ampify::win::ShowMessage(Tr(kStopFailed), true);
        }
        return kExitRefused;
    }

    // Files first: if one is in use, nothing else is removed yet, so the uninstall can simply be run again.
    for (std::string_view name : ampify::InstalledFiles()) {
        std::wstring wideName = ampify::win::FromUtf8(name);
        if (wideName == ampify::win::FromUtf8(ampify::kSetupFile)) continue;

        std::error_code error;
        fs::remove(target / wideName, error);
        if (error && fs::exists(target / wideName)) {
            log.Write("Could not remove " + std::string(name) + ": " + error.message());
            if (!silent) {
                ampify::win::ShowMessage(Tr(kRemoveFailed, {{"name", name}}), true);
            }
            return kExitRefused;
        }
        log.Write("Removed: " + std::string(name));
    }

    RemoveUpdateLeftovers(target, log);
    RemoveOldShortcut(log);
    log.Write(UpdateUserPath(target, false) ? "Install folder removed from the user's Path"
                                            : "Could not remove the install folder from the user's Path");

    LSTATUS status = RegDeleteTreeW(HKEY_CURRENT_USER, ampify::win::FromUtf8(ampify::kUninstallKey).c_str());
    log.Write(status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND ? "Uninstall entry removed"
                                                                        : "Uninstall entry: error");

    if (!keepData) {
        fs::path data = ampify::win::KitDirectory();
        if (ampify::IsKitDataFolder(ampify::win::ToUtf8(data.wstring()))) {
            std::error_code error;
            fs::remove_all(data, error);
        }
    }

    // The installer cannot delete itself while it runs: only when it runs from the install folder does a short-lived
    // command do it after the final dialog, once this process has exited.
    fs::path selfFile = target / ampify::win::FromUtf8(ampify::kSetupFile);
    std::error_code selfError;
    fs::remove(selfFile, selfError);
    bool selfStays = fs::exists(selfFile, selfError);
    if (!selfStays) {
        std::error_code folderError;
        fs::remove(target, folderError);
    }

    log.Write("Uninstall finished");
    if (!silent) {
        std::wstring text = Tr(kUninstalled);
        if (keepData) text += Tr(kDataKept, {{"folder", ampify::win::ToUtf8(ampify::win::KitDirectory().wstring())}});

        ampify::win::ShowMessage(text);
    }
    if (selfStays) ScheduleSelfDelete(selfFile, log);
    return kExitDone;
}

}  // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool silent = HasFlag(argc, argv, L"/silent");
    bool uninstall = HasFlag(argc, argv, L"/uninstall");
    bool keep = HasFlag(argc, argv, L"/keepdata");
    bool purge = HasFlag(argc, argv, L"/purgedata");
    LocalFree(argv);

    int code = uninstall ? Uninstall(silent, keep, purge) : Install(silent);

    CoUninitialize();
    return code;
}
