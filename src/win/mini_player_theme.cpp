// Built with AMPIFY_WITH_XAML, like theme_service.cpp, whose request this copy of the engine applies.

#if defined(AMPIFY_WITH_XAML)

#include "mini_player_theme.hpp"

#include <fstream>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <string>
#include <utility>

#include <winrt/Microsoft.UI.Dispatching.h>

#include "win_common.hpp"
#include "xaml_backend.hpp"
#include "xaml_chrome.hpp"
#include "../core/theme/inspector.hpp"
#include "../core/ui_watchdog.hpp"

namespace ampify::win {

namespace {

namespace dispatching = winrt::Microsoft::UI::Dispatching;
namespace xaml = winrt::Microsoft::UI::Xaml;

// The engine's copy for one mini player window. Made, used and destroyed on that window's thread only: its XAML
// objects belong there.
struct MiniSide {
    explicit MiniSide(Journal& parent) : journal(parent, "Mini player: "), session(journal) {}

    Journal journal;  // its lines in the kernel's log, behind "Mini player: "
    XamlTreeBackend backend;
    theme::ThemeSession session;
    bool attached = false;
    bool ticked = false;  // a first tick ran: the theme is on, whatever the window's visibility then
};

// One mini player window: Apple makes a new one at each opening.
struct Binding {
    winrt::weak_ref<xaml::Window> window;
    dispatching::DispatcherQueue dispatcher{nullptr};
    std::shared_ptr<MiniSide> side;  // touched on the window's thread only
};

struct State {
    std::mutex lock;
    Journal* journal = nullptr;
    std::optional<theme::SessionRequest> request;  // the last one offered
    std::shared_ptr<Binding> binding;
    bool stopped = false;
};

// Never destroyed: a copy of the engine still held at exit would release XAML objects off their thread.
State& Shared() {
    static State* state = new State;
    return *state;
}

// The binding's copy of the engine goes on its own thread. A thread already gone keeps it: leaked, never released
// from the wrong thread (as the interface service drops its windows).
void Drop(std::shared_ptr<Binding> binding) noexcept {
    if (!binding) return;
    auto* holder = new (std::nothrow) std::shared_ptr<Binding>(std::move(binding));
    if (!holder) return;
    try {
        dispatching::DispatcherQueue dispatcher = (*holder)->dispatcher;
        if (dispatcher && dispatcher.TryEnqueue(dispatching::DispatcherQueuePriority::Low, [holder]() noexcept {
                (*holder)->side.reset();
                delete holder;
            })) {
            return;
        }
    } catch (...) {
        // The thread is shutting down with its window.
    }
}

// The binding of the mini player open now, made when it is a new window; the one of a window gone goes to `dropped`.
// Under the state's lock.
std::shared_ptr<Binding> Bind(State& state, std::shared_ptr<Binding>& dropped) {
    xaml::Window window = AppWindowOf(WindowRole::Mini);
    if (state.binding && (!window || state.binding->window.get() != window)) dropped = std::move(state.binding);
    if (!window) return nullptr;
    if (state.binding) return state.binding;
    auto binding = std::make_shared<Binding>();
    binding->window = winrt::make_weak(window);
    try {
        binding->dispatcher = window.DispatcherQueue();  // documented as callable from any thread
    } catch (...) {
        return nullptr;  // a window being closed has no queue
    }
    if (!binding->dispatcher) return nullptr;
    state.binding = binding;
    return binding;
}

void WriteInspection(MiniSide& side, const theme::Inspection& inspection, const std::string& themeId) {
    ampify::ActivityScope activity("mini player inspector tree");
    std::ofstream(JournalFile("inspector-mini.txt")) << theme::InspectionText(inspection);
    theme::InspectOptions recorded;
    recorded.theme = themeId;
    recorded.originals = &side.session.Originals();
    std::ofstream(JournalFile("inspector-mini.json")) << theme::InspectionJson(inspection, recorded);
    side.journal.Write("Inspector: tree written to logs\\inspector-mini.txt and inspector-mini.json");
}

// Binds the engine's copy to the window on the first tick, then applies the request as the main window's tick does,
// minus what only the main window has (its material, the fonts published to scripts, the element picker).
void TickOn(Binding& binding, const theme::SessionRequest& request, Journal& journal) {
    if (!binding.side) binding.side = std::make_shared<MiniSide>(journal);
    MiniSide& side = *binding.side;
    if (!side.attached) {
        if (AppWindowOf(WindowRole::Mini) != binding.window.get()) return;  // another window since: stale work
        side.attached = side.backend.Attach(WindowRole::Mini);
        if (!side.attached) return;
        side.journal.Write(side.backend.SharesMainResources()
                               ? "Themes: bound on its own interface thread; the colour keys are the main window's"
                               : "Themes: bound on its own interface thread, with its own copy of the colour keys");
        // What opens later (the lyrics, Up Next, the album view, a flyout) gets the theme within a fraction of a second.
        std::weak_ptr<MiniSide> weak = binding.side;
        side.backend.WatchChanges([weak] {
            std::shared_ptr<MiniSide> live = weak.lock();
            if (!live || live->backend.WindowClosed()) return;
            ampify::ActivityScope activity("mini player theme pass after a change");
            live->session.Reapply(live->backend);
        });
    }
    if (side.backend.WindowClosed()) return;
    if (side.ticked && !side.backend.WindowShown()) return;
    side.ticked = true;
    ampify::ActivityScope activity("mini player theme tick");
    side.backend.RecheckMedia();
    std::optional<theme::Inspection> inspection = side.session.Tick(side.backend, request);
    // The window's own light or dark mode follows the scheme, as the main window's does.
    side.backend.SetAppMode(request.theme ? request.theme->dark : std::nullopt);
    if (std::string note = side.backend.TakeCaptionNote(); !note.empty()) side.journal.Write(note);
    if (std::string note = side.backend.TakeArtworkNote(); !note.empty()) side.journal.Write(note);
    if (inspection) WriteInspection(side, *inspection, request.themeId);
}

void Apply(const std::shared_ptr<Binding>& binding, const theme::SessionRequest& request, Journal& journal) noexcept {
    try {
        std::weak_ptr<Binding> weak = binding;
        binding->dispatcher.TryEnqueue(dispatching::DispatcherQueuePriority::Low, [weak, request, &journal]() noexcept {
            try {
                if (std::shared_ptr<Binding> live = weak.lock()) TickOn(*live, request, journal);
            } catch (...) {
                // Work queued on Apple Music's thread: nothing may escape it.
            }
        });
    } catch (...) {
        // The window's thread is shutting down: the next opening binds again.
    }
}

}  // namespace

void OfferMiniPlayerTheme(const theme::SessionRequest& request, Journal& journal) {
    State& state = Shared();
    std::shared_ptr<Binding> binding;
    std::shared_ptr<Binding> dropped;
    {
        std::lock_guard<std::mutex> guard(state.lock);
        if (state.stopped) return;
        state.journal = &journal;
        state.request = request;
        binding = Bind(state, dropped);
    }
    Drop(std::move(dropped));
    if (binding) Apply(binding, request, journal);
}

void MiniPlayerOpened() noexcept {
    try {
        State& state = Shared();
        std::shared_ptr<Binding> binding;
        std::shared_ptr<Binding> dropped;
        std::optional<theme::SessionRequest> request;
        Journal* journal = nullptr;
        {
            std::lock_guard<std::mutex> guard(state.lock);
            if (state.stopped || !state.request || !state.journal) return;  // the theme service has not ticked yet
            binding = Bind(state, dropped);
            request = state.request;
            journal = state.journal;
        }
        Drop(std::move(dropped));
        if (binding) Apply(binding, *request, *journal);
    } catch (...) {
        // Called from the interface service's watching thread: the next tick binds the window instead.
    }
}

void StopMiniPlayerTheme() noexcept {
    State& state = Shared();
    std::shared_ptr<Binding> dropped;
    {
        std::lock_guard<std::mutex> guard(state.lock);
        state.stopped = true;
        state.request.reset();
        dropped = std::move(state.binding);
    }
    Drop(std::move(dropped));
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
