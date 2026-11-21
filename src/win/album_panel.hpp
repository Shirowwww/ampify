#pragma once
// The panel apple.albumClick opens under an album of the library grid (win/album_expand.cpp): the cover, the album's
// name, artist and year, Play, Shuffle and Open album buttons, and its track list in one or two columns, as iTunes
// showed it. A track plays the album from there on a double-click, or on its number, which turns into a play button
// under the pointer; a right-click offers Play, Play Next, Play Later and Open album. The song playing shows a speaker. Built in code, in the grid's own text colour and font, so a theme reaches it as it reaches the grid.

#if defined(AMPIFY_WITH_XAML)

#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.h>

#include <functional>
#include <string>
#include <vector>

namespace ampify::win {

struct AlbumPanelTrack {
    std::wstring section;  // a heading shown above the track ("Disc 2"), empty for none
    std::wstring number;   // empty when the library gives none
    std::wstring title;
    std::wstring artist;  // shown only when it differs from the album's
    std::wstring duration;
    bool playing = false;  // the song Apple plays now: a speaker in its number's place
};

struct AlbumPanelModel {
    std::wstring title;
    std::wstring subtitle;  // artist, genre, year
    std::wstring footer;    // "12 songs, 42:13", or why there is no list
    winrt::Microsoft::UI::Xaml::Media::ImageSource artwork{nullptr};
    std::vector<AlbumPanelTrack> tracks;
};

struct AlbumPanelActions {
    std::function<void(int track, bool shuffle)> play;  // track -1: from the start
    std::function<void(int track, bool next)> queue;    // Play Next, or Play Later
    std::function<void()> open;                         // the album's page
    std::function<void()> close;
};

// The panel, `width` wide; `text` is the grid's foreground, which colours its text and, faintly, its background.
winrt::Microsoft::UI::Xaml::FrameworkElement MakeAlbumPanel(const AlbumPanelModel& model, const AlbumPanelActions& actions,
                                                            const winrt::Microsoft::UI::Xaml::Media::Brush& text,
                                                            double width);

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
