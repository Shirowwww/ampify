# Element names

Apple Music names few of its elements, and the paths to them are long: the sidebar is
`SplitView#RootSplitView > Grid > Grid#PaneRoot`. Write a stable name instead, as a CSS class:

```css
.sidebar { corner-radius: 10px; }
.card:hover { transform: scale(1.04); }
.playbar Button { color: #FFFFFF; }
```

Each class stands for the path to one zone (`src/core/theme/builtins.cpp`). When an Apple Music update moves a zone,
the path changes once in Ampify and themes keep working, like Spicetify's `css-map.json`.

A class goes anywhere a part of a selector goes: `.sidebar`, `.card:hover`, `.album-header TextBlock`. It must start
its part (`Grid.card` is refused). An unknown class makes the rule ignored, with a line in the log. The names were
read on Apple Music 1.1792.

## Window

| Class | Zone |
|---|---|
| `.window` | The whole window, behind everything else |
| `.titlebar` | The title bar strip, on the pages that show it. It lies over the top of the sidebar and the playback bar: a `background` on it catches their clicks, so leave it unset |
| `.titlebar-drag-area` | The area that drags the window |
| `.caption-buttons-cover` | The strip that keeps the minimize, maximize and close buttons readable over the playback bar |
| `.modal` | Where Apple Music shows its dialogs |
| `.focus-ring` | The outline drawn around the control that has the keyboard focus |
| `.popup-scrim` | The layer behind an open menu, which catches the clicks outside it. Keep it transparent: a painted background covers the whole window while a menu is open |
| `.info-bar` | The banners at the top and at the bottom of the window |
| `.scrollbar` | The scroll bars of every list and page. Away from the pointer a scroll bar is a thin line, painted by `color`; under the pointer it widens to a thumb painted by the `:hover` colour, on a track painted by `background` (`:hover` too under the pointer) |

## Sidebar

| Class | Zone |
|---|---|
| `.sidebar` | The sidebar panel |
| `.navigation` | The navigation view that holds the sidebar and the pages |
| `.sidebar-header` | The top of the sidebar, with the app name |
| `.sidebar-toggle` | The button that folds the sidebar |
| `.sidebar-toggle-area` | The area of that button, at the top of the folded sidebar |
| `.sidebar-column` | The column of the sidebar's icons and items, all that shows of the sidebar when folded |
| `.back-button` | The back button |
| `.app-name` | The "Music" title |
| `.search-area` | The area around the search box |
| `.search-box` | The search box |
| `.search-suggestions` | The suggestions under the search box (a popup of its own, outside the window) |
| `.topbar-back` | With `apple.topbar = search`: the back arrow left of the search box, in the strip at the top of the window. It presses Apple's `.back-button`, which then stays in its place, transparent |
| `.topbar-search` | With `apple.topbar = search`: the cell in that strip that holds Apple's own `.search-box`, centred in the window. `.search-area` stays empty and hidden in the sidebar meanwhile |
| `.sidebar-scroll` | The scrolling list of the sidebar |
| `.sidebar-items` | The items of that list |
| `.sidebar-item` | One item: Home, Radio, a playlist... |
| `.sidebar-item-artwork` | The artwork of a pinned item |
| `.sidebar-selection` | The bar that marks the selected item. Each item has its own, and only the selected one shows |
| `.sidebar-count` | The number badge on a sidebar item |
| `.sidebar-play-button` | The play button over that artwork |
| `.sidebar-footer` | The bottom of the sidebar, with the account and the Ampify entry |
| `.sidebar-footer-items` | The items of that bottom part |
| `.sidebar-separator` | The line above it |
| `.sidebar-entry` | An entry Ampify adds at the bottom of the sidebar: the Marketplace, an extension's page. Folded, it is a centred icon as wide as the column less its margins; open, the full row. Its icon sits on the axis of Apple's own icons, wherever a theme moves them. Ampify sets that width and the padding itself, at each pass over the sidebar, so a `width` or `padding` here does not hold. Margin, height and colours are the theme's |
| `.sidebar-drag-indicator` | The line shown above or below a pinned item while it is dragged to a new place |
| `.account-picture` | The account picture |
| `.account-name` | The account name |

## Content and pages

| Class | Zone |
|---|---|
| `.main-column` | The whole column right of the sidebar: the playback bar and the pages, on a light gray veil |
| `.content` | The panel that holds the pages |
| `.content-background` | Its background layer |
| `.content-frame` | The frame the pages are shown in |
| `.content-shadow` | The shadow along its edge |
| `.page-background` | What Apple lays behind a page's list: an artist's video, a coloured backdrop |
| `.page` | A page (the Songs list and the settings pages, which are pages of their own types, included) |
| `.stacked-page` | A page made of stacked shelves, such as Home |
| `.list-page` | A page with a header and a track list: an album, a playlist |
| `.grid-page` | A page made of a grid of cards, such as Albums |
| `.side-by-side-page` | A page in two columns, a list on the left and its content on the right: Artists |
| `.table-page` | A page made of a table |
| `.section-page` | A page made of one section, such as a "See All" page |
| `.tracklist-page` | A page made of a plain track list |
| `.songs-page` | The Songs page |
| `.settings-page` | Every settings page: the main one and General, Playback, Restrictions, Files, Advanced, Import |
| `.settings-card` | A card of the settings pages, and a row of the main settings page |
| `.page-top-spacer` | The empty space at the top of a page, 64 pixels high, that keeps it below the playback bar |
| `.stacked-list` | The list of Home and Search, whose top padding plays the same part |
| `.page-header` | The title bar of a page, which stays at the top when the page scrolls |
| `.page-title` | The title of a page |
| `.loading` | The spinner shown while a page loads its content |
| `.frost` | The frosted layer behind the playback bar and the page headers |
| `.filter-box` | The filter box of the library pages |
| `.filter-button` | The button that opens it |
| `.sort-button` | The sort button of the Songs page |
| `.sticky-header-background` | The background of a header that stays at the top |
| `.list-page-shelves` | The shelves under the tracks of an album or a playlist (more by the artist, videos), which Apple paints on a grey band |
| `.list-header` | The column titles of the Songs list (Name, Duration, Artist...), which stay at the top as it scrolls |

## Shelves and sections

| Class | Zone |
|---|---|
| `.section-header` | The header of a section |
| `.shelf` | A row of cards that scrolls sideways |
| `.shelf-title` | Its title |
| `.shelf-grid` | The cards of the row |
| `.shelf-arrows` | The arrows that scroll it |
| `.shelf-arrow-left`, `.shelf-arrow-right` | Each arrow |

## Cards

| Class | Zone |
|---|---|
| `.card` | A square card: an album, a playlist, a station |
| `.card-brick` | A wide card, such as the categories of Search |
| `.card-wide` | A card with its artwork on the left |
| `.card-track` | A song in a shelf |
| `.card-poster` | A large editorial tile, such as the picks for you on Home: the tile and its artwork frame, which clip with their own corner. Round or square it with the covers |
| `.card-poster-chin` | The band under a large editorial tile's text, rounded at the bottom only: give it `0 0 r r` |
| `.card-play-button` | The play button that shows over a card under the pointer |
| `.cover-play-button` | The play button over an album's or a playlist's cover (not the one of its header) |
| `.card-title`, `.card-subtitle` | The two lines of text under a card |
| `.artwork` | Every artwork, in cards, lists and the playback bar, except artists' pictures |
| `.artist-artwork` | Artists' round pictures, in shelves and at the top of an artist's page. Apple lights them under the pointer with a round shape: squaring them leaves a round light over a square picture |
| `.artwork-shadow` | The shadow under an artwork |
| `.explicit-badge` | The "E" badge of a song in a list (`.lcd-explicit-badge` for the song playing) |
| `.explicit-group` | The "E" badge with the favourite star, side by side |
| `.favorite-button`, `.favorite-icon` | The star, and its icon |
| `.add-button` | The button that adds to the library |
| `.download-button`, `.downloaded-button`, `.download-progress` | The download button, its done state, and its progress ring |
| `.library-status` | The add and download buttons of an item, as one group (`.add-button` and the others stay one by one) |
| `.now-playing-indicator` | The bars that mark the song playing |

## Albums, playlists and songs

| Class | Zone |
|---|---|
| `.album-header` | The header of an album or a playlist: artwork, title, buttons |
| `.album-play-button`, `.album-shuffle-button` | Its Play and Shuffle buttons |
| `.album-description` | Its description |
| `.album-artist` | Its artist, a link that Apple writes in its own blue |
| `.add-description-button` | The button that adds a description to a playlist |
| `.share-button` | The share button |
| `.lossless-badge`, `.digital-master-badge` | The Lossless and Apple Digital Master badges |
| `.motion-artwork` | The moving artwork of some albums |
| `.artist-picture` | The picture of an artist |
| `.radio-play-button` | The play button of a station |
| `.track-list` | The list of songs |
| `.track-row` | One song of that list, on a playlist page and on an album page. Its light under the pointer is the only one: Apple's own (`.track-list-item`) is off |
| `.track-list-item` | The list row that holds a song of that list, as wide as the list |
| `.track-play-button` | The play button of a song |
| `.reactions-button` | The reactions button of a song |
| `.suggested-songs` | The box of suggested songs under a playlist |
| `.song-list` | The table of the Songs page |
| `.song-row` | One row of that table |
| `.list-item` | Any row of any list, which draws its own light under the pointer (`hover-background`, `active-background`) and its selection (`selected-background`). `.list-item:playing` is the row of the song playing |

## Search

| Class | Zone |
|---|---|
| `.search-top-spacer` | The empty space above the search page's header, 64 pixels high, that keeps it below the playback bar |
| `.search-header` | The top of the search page |
| `.search-scopes` | The buttons that choose where to search |
| `.search-scope` | One of them: Apple Music, Library, Store |
| `.search-status` | The status line ("No results"...) |
| `.search-top-result` | The tile of one of the top results, under "Top Results" |
| `.fullscreen-player` | Apple's full-screen player, a window of its own (Now Playing opens it) |
| `.fullscreen-background` | Its animated artwork background |
| `.fullscreen-artwork` | Its large artwork |
| `.fullscreen-title`, `.fullscreen-artist` | The song's title and artist under the artwork |
| `.fullscreen-transport` | Its buttons, scrubber and volume |
| `.fullscreen-time` | The elapsed and remaining times |
| `.fullscreen-lyrics` | Its lyrics column |
| `.fullscreen-exit` | Its exit and close buttons |

## Playback bar

| Class | Zone |
|---|---|
| `.playbar` | The playback bar |
| `.playbar-controls` | Its first column: shuffle, previous, play, next, repeat |
| `.now-playing` | Its second column: the song playing |
| `.playbar-extras` | Its third column: lyrics, queue, AirPlay, volume |
| `.transport` | The control that holds the buttons of the first column (`.transport AppBarButton` for the buttons) |
| `.shuffle-button`, `.shuffle-dot` | Shuffle, and the dot that shows it is on |
| `.previous-button`, `.play-button`, `.next-button` | Previous, play or pause, next. Apple Music turns them off until a song is loaded, and a button turned off gets no pointer: their `:hover` and `:active` show once a song is there |
| `.repeat-button`, `.repeat-dot` | Repeat, and its dot |
| `.lcd` | The display of the song playing |
| `.lcd-artwork` | Its artwork area |
| `.lcd-artwork-image`, `.lcd-placeholder` | The artwork, and what shows when there is none |
| `.track-text` | The title and the artist |
| `.track-lines`, `.track-line` | The block of the two lines, and each line |
| `.track-title`, `.track-artist` | Each of them |
| `.lcd-explicit-badge` | The "E" badge beside the title of an explicit song. Apple shows and hides it with each song: hide it with `opacity: 0` (and `width: 0; margin: 0` to give its room back), not `visibility`, or a later song may show it wrongly once the rule goes |
| `.scrubber` | The progress bar (with `ampify:centre-bar`, a line under the buttons): `color` fills the played part (the theme's accent by default), `background` the rest of the track, `.scrubber Thumb` its thumb; they hold under the pointer and while dragged. An image can ride at the song's position ([format.md](format.md#an-image-that-follows-the-songs-progress)) |
| `.current-time`, `.duration` | The time played and the length. With `ampify:centre-bar` they stand at the ends of the progress line, under the buttons |
| `.audio-badge` | The badge of the audio format (Lossless, Dolby Atmos) |
| `.live-badge`, `.autoplay-badge` | The Live and Autoplay badges |
| `.apple-logo` | The Apple logo shown when nothing plays |
| `.lcd-more-button` | The "..." button of the song playing |
| `.playbar-toggles` | The buttons of the third column |
| `.lyrics-button`, `.queue-button`, `.airplay-button` | Lyrics, Up Next, AirPlay |
| `.volume`, `.volume-button` | The volume control, and its speaker button |
| `.volume-slider` | The slider in the volume flyout: `color` fills the part left of the thumb, `background` the rest of the track |
| `.volume-mute-button` | The mute button on its left |

The volume slider sits in a `.flyout`. Its thumb is `.volume-slider Thumb`, and its track is
`.volume-slider Rectangle#HorizontalTrackRect` (its `height` sets the thickness). They keep the theme's colors under
the pointer and while dragged.

```css
.volume-slider {
    color: #FFFFFF;
    background: #4D4D4D;
}

.volume-slider Thumb {
    background: #FFFFFF;
}
```

## Flyouts, menus and tooltips

They open in popups, each a root of its own: `.flyout Slider` works, `.playbar .flyout` matches nothing.

| Class | Zone |
|---|---|
| `.flyout` | A flyout, such as the volume's: its `background`, `corner-radius` and `border-color` |
| `.menu` | A menu, such as the "..." menu of a song, and each of its submenus |
| `.menu-item` | An entry of a menu, including those that open a submenu or show a check mark |
| `.menu-separator` | The line between two groups of entries |
| `.tooltip` | The tip that shows under a button after a moment |

## Lyrics and queue

| Class | Zone |
|---|---|
| `.lyrics-panel` | The side panel of the lyrics and of Up Next |
| `.lyrics-panel-top-spacer` | The 64 at its top that Apple keeps for its own playback bar |
| `.lyrics` | The lyrics view |
| `.lyrics-menu-button` | Its menu button |
| `.lyrics-synced` | The lyrics that follow the song |
| `.lyrics-static` | The lyrics without timing |
| `.lyrics-message` | The text that stands in for lyrics: nothing playing, loading, not available |
| `.lyrics-line` | One line of the synced lyrics, whole; `:current` while it is the line being sung |
| `.lyrics-line-button` | The button around a line: a click goes to that moment of the song |
| `.lyrics-line-content` | What a line draws; Apple fades it by state (past or coming at half opacity, current at full) |
| `.lyrics-words` | The sung text of a line: a plain text when the song is timed by line, the word by word host when it is timed by word |
| `.lyrics-background-words` | The background vocals of a line, when it has some |
| `.lyrics-instrumental` | The three dots of an instrumental break |
| `.lyrics-footer` | The strip under the lyrics |
| `.lyrics-writers` | Its credits line |
| `.queue` | The Up Next list |
| `.queue-clear` | Its "Clear" button, in Apple's red; a theme with an accent paints it in the accent |
| `.queue-autoplay` | Its autoplay toggle (∞), blue while on; a theme with an accent gives it `checked-color` |

## Everything else

Any other element can be reached as `Type#Name` or by its place (`:nth-child`). `ampify devtools` writes the tree of
the page on screen with a selector for each element, and
[knowledge/apple-music-xaml-tree.md](../../knowledge/apple-music-xaml-tree.md) describes the whole tree. Template
parts and inner pieces change more often than the zones above.

## Mini player

Ampify's mini player is a small window that stays on top: the cover, the title and artist, previous, play or pause,
next, the progress, and a button back to the main window. It is not Apple's mini player, and element rules do not reach
it. Its four colours come from the theme's `:root` variables, written as `#RRGGBB` or `#AARRGGBB`. A missing or invalid
one keeps Ampify's default.

| Variable | What it colours | Default |
|---|---|---|
| `--mini-background` | the window and its title bar | `#1C1C1E` |
| `--mini-foreground` | the title, the buttons, and the title bar's text and close button | `#FFFFFF` |
| `--mini-muted` | the artist line, the frame of the cover, the rest of the progress bar, the title bar's text while the window is not in front | `#A1A1A6` |
| `--mini-accent` | the part of the progress bar already played | `#FA2D48` |

```css
:root {
  --mini-background: #101820;
  --mini-accent: #00AAFF;
}
```

## Check the names after an update

After an Apple Music update, save a few trees with the inspector (Home, an album, Search, the lyrics), then run:

```
ampify theme names C:\trees\home.json C:\trees\album.json
```

It prints how many elements each class found, and exits with code 1 when a class matches nothing: that class needs a
new path in `src/core/theme/builtins.cpp`.
