#include "console_style.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <iterator>

#include "platform.hpp"

namespace ampify::cli {

namespace {

// Ampify's palette (docs/assets/site.css): the accent, the green of a finished step, the grey of secondary text.
constexpr const char* kAccent = "\x1b[38;2;250;45;72m";
constexpr const char* kOk = "\x1b[38;2;92;207;140m";
constexpr const char* kMuted = "\x1b[38;2;165;157;160m";
constexpr const char* kBold = "\x1b[1m";
constexpr const char* kReset = "\x1b[0m";
constexpr const char* kClearLine = "\r\x1b[2K";
constexpr const char* kHideCursor = "\x1b[?25l";
constexpr const char* kShowCursor = "\x1b[?25h";

constexpr auto kFrame = std::chrono::milliseconds(80);  // a steady turn: fast enough to read as alive, not busy
constexpr const char* kTurnUnicode[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
constexpr const char* kTurnAscii[] = {"|", "/", "-", "\\"};

const char* Tick(const ConsoleStyle& style) { return style.unicode ? "✓" : "+"; }
const char* Cross(const ConsoleStyle& style) { return style.unicode ? "✗" : "x"; }

}  // namespace

ConsoleStyle StyleOf(const std::ostream& out) {
    if (&out != &std::cout) return {};
    static const ConsoleStyle style = [] {
        const char* noColour = std::getenv("NO_COLOR");
        if (noColour && *noColour) return ConsoleStyle{};
        platform::TerminalLook look = platform::PrepareTerminal();
        return ConsoleStyle{look.colour, look.colour && look.unicode};
    }();
    return style;
}

bool Styled(const Context& ctx) { return StyleOf(ctx.out).colour; }

void Note(Context& ctx, const std::string& text, const std::string& command) {
    if (!Styled(ctx)) return;
    ctx.out << "\n  " << kMuted << text << kReset;
    if (!command.empty()) ctx.out << kAccent << command << kReset;
    ctx.out << "\n\n" << std::flush;
}

void Heading(Context& ctx, const std::string& detail) {
    ConsoleStyle style = StyleOf(ctx.out);
    if (!style.colour) return;
    ctx.out << "\n  " << kAccent << kBold << (style.unicode ? "♫ " : "") << "Ampify" << kReset << "  " << kMuted
            << detail << kReset << "\n\n"
            << std::flush;
}

void Done(Context& ctx, const std::string& text) {
    ConsoleStyle style = StyleOf(ctx.out);
    if (!style.colour) return;
    ctx.out << "  " << kOk << Tick(style) << kReset << " " << text << "\n" << std::flush;
}

Working::Working(Context& ctx, std::string text, bool announce)
    : m_ctx(ctx), m_text(std::move(text)), m_style(StyleOf(ctx.out)) {
    if (!m_style.colour) {
        if (announce) m_ctx.out << m_text << "...\n" << std::flush;
        return;
    }
    m_ctx.out << kHideCursor << std::flush;
    m_thread = std::thread([this] { Turn(); });
}

Working::~Working() { Finish(false); }

void Working::Turn() {
    size_t frames = m_style.unicode ? std::size(kTurnUnicode) : std::size(kTurnAscii);
    for (size_t i = 0; !m_stop; i++) {
        const char* mark = m_style.unicode ? kTurnUnicode[i % frames] : kTurnAscii[i % frames];
        m_ctx.out << kClearLine << "  " << kAccent << mark << kReset << " " << m_text << kMuted << "..." << kReset
                  << std::flush;
        std::this_thread::sleep_for(kFrame);
    }
}

void Working::Finish(bool ok, const std::string& text) {
    if (m_finished) return;
    m_finished = true;
    if (!m_style.colour) return;
    m_stop = true;
    if (m_thread.joinable()) m_thread.join();
    m_ctx.out << kClearLine << "  " << (ok ? kOk : kAccent) << (ok ? Tick(m_style) : Cross(m_style)) << kReset << " "
              << (ok && !text.empty() ? text : m_text) << "\n"
              << kShowCursor << std::flush;
}

}  // namespace ampify::cli
