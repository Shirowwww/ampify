#pragma once

// The mini player: Ampify's own small window, always on top, with the cover, the title and artist, previous, play or
// pause, next, the progress and a button back to the main window. This is the portable half: what it shows, where its
// parts sit, where its window goes, its colours from the active theme, and the feed between the kernel's threads and
// the interface thread that runs its window (src/win/mini_window.*, compiled with AMPIFY_WITH_XAML). Every function here is
// safe from any thread. Apple's own mini player (MiniPlayerWindow) is a different window; see knowledge/.

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "host.hpp"
#include "ui_model.hpp"

namespace ampify::mini {

inline constexpr char kPlacementKey[] = "mini.position";  // "x,y,width,height", written when the window moves
inline constexpr char kShortcutKey[] = "mini.shortcut";  // "off" or empty turns it off
// A system-wide hotkey. Ctrl+Alt is AltGr on many keyboards: whoever needs AltGr+M changes it (mini.shortcut).
inline constexpr char kDefaultShortcut[] = "Ctrl+Alt+M";
inline constexpr char kElementId[] = "ampify.mini";  // the id the shortcut and the sidebar entry are registered under

// Sizes in device-independent pixels. The window is resized to them at the screen's scale.
inline constexpr int kDefaultWidth = 340;
inline constexpr int kDefaultHeight = 140;
inline constexpr int kMinWidth = 280;
inline constexpr int kMinHeight = 120;
inline constexpr int kMaxWidth = 560;
inline constexpr int kMaxHeight = 320;

// ---- What it shows -------------------------------------------------------------------------------------------------

// The text and the numbers of the window, from what Apple's player (or the Windows media controls) reported.
struct Info {
    bool hasTrack = false;
    std::string trackId;  // TrackInfo.id: the cover follows it
    std::string title;
    std::string artist;
    bool playing = false;
    double progress = 0;  // 0 to 1, 0 when the duration is unknown
    std::string elapsed;  // "1:05", or "1:01:01" from an hour on
    std::string total;    // empty when the duration is unknown

    bool operator==(const Info&) const = default;
};

// "m:ss", or "h:mm:ss" from an hour on. Negative times read as 0:00.
std::string FormatTime(int64_t ms);
Info InfoFrom(const PlayerSnapshot& snapshot);

// ---- Where its parts sit -------------------------------------------------------------------------------------------

struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

// The rectangles of the window's parts, relative to its top left corner.
struct Layout {
    Rect artwork;
    Rect title;
    Rect artist;
    Rect progress;
    Rect previous;
    Rect playPause;
    Rect next;
    Rect back;  // the button back to the main window, top right
    int titleSize = 0;  // font sizes in pixels
    int artistSize = 0;
};

// The layout for a window of this size, which is first kept within the minimum and maximum.
Layout LayoutFor(int width, int height);

// ---- Where the window goes -----------------------------------------------------------------------------------------

struct Placement {
    int x = 0;
    int y = 0;
    int width = kDefaultWidth;
    int height = kDefaultHeight;
};

std::string FormatPlacement(const Placement& placement);
// Four integers separated by commas. Nothing when the text is anything else.
std::optional<Placement> ParsePlacement(std::string_view text);
// A placement holds screen pixels: `scale` is the screen's scale (1.25 at 125%), which the sizes of the limits follow.
// The placement with its size kept within the limits and the window moved (or shrunk) so that it lies in `area`.
Placement FitInto(Placement placement, const Rect& area, double scale = 1);
// The default place: the bottom right corner of `area`, with a margin, at the default size.
Placement DefaultPlacement(const Rect& area, double scale = 1);

// ---- Colours -------------------------------------------------------------------------------------------------------

// XAML colours, "#AARRGGBB". The theme sets them with --mini-background, --mini-foreground, --mini-muted and
// --mini-accent (docs/themes/names.md). A variable that is missing or not a colour keeps its default.
struct Colours {
    std::string background = "#FF1C1C1E";
    std::string foreground = "#FFFFFFFF";
    std::string muted = "#FFA1A1A6";
    std::string accent = "#FFFA2D48";

    bool operator==(const Colours&) const = default;
};

Colours ColoursFrom(const std::map<std::string, std::string>& themeVariables);

// ---- Feed between the kernel's threads and the mini player's ---------------------------------------------------------

struct Artwork {
    std::string trackId;
    std::string contentType;  // "image/jpeg", "image/png"
    std::string bytes;
};

void PublishInfo(Info info);
Info CurrentInfo();
void PublishColours(Colours colours);
Colours CurrentColours();
void PublishArtwork(Artwork artwork);
// True when the media session's title and artist, read with its thumbnail, are those of `track`: right after a song
// change Windows may still hand the previous song's thumbnail. The session's artist may be "Artist — Album".
// An empty session title (nothing to compare) counts as a match.
bool ArtworkIsFor(const TrackInfo& track, const std::string& sessionTitle, const std::string& sessionArtist);
std::optional<Artwork> CurrentArtwork();

// The player buttons pressed in the mini player. The kernel takes them on its JavaScript thread and sends them to Apple.
void RequestCommand(PlayerCommand command);
std::vector<PlayerCommand> TakeCommands();

// The entry in Apple Music's sidebar. A click on it sends WindowAction::Toggle, as the shortcut does.
void AddSidebarEntry(UiModel& model, const std::string& label);

// Opening and closing. The window itself is installed by src/win/mini_window.cpp; before that, requests are refused.
enum class WindowAction { Open, Close, Toggle };
using WindowHandler = std::function<void(WindowAction action)>;
void InstallWindow(WindowHandler handler);  // null removes it
// True when a handler took the request.
bool RequestWindow(WindowAction action);
// Set by the window: the kernel fetches the cover only while the window is on screen.
void SetWindowOpen(bool open);
bool WindowOpen();

}  // namespace ampify::mini
