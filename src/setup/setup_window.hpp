#pragma once

// The window of ampify_setup.exe: one page at a time (the question, the steps while the work runs, the result), in
// Ampify's colours. Every element is a child window drawn by Ampify, so the keyboard (Tab, Space, Enter, Escape) and
// screen readers reach it as they reach a dialog. Used from the thread that created it.

#include <windows.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ampify::setup {

enum class StepState { Pending, Running, Done, Failed };

// A line set apart under the text: something to know, or a warning (marked in the accent colour).
struct Notice {
    std::wstring text;
    bool warning = false;
};

struct Page {
    std::wstring title;
    std::wstring text;
    std::vector<Notice> notices;
    std::wstring checkbox;              // a choice under the notices; empty for none
    bool checked = false;
    std::vector<std::wstring> steps;    // the steps of the work, on the page that runs it
    std::wstring primary;               // the main button; empty for none
    std::wstring secondary;             // the other button; empty for none
    bool failure = false;               // a failed run: the title takes the accent colour
};

// Closing the window, or Escape, answers Secondary.
enum class Choice { Primary, Secondary };

// What the work tells the window while it runs, from its own thread.
class Progress {
public:
    virtual ~Progress() = default;
    virtual void Set(size_t step, StepState state) = 0;
};

// For a run without a window (/silent): the steps go nowhere.
class NoProgress final : public Progress {
public:
    void Set(size_t, StepState) override {}
};

class Window {
public:
    explicit Window(const std::wstring& version);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Shows `page` and waits for a button, as a message box does. `checked` receives the state of the checkbox.
    Choice Ask(const Page& page, bool* checked = nullptr);

    // Shows `page` with its steps and runs `work` on a thread of its own; the window stays live until it returns.
    // An exception thrown by `work` comes back here.
    void Run(const Page& page, const std::function<void(Progress&)>& work);

    struct State;

private:
    std::unique_ptr<State> m_state;
};

}  // namespace ampify::setup
