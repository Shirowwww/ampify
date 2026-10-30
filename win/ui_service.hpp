#pragma once

// The kernel's interface service in AppleMusic.exe: finds Apple Music's XAML windows, follows their pages, shows the
// extensions' panels, popups, app pages and buttons with the native renderer (ui_renderer.hpp), and adds the
// « Mods (Apple Music Mod Kit) » card to the Playback page of the settings. Compiled only with AMK_WITH_XAML. Not run
// yet in Apple Music: the bench case 63-m8-interface checks its journal lines (docs/tests/M8-interface.md).
//
// Threads: a worker thread (MTA) watches the windows (SetWinEventHook on the process + a poll every 5 s) and the
// interface model (UiModel, every 250 ms); everything that touches XAML runs on the window's interface thread through
// window.DispatcherQueue().TryEnqueue(Low), one queued refresh per window at a time (uirender::RefreshTicket).
//
// Discovery, page watching and the settings card follow AMExclusive (MIT, https://github.com/AntiOblivionis/AMExclusive,
// mod/src/UiAdapter.cpp at 91f589d), adapted to the kit.

#if defined(AMK_WITH_XAML)

#include <filesystem>
#include <memory>
#include <thread>

#include "../core/journal.hpp"
#include "../core/ui_model.hpp"
#include "ui_renderer.hpp"

namespace amk::win {

class UiService {
public:
    // `sink` receives the clicks and changes of the rendered trees, on the interface thread: it must only queue them
    // for the JavaScript thread (ExtensionLoader::DeliverClickTo, DeliverEventTo).
    UiService(Journal& journal, std::filesystem::path kit, UiModel& model, UiEventSink sink);
    ~UiService();

    UiService(const UiService&) = delete;
    UiService& operator=(const UiService&) = delete;

    void Start();
    void Stop();

    struct Shared;  // defined in ui_service.cpp

private:
    void Run();

    std::shared_ptr<Shared> m_shared;
    UiModel& m_model;
    std::thread m_worker;
};

}  // namespace amk::win

#endif  // AMK_WITH_XAML
