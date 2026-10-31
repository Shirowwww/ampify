# Apple Music's own player views

What Apple Music 1.1792 already offers beyond the main window, read from the names in its compiled XAML
(`AMP.UI.Views.pri` and `AMP.UI.Controls.pri`, dumped with `makepri dump /dt detailed`; the `.xbf` files they embed start
with a string table of every element name, state, resource key and property the view uses). Names only: no XAML is
copied here.

## How the user reaches them

1. A click on the artwork in the playback bar (`MiniPlayer_OpenMiniPlayer` in `NowPlayingControl`) opens the **mini
   player**, a second window, `MiniPlayerWindow`, with its own interface thread.
2. In the mini player, the full-screen button (`MiniPlayer_ShowFullScreenPlayerButton`, `EnterFullScreenGlyph`) turns
   that window into the **full-screen player** (`WindowPresenterFullScreen`).
3. `ExitFullScreenButton` goes back to the mini player, `CloseFullScreenButton` closes it.

Both views live in `MiniPlayerWindow`, not in the main window.

## Mini player (`MiniPlayerPage`, `MiniPlayerView`)

| Part | Names |
|---|---|
| Background | `ImmersionLockup` (the animated artwork Apple also uses on album pages) under a `BlurControl`, with `EffectOpen` and `EffectClosed` states |
| Now playing | `MiniNowPlayingControl#LCD`: artwork that grows into an album view (`ExpandArtworkStoryboard`, `ShrinkArtworkStoryboard`, `MiniPlayer_ExpandAlbum`, `MiniPlayer_CollapseAlbum`), title and artist ticker tapes, audio badge, LIVE badge |
| Panes | `LyricsView` and `PlayQueueView`, switched by the states `PaneClosed`, `LyricsViewOpen`, `PlayQueueViewOpen` |
| Sizes | states `Default` and `Expanded` (`DefaultHeight`, `AlbumViewHeight`, `ExpandedHeight`) |
| Controls | `TransportControl` (with `IsFullPlayer`), scrubber with `CurrentTime` and `Duration`, `VolumeControl`, `AirPlayButton`, `ReactionsButton`, lyrics and queue toggles, its own caption buttons |

## Full-screen player (`FullScreenPlayerPage`, `FullScreenPlayerView`)

| Part | Names |
|---|---|
| Background | `ImmersionLockup` |
| Layout | five columns: `LeftPaddingColumn`, `ArtworkTransportColumn`, `MiddlePaddingColumn`, `LyricsColumn`, `RightPaddingColumn`; states `Default` and `LyricsViewOpen` |
| Left column | `ArtworkImage#ActualArtwork` (square, `ArtworkMaxSize`), title and artist ticker tapes, `TransportGrid` with `TransportControl`, scrubber, times, `AudioBadgeControl`, `VolumeControl`, `AirPlayButton`, `ReactionsButton`, a lyrics toggle |
| Right column | `LyricsHostView` with `CurrentLineAlignment`, `EndScrollsUp`, `IsFadingEnabled` |
| Exit | `ExitFullScreenButton`, `CloseFullScreenButton` |

## Lyrics (`LyricsView`, `KaraokeLyricsView`, `LyricsListViewItem`, `KaraokeLyricsLine`)

Apple's lyrics are richer than they look in the side panel:

- **Line states**: `Past`, `Current`, `Future1`, `Future2`, `Future3`, `FutureN`, each with a faded variant, and an
  opacity resource per state (`LyricsOpacity_Past`, `LyricsOpacity_Current`, `LyricsOpacity_Future1`...).
- **Word by word** (karaoke): `WordByWordContainer`, `WordByWordHost`, with `SungColor` and `UnsungColor`.
- **Background vocals**: `BackgroundContainer`, `BackgroundWordByWordHost`, `BackgroundSungColor`.
- **Translation and pronunciation**: `TranslationContainer`, `TranslationText`, `LinePronunciationTextBlock`, switched by
  the settings `ShowLyricsTranslation` and `ShowLyricsPronunciation` and a menu button in the lyrics view.
- **Instrumental breaks**: three dots (`InstrumentalTextBlock1` to `3`) with a `Pulsing` animation.
- **States of the view**: `NoSong`, `Loading`, `NoLyrics`, `StaticLyricsLoaded`, `TimeBasedLyricsLoaded`; a footer with
  the writers (`Lyrics_WrittenBy`); a `FullScreenWidthState` that widens the lines in full screen.

`SungColor`, `UnsungColor`, `LyricsOpacity_*`, `LineFontSize` and `ArtworkMaxSize` are not in the application's resources:
each view defines them for itself. A theme cannot reach them with `colors.ini` today.

## Against Ampify's Now Playing

| | Apple's full-screen player | Ampify's Now Playing (`extensions/now-playing`) |
|---|---|---|
| Getting there | two clicks: artwork, then full screen, in a second window | one button in the playback bar, over the main window |
| Background | animated artwork (`ImmersionLockup`) | the cover, blurred, still |
| Lyrics | line states, word by word, background vocals, translation, pronunciation, instrumental dots | the current line highlighted, two lines before and four after |
| Lyrics source | Apple's, always | Apple's once its lyrics panel has been open for the song, otherwise LRCLIB |
| Controls | the whole transport, scrubber, volume, AirPlay, reactions, audio badge | none: Escape or Close |
| Reached by themes | no: the theme engine walks the main window only | yes, through the style attributes of the view |

Apple's view is the better player. What Ampify's view brings is one-click access, a place extensions can draw on, and
lyrics from LRCLIB when Apple has none.

## What Ampify could do with Apple's views

1. **Reach `MiniPlayerWindow` with themes.** The interface service already binds that window for extension surfaces
   (`win/ui_service.cpp`, its own DispatcherQueue); the theme engine would need a second tree backend posted to that
   thread, and stable names for the parts above (`.mini-player`, `.fullscreen-player`, `.fullscreen-artwork`,
   `.fullscreen-lyrics`...).
2. **Element-scoped resources.** Writing `SungColor`, `UnsungColor`, `LyricsOpacity_Current` or `LineFontSize` into a
   view's own resources would let a theme recolour and resize Apple's karaoke lyrics, the same way Ampify already keeps
   a button's colours under the pointer (it writes `{Control}{Property}PointerOver` keys into that button's resources).
3. **One click to Apple's full screen.** A playback-bar button that opens Apple's full-screen player directly, instead
   of an Ampify view, if Apple's commands for the mini player and the full-screen switch can be invoked.
