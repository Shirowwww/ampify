#pragma once

// Ampify's look in a terminal, for the commands that install, update or remove Ampify itself: a heading in Ampify's
// colours, a turning mark while a step runs, a green tick when it is done. Only a console that shows colours gets it
// (and never with NO_COLOR set): through a pipe, into a file or in the tests, the output stays the plain text it was.

#include <atomic>
#include <iosfwd>
#include <string>
#include <thread>

#include "internal.hpp"

namespace ampify::cli {

struct ConsoleStyle {
    bool colour = false;   // ANSI colours and cursor moves
    bool unicode = false;  // the terminal's font has the marks (Windows Terminal, a UTF-8 terminal elsewhere)
};

// The style `out` gets: only the standard output, when it is a console that shows colours.
ConsoleStyle StyleOf(const std::ostream& out);

// True when `ctx.out` gets Ampify's look (and the command may say things the styled way).
bool Styled(const Context& ctx);

// A closing line after a blank one: `text` in grey, then `command` in the accent. Styled output only.
void Note(Context& ctx, const std::string& text, const std::string& command = {});

// "Ampify  <detail>" in Ampify's accent, then a blank line. Nothing in plain output.
void Heading(Context& ctx, const std::string& detail);

// "  ✓ <text>" in green. Nothing in plain output, where the command's own sentences say it.
void Done(Context& ctx, const std::string& text);

// A step that takes a moment. Styled, its line shows a turning mark until Finish replaces it with a tick or a cross;
// plain, the line "<text>..." is printed once, as before (unless the step is not announced). Nothing else may write
// to the output meanwhile.
class Working {
public:
    // `announce`: whether plain output prints the "<text>..." line (false for a step the plain output never named).
    Working(Context& ctx, std::string text, bool announce = true);
    ~Working();
    Working(const Working&) = delete;
    Working& operator=(const Working&) = delete;

    // Ends the turning mark: a tick and `text` when `ok`, a cross and the step's own text otherwise.
    void Finish(bool ok, const std::string& text = {});

private:
    void Turn();

    Context& m_ctx;
    std::string m_text;
    ConsoleStyle m_style;
    std::atomic<bool> m_stop{false};
    std::thread m_thread;
    bool m_finished = false;
};

}  // namespace ampify::cli
