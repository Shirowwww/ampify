#pragma once

// Whether anything Ampify reads in the kit changed since the last look: the files at its root (settings.txt,
// reload.txt, sidebar.json...) and, when asked, every file under themes\ and snippets\. Windows change notifications
// keep count, so a look costs no disk access, where the kernel used to open or stat these files several times a
// second. The logs\ folder is not watched: the journals write to it all the time.

#include <windows.h>

#include <filesystem>
#include <string>
#include <vector>

namespace ampify::win {

class KitWatch {
public:
    // `folders`: the sub-folders to watch with all they hold, besides the root ("themes", "snippets").
    explicit KitWatch(std::filesystem::path kit, std::vector<std::string> folders = {});
    ~KitWatch();
    KitWatch(const KitWatch&) = delete;
    KitWatch& operator=(const KitWatch&) = delete;

    // True when something changed since the last call, at the first call, and while a folder cannot be watched (it
    // does not exist yet, or Windows refused): then every look counts as a change, as when the files were read each
    // time. One thread at a time.
    bool Moved();

private:
    struct Folder {
        std::filesystem::path path;
        bool subtree = false;
        HANDLE handle = INVALID_HANDLE_VALUE;
    };

    static HANDLE Open(const Folder& folder);

    std::vector<Folder> m_folders;
    bool m_first = true;
};

}  // namespace ampify::win
