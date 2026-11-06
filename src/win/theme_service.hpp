#pragma once

// The kernel's theme service: a worker thread reads the settings and the theme folder, and posts each tick to the
// interface thread, where the theme session applies it. Compiled only with AMPIFY_WITH_XAML, like xaml_backend.hpp.

#if defined(AMPIFY_WITH_XAML)

#include <windows.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <thread>

#include "../core/journal.hpp"

namespace ampify::win {

struct UiSide;  // the state that only the interface thread touches; defined in theme_service.cpp

class ThemeService {
public:
    ThemeService(Journal& journal, std::filesystem::path kit);
    ~ThemeService();

    // Starts the worker. `module` is the kernel DLL.
    void Start(HINSTANCE module);
    void Stop();

private:
    void Run(HINSTANCE module);

    Journal& m_journal;
    std::filesystem::path m_kit;
    std::atomic<bool> m_running{false};
    std::thread m_worker;
    std::shared_ptr<UiSide> m_ui;
};

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
