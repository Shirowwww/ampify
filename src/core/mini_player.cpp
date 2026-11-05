#include "mini_player.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cmath>
#include <mutex>
#include <system_error>
#include <utility>

#include "theme/color.hpp"

namespace ampify::mini {

namespace {

constexpr int kPad = 12;          // the window's margin
constexpr int kGap = 12;          // between the artwork and the text
constexpr int kButton = 32;       // a transport button, square
constexpr int kButtonGap = 8;
constexpr int kBackSize = 28;     // the button back to the main window
constexpr int kProgressHeight = 4;
constexpr int kMinArtwork = 48;
constexpr int kMinTextWidth = 40;
constexpr size_t kMaxQueuedCommands = 16;  // a stalled kernel must not grow the queue without limit
constexpr int kDefaultMargin = 24;         // from the screen's edges
constexpr int kTaskbarAllowance = 64;      // above the taskbar, when the work area is not known

// The height of one line of text: a size and a third more, as Windows sets a line.
int LineHeight(int size) {
    return size * 4 / 3 + 2;
}

std::string TwoDigits(int64_t value) {
    return (value < 10 ? "0" : "") + std::to_string(value);
}

int Clamp(int value, int low, int high) {
    return std::clamp(value, low, high);
}

// A length in device-independent pixels, as screen pixels at `scale`.
int Scaled(int length, double scale) {
    return static_cast<int>(std::lround(length * (scale > 0 ? scale : 1)));
}

std::string FormatInts(const Placement& placement) {
    std::string text;
    for (int value : {placement.x, placement.y, placement.width, placement.height}) {
        if (!text.empty()) text += ',';
        text += std::to_string(value);
    }
    return text;
}

// One integer of a placement, the whole token used. False on anything else.
bool ParseInt(std::string_view token, int& value) {
    if (token.empty()) return false;
    const char* end = token.data() + token.size();
    auto [pointer, error] = std::from_chars(token.data(), end, value);
    return error == std::errc() && pointer == end;
}

// Pixels of a colour variable, as XAML writes it, or the fallback when it is missing or not a colour.
std::string ColourOf(const std::map<std::string, std::string>& variables, const char* name, const std::string& fallback) {
    auto found = variables.find(name);
    if (found == variables.end()) return fallback;
    std::optional<theme::Argb> colour = theme::ParseColor(found->second);
    return colour ? theme::ToXamlColor(*colour) : fallback;
}

struct Feed {
    std::mutex mutex;
    Info info;
    Colours colours;
    std::optional<Artwork> artwork;
    std::vector<PlayerCommand> commands;
    WindowHandler window;
    std::atomic<bool> windowOpen{false};
};

// Never destroyed: the kernel's threads may still reach it while the process exits.
Feed& FeedState() {
    static Feed* feed = new Feed();
    return *feed;
}

}  // namespace

std::string FormatTime(int64_t ms) {
    int64_t seconds = std::max<int64_t>(ms, 0) / 1000;
    int64_t hours = seconds / 3600;
    int64_t minutes = seconds % 3600 / 60;
    std::string text = hours > 0 ? std::to_string(hours) + ":" + TwoDigits(minutes) : std::to_string(minutes);
    return text + ":" + TwoDigits(seconds % 60);
}

Info InfoFrom(const PlayerSnapshot& snapshot) {
    Info info;
    info.playing = snapshot.playing;
    info.elapsed = FormatTime(snapshot.positionMs);
    if (snapshot.durationMs > 0) {
        info.total = FormatTime(snapshot.durationMs);
        info.progress = std::clamp(static_cast<double>(snapshot.positionMs) / static_cast<double>(snapshot.durationMs), 0.0, 1.0);
    }
    if (snapshot.track) {
        info.hasTrack = true;
        info.trackId = snapshot.track->id;
        info.title = snapshot.track->title;
        info.artist = snapshot.track->artist;
    }
    return info;
}

Layout LayoutFor(int requestedWidth, int requestedHeight) {
    const int width = Clamp(requestedWidth, kMinWidth, kMaxWidth);
    const int height = Clamp(requestedHeight, kMinHeight, kMaxHeight);
    Layout layout;
    layout.titleSize = Clamp(height / 9, 14, 22);
    layout.artistSize = std::max(11, layout.titleSize - 4);

    const int side = std::max(kMinArtwork, std::min(height - 2 * kPad, width / 3));
    layout.artwork = {kPad, kPad, side, side};

    const int textX = kPad + side + kGap;
    const int textWidth = std::max(kMinTextWidth, width - kPad - textX);
    layout.back = {width - kPad - kBackSize, kPad, kBackSize, kBackSize};

    const int titleHeight = LineHeight(layout.titleSize);
    const int titleWidth = std::max(kMinTextWidth, layout.back.x - kGap / 2 - textX);
    layout.title = {textX, kPad, titleWidth, titleHeight};
    layout.artist = {textX, kPad + titleHeight, textWidth, LineHeight(layout.artistSize)};

    const int rowY = height - kPad - kButton;
    const int step = kButton + kButtonGap;
    layout.previous = {textX, rowY, kButton, kButton};
    layout.playPause = {textX + step, rowY, kButton, kButton};
    layout.next = {textX + 2 * step, rowY, kButton, kButton};
    layout.progress = {textX, rowY - kGap / 2 - kProgressHeight, textWidth, kProgressHeight};
    return layout;
}

std::string FormatPlacement(const Placement& placement) {
    return FormatInts(placement);
}

std::optional<Placement> ParsePlacement(std::string_view text) {
    std::array<int, 4> values{};
    for (size_t index = 0; index < values.size(); ++index) {
        size_t comma = text.find(',');
        bool last = index + 1 == values.size();
        if ((comma == std::string_view::npos) != last) return std::nullopt;
        std::string_view token = text.substr(0, comma);
        if (!ParseInt(token, values[index])) return std::nullopt;
        if (!last) text.remove_prefix(comma + 1);
    }
    return Placement{values[0], values[1], values[2], values[3]};
}

Placement FitInto(Placement placement, const Rect& area, double scale) {
    placement.width = Clamp(placement.width, Scaled(kMinWidth, scale), Scaled(kMaxWidth, scale));
    placement.height = Clamp(placement.height, Scaled(kMinHeight, scale), Scaled(kMaxHeight, scale));
    if (area.width <= 0 || area.height <= 0) return placement;
    placement.width = std::min(placement.width, area.width);
    placement.height = std::min(placement.height, area.height);
    placement.x = Clamp(placement.x, area.x, area.x + area.width - placement.width);
    placement.y = Clamp(placement.y, area.y, area.y + area.height - placement.height);
    return placement;
}

Placement DefaultPlacement(const Rect& area, double scale) {
    Placement placement;
    placement.width = Scaled(kDefaultWidth, scale);
    placement.height = Scaled(kDefaultHeight, scale);
    placement.x = area.x + area.width - placement.width - Scaled(kDefaultMargin, scale);
    placement.y = area.y + area.height - placement.height - Scaled(kTaskbarAllowance, scale);
    return FitInto(placement, area, scale);
}

Colours ColoursFrom(const std::map<std::string, std::string>& themeVariables) {
    Colours defaults;
    Colours colours;
    colours.background = ColourOf(themeVariables, "mini-background", defaults.background);
    colours.foreground = ColourOf(themeVariables, "mini-foreground", defaults.foreground);
    colours.muted = ColourOf(themeVariables, "mini-muted", defaults.muted);
    colours.accent = ColourOf(themeVariables, "mini-accent", defaults.accent);
    return colours;
}

void AddSidebarEntry(UiModel& model, const std::string& label) {
    model.AddControl(kElementId, "sidebar", label, "");
}

void PublishInfo(Info info) {
    Feed& feed = FeedState();
    std::lock_guard<std::mutex> lock(feed.mutex);
    feed.info = std::move(info);
}

Info CurrentInfo() {
    Feed& feed = FeedState();
    std::lock_guard<std::mutex> lock(feed.mutex);
    return feed.info;
}

void PublishColours(Colours colours) {
    Feed& feed = FeedState();
    std::lock_guard<std::mutex> lock(feed.mutex);
    feed.colours = std::move(colours);
}

Colours CurrentColours() {
    Feed& feed = FeedState();
    std::lock_guard<std::mutex> lock(feed.mutex);
    return feed.colours;
}

void PublishArtwork(Artwork artwork) {
    Feed& feed = FeedState();
    std::lock_guard<std::mutex> lock(feed.mutex);
    feed.artwork = std::move(artwork);
}

std::optional<Artwork> CurrentArtwork() {
    Feed& feed = FeedState();
    std::lock_guard<std::mutex> lock(feed.mutex);
    return feed.artwork;
}

void RequestCommand(PlayerCommand command) {
    Feed& feed = FeedState();
    std::lock_guard<std::mutex> lock(feed.mutex);
    if (feed.commands.size() < kMaxQueuedCommands) feed.commands.push_back(command);
}

std::vector<PlayerCommand> TakeCommands() {
    Feed& feed = FeedState();
    std::lock_guard<std::mutex> lock(feed.mutex);
    return std::exchange(feed.commands, {});
}

void InstallWindow(WindowHandler handler) {
    Feed& feed = FeedState();
    std::lock_guard<std::mutex> lock(feed.mutex);
    feed.window = std::move(handler);
}

void SetWindowOpen(bool open) {
    FeedState().windowOpen = open;
}

bool WindowOpen() {
    return FeedState().windowOpen;
}

bool RequestWindow(WindowAction action) {
    Feed& feed = FeedState();
    WindowHandler handler;
    {
        std::lock_guard<std::mutex> lock(feed.mutex);
        handler = feed.window;
    }
    if (!handler) return false;
    handler(action);  // outside the lock: the handler may publish or request in turn
    return true;
}

}  // namespace ampify::mini
