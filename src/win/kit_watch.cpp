#include "kit_watch.hpp"

#include <utility>

namespace ampify::win {

namespace {

// Names, sizes and writes: a file saved in place, a file replaced through a rename, a folder added or removed.
constexpr DWORD kChanges = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE |
                           FILE_NOTIFY_CHANGE_LAST_WRITE;

}  // namespace

KitWatch::KitWatch(std::filesystem::path kit, std::vector<std::string> folders) {
    m_folders.push_back({kit, false});
    for (const std::string& name : folders) m_folders.push_back({kit / name, true});
    for (Folder& folder : m_folders) folder.handle = Open(folder);
}

KitWatch::~KitWatch() {
    for (Folder& folder : m_folders) {
        if (folder.handle != INVALID_HANDLE_VALUE) FindCloseChangeNotification(folder.handle);
    }
}

HANDLE KitWatch::Open(const Folder& folder) {
    return FindFirstChangeNotificationW(folder.path.c_str(), folder.subtree ? TRUE : FALSE, kChanges);
}

bool KitWatch::Moved() {
    bool moved = std::exchange(m_first, false);
    for (Folder& folder : m_folders) {
        if (folder.handle == INVALID_HANDLE_VALUE) {
            folder.handle = Open(folder);  // a folder made since (the first theme installed)
            moved = true;
            continue;
        }
        if (WaitForSingleObject(folder.handle, 0) != WAIT_OBJECT_0) continue;
        moved = true;
        if (!FindNextChangeNotification(folder.handle)) {  // the folder went away: watched again once it is back
            FindCloseChangeNotification(folder.handle);
            folder.handle = INVALID_HANDLE_VALUE;
        }
    }
    return moved;
}

}  // namespace ampify::win
