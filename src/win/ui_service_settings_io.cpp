// Export and import of one element's settings: a small JSON file in the kit's exports folder, copied to the clipboard,
// and the clipboard read back on import, so the text can travel by message. The rules of the file are in
// src/core/element_settings.cpp.

#if defined(AMPIFY_WITH_XAML)

#include "ui_service_detail.hpp"

#include <algorithm>
#include <cstring>
#include <cwchar>
#include <fstream>

namespace ampify::win::ui_service_detail {

namespace {

std::map<std::string, std::string> ValuesOf(const uirender::ModEntry& mod) {
    std::map<std::string, std::string> values;
    for (size_t index = 0; index < mod.settings.size() && index < mod.values.size(); index++) {
        values[mod.settings[index].key] = mod.values[index];
    }
    return values;
}

// Puts the text on the clipboard as Unicode text. Windows owns the memory once it accepts it.
bool CopyText(const std::string& text) {
    winrt::hstring wide = Wide(text);
    size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) return false;
    void* target = GlobalLock(memory);
    if (!target) {
        GlobalFree(memory);
        return false;
    }
    std::memcpy(target, wide.c_str(), bytes);
    GlobalUnlock(memory);
    if (!OpenClipboard(nullptr)) {
        GlobalFree(memory);
        return false;
    }
    EmptyClipboard();
    bool copied = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
    CloseClipboard();
    if (!copied) GlobalFree(memory);
    return copied;
}

// The text on the clipboard as UTF-8; nothing when it holds no text. Past the longest settings file it is cut: one
// UTF-16 unit is at least one UTF-8 byte, so the cut text is refused as too big without converting all of it.
std::optional<std::string> ClipboardText() {
    if (!OpenClipboard(nullptr)) return std::nullopt;
    std::optional<std::string> text;
    if (HANDLE handle = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t* chars = static_cast<const wchar_t*>(GlobalLock(handle))) {
            size_t units = std::min(static_cast<size_t>(GlobalSize(handle) / sizeof(wchar_t)),
                                    settings_panel::kMaxFileBytes + 1);
            text = Utf8(winrt::hstring(chars, static_cast<uint32_t>(wcsnlen(chars, units))));
            GlobalUnlock(handle);
        }
    }
    CloseClipboard();
    return text;
}

bool SaveText(const std::filesystem::path& file, const std::string& text) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
    out.flush();
    return static_cast<bool>(out);
}

}  // namespace

// The values come from settings.txt as it is now, not from the card's copy, which a change may have left behind.
void ExportElementSettings(UiService::Shared& shared, const uirender::ModEntry& mod, const SettingsSay& say) {
    std::string kind(SettingKindOf(mod));
    std::optional<uirender::ModEntry> now = uirender::ElementSettings(shared.kit, kind, mod.id);
    const uirender::ModEntry& current = now ? *now : mod;
    std::string text = settings_panel::ExportSettings(kind, mod.id, current.settings, ValuesOf(current));
    std::filesystem::path file = shared.kit / "exports" / (kind + "-" + mod.id + ".json");
    if (!SaveText(file, text)) {
        shared.journal.Write("Settings: could not write " + file.string());
        return;
    }
    if (!CopyText(text)) shared.journal.Write("Settings: the clipboard refused the copy of \"" + mod.id + "\"");
    std::string done = i18n::Text("settings.exported", {{"id", mod.id}, {"path", file.string()}});
    shared.journal.Write(done);
    say(done);
}

// A refusal is written to the journal and shown under the card. A change rebuilds the card, then shows its line.
void ImportElementSettings(UiService::Shared& shared, const uirender::ModEntry& mod, const SettingsRefresh& refresh,
                           const SettingsSay& say) {
    auto refuse = [&shared, &say](const std::string& line) {
        shared.journal.Write(line);
        say(line);
    };
    std::optional<std::string> text = ClipboardText();
    if (!text) {
        refuse(i18n::Text("settings.noClipboard"));
        return;
    }
    std::string kind(SettingKindOf(mod));
    settings_panel::ImportRefusal refusal;
    std::optional<std::map<std::string, std::string>> values =
        settings_panel::ImportSettings(*text, kind, mod.id, mod.settings, refusal);
    if (!values) {
        refuse(i18n::Text("settings.importFailed", {{"error", settings_panel::RefusalText(refusal)}}));
        return;
    }
    std::string error;
    if (!uirender::ChangeElementValues(shared.kit, kind, mod.id, *values, error)) {
        refuse(i18n::Text("settings.importFailed", {{"error", error}}));
        return;
    }
    std::string done = i18n::Text("settings.imported", {{"id", mod.id}});
    shared.journal.Write(done);
    refresh(done);
}

}  // namespace ampify::win::ui_service_detail

#endif  // AMPIFY_WITH_XAML
