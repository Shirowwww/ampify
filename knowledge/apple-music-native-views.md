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

Neither view lives in the main window. `AMP.UI.Views.FullScreenPlayer.Instance().Show()` (what Ampify's
`Ampify.ui.applePlayer` calls) brings up a window of its own, which Apple names `FullScreenPlayerWindow` among its app
properties (`GetAppProperty`, like `MainWindow` and `MiniPlayerWindow`). Measured on 1.1792: it is created within
200 ms of `Show`, on the main window's interface thread, with the FullScreen presenter. That presenter puts its own
Win32 style back after any change; swapping it for an overlapped presenter (border, no title bar) holds. Not always:
with the main window on a second, upright screen, Apple showed it with an overlapped presenter (title bar and all)
stretched over that screen and never switched; a wait for the FullScreen presenter alone then never ends.

Word-by-word lyrics draw softer than the text around them at 125 %, Ampify's effects off or on (seen 13/11/2026):
Apple's own drawing. Line-synced lyrics are sharp, as long as anything that moves them lands on a whole pixel.

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

## What Ampify reaches

- The interface service binds `MiniPlayerWindow` for extension surfaces (`src/win/ui_service.cpp`, its own
  DispatcherQueue).
- Themes reach the mini player through a second copy of the theme engine on its thread
  (`src/win/mini_player_theme.cpp`), fed the theme service's request. It expects WinUI to give each interface thread
  its own copy of the application's resources, as UWP did for each view (not yet seen on screen): the log line "bound
  on its own interface thread" says which case holds, and when the copy is shared ("the colour keys are the main
  window's") only element rules apply there.
- `Ampify.ui.applePlayer.open()` (`src/win/apple_player.cpp`) opens Apple's full-screen player as a window over the
  main one; Now Playing uses it.
- The class names themes use for these views are in [names.md](../docs/themes/names.md).
