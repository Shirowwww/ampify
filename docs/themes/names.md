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
| `.info-bar` | The banners at the top and at the bottom of the window |
| `.scrollbar` | The scroll bars of every list and page. Away from the pointer a scroll bar is a thin line, painted by `color`; under the pointer it widens to a thumb painted by the `:hover` colour, on a track painted by `background` (`:hover` too under the pointer) |

## Sidebar

| Class | Zone |
|---|---|
| `.sidebar` | The sidebar panel |
| `.navigation` | The navigation view that holds the sidebar and the pages |
| `.sidebar-header` | The top of the sidebar, with the app name |
| `.sidebar-toggle` | The button that folds the sidebar |
| `.back-button` | The back button |
| `.app-name` | The "Music" title |
| `.search-area` | The area around the search box |
| `.search-box` | The search box |
| `.sidebar-scroll` | The scrolling list of the sidebar |
| `.sidebar-items` | The items of that list |
| `.sidebar-item` | One item: Home, Radio, a playlist... |
| `.sidebar-item-artwork` | The artwork of a pinned item |
| `.sidebar-play-button` | The play button over that artwork |
| `.sidebar-footer` | The bottom of the sidebar, with the account and the Ampify entry |
| `.sidebar-footer-items` | The items of that bottom part |
| `.sidebar-separator` | The line above it |
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
| `.card-play-button` | The play button that shows over a card under the pointer |
| `.card-title`, `.card-subtitle` | The two lines of text under a card |
| `.artwork` | Every artwork, in cards, lists and the playback bar |
| `.artwork-shadow` | The shadow under an artwork |
| `.explicit-badge` | The "E" badge |
| `.favorite-button`, `.favorite-icon` | The star, and its icon |
| `.add-button` | The button that adds to the library |
| `.download-button`, `.downloaded-button`, `.download-progress` | The download button, its done state, and its progress ring |
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
| `.track-row` | One song of that list |
| `.track-play-button` | The play button of a song |
| `.reactions-button` | The reactions button of a song |
| `.song-list` | The table of the Songs page |
| `.song-row` | One row of that table |
| `.list-item` | Any row of any list, which draws its own selection (`selected-background`) |

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
| `.scrubber` | The progress bar: `color` fills the played part (the theme's accent by default), `background` the rest of the track, `.scrubber Thumb` its thumb; they hold under the pointer and while dragged |
| `.current-time`, `.duration` | The time played and the length |
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
| `.queue` | The Up Next list |

## Everything else

Any other element can be reached as `Type#Name` or by its place (`:nth-child`). `ampify devtools` writes the tree of
the page on screen with a selector for each element, and
[knowledge/apple-music-xaml-tree.md](../../knowledge/apple-music-xaml-tree.md) describes the whole tree. Template
parts and inner pieces change more often than the zones above.

## Check the names after an update

After an Apple Music update, save a few trees with the inspector (Home, an album, Search, the lyrics), then run:

```
ampify theme names C:\trees\home.json C:\trees\album.json
```

It prints how many elements each class found, and exits with code 1 when a class matches nothing: that class needs a
new path in `src/core/theme/builtins.cpp`.
