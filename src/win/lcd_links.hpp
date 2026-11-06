#pragma once
// The play bar's display as links, as on the Mac: a click on the song's title or on its album opens the album (Apple's
// own "go to" command, which also finds a file of the library), a click on the artist opens the artist's page (found
// in Apple's public catalog, then opened as a music: link).

#if defined(AMPIFY_WITH_XAML)

#include <string>

namespace ampify {
class Journal;
}

namespace ampify::win {

// Puts the click handlers on the display of the main window, or checks they are still there. Interface thread only.
// Cheap once they are in place: the display is looked for again only when Apple Music built a new one.
void KeepDisplayLinks(Journal& journal) noexcept;

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
