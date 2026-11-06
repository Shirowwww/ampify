#include "clipboard.hpp"

#include <windows.h>

#include <chrono>
#include <cstring>
#include <thread>

#include "win_common.hpp"

namespace ampify::win {

namespace {

// Another program may hold the clipboard for a moment (a clipboard manager reading what was just copied).
constexpr int kOpenTries = 5;
constexpr auto kOpenRetry = std::chrono::milliseconds(10);

bool OpenClipboardSoon(HWND owner) {
    for (int attempt = 0; attempt < kOpenTries; attempt++) {
        if (OpenClipboard(owner)) return true;
        std::this_thread::sleep_for(kOpenRetry);
    }
    return false;
}

// Closes the clipboard however the caller leaves.
struct OpenedClipboard {
    ~OpenedClipboard() { CloseClipboard(); }
};

}  // namespace

bool CopyTextToClipboard(void* owner, const std::string& utf8) {
    if (!owner) return false;
    std::wstring wide = FromUtf8(utf8);
    const size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) return false;
    void* target = GlobalLock(memory);
    if (!target) {
        GlobalFree(memory);
        return false;
    }
    std::memcpy(target, wide.c_str(), bytes);
    GlobalUnlock(memory);
    if (!OpenClipboardSoon(static_cast<HWND>(owner))) {
        GlobalFree(memory);
        return false;
    }
    OpenedClipboard opened;
    // Once SetClipboardData succeeds the memory belongs to Windows; only a refusal leaves it to free here.
    if (!EmptyClipboard() || !SetClipboardData(CF_UNICODETEXT, memory)) {
        GlobalFree(memory);
        return false;
    }
    return true;
}

std::optional<std::string> ReadClipboardText() {
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboardSoon(nullptr)) return std::nullopt;
    OpenedClipboard opened;
    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    if (!data) return std::nullopt;
    const auto* text = static_cast<const wchar_t*>(GlobalLock(data));
    if (!text) return std::nullopt;
    // The block may be larger than the text: the text ends at its first null, and never past the block.
    const size_t limit = GlobalSize(data) / sizeof(wchar_t);
    std::string utf8 = ToUtf8(std::wstring_view(text, wcsnlen(text, limit)));
    GlobalUnlock(data);
    return utf8;
}

}  // namespace ampify::win
