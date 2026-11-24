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

## Audio badge (`AudioBadgeControl`, `AudioQualityFlyout`)

Apple Music 1.1792 on Windows calls every lossless stream "Lossless", even 24-bit 192 kHz, although its album pages say
"Hi-Res Lossless" (`HiResLosslessText`) and its now-playing item knows the rate (`INowPlayingItem.AudioFormat` is
`HiResLossless`, `get_AudioFormatSampleRate` and `get_AudioFormatBitDepth` give HSTRINGs, the bit depth often empty).

| Part | Names |
|---|---|
| Badge | `AudioBadgeControl#AudioBadgeControl` > `Button#AudioBadgeButton` (its screen reader name is the badge's word) holding two variants: `AudioBadge` (`ImageWithLabel`: a mark and `TextBlock#AudioBadgeLabel`) and `StackPanel#AudioBadgeCompact` (the mark alone, the one the play bar shows at full width) |
| Marks | a `Canvas` without a name, 17.25 x 11, holding one `Path` of the resource `LosslessAssetPathData`; the Dolby ones are named (`DolbyImageCompact`). The Lossless and Dolby canvases take turns through their `Visibility`. The `Path` has its own size and a stretch: a geometry put in it is fitted to that box, out of shape, unless its `Stretch` goes to `None` |
| Flyout | `AudioQualityFlyout`, in a popup window of its own ("Hôte contextuel"): the large mark, a headline with the badge's word, a line such as "ALAC 24 bits/192 kHz", and `AudioQualitySettings`. Reached through `AudioBadgeButton.Flyout()` and its `Opened` event |

`Ampify.ui.playbar.audioBadge` changes the words in place and adds its icon as a second path in the mark's canvas, after Apple's (`src/win/ui_service_badge.cpp`). A geometry for a `Path` made
by `XamlReader` loads from `<Path Data='F1 M...'/>`; built as `GeometryGroup` of `PathGeometry Figures=`, the icon
did not show.

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
- `Ampify.ui.miniPlayer` (`src/win/mini_player_window.cpp`) opens the mini player with `AMP.UI.Views.MiniPlayer`
  (`IMiniPlayerStatics` 73725FCF-EAB9-5408-9E38-8D694C96CFB3, `Instance` at slot 6; `IMiniPlayer`
  5BCBEBFA-C49D-5FB2-BBC4-4BFE6F2247BB, `get_ViewModel` 6, `Show` 8) and closes it with `WM_CLOSE`, as Alt+F4 does (invoking its
  `CloseButton` through an automation peer, or by UI Automation, at times did nothing: 24/11/2026). Apple's
  own setting "Keep MiniPlayer on top of all other windows" (Settings > Advanced, `KeepMiniplayerOnTop` of
  `IGeneralSettings`) already keeps it in front. `IMiniPlayerView` also has `ExpandAlbumView`, `ShrinkAlbumView` and
  `ShowFullScreenPlayer`; its view model (`IMiniPlayerViewModel`) `IsLyricsOpen`, `IsPlayQueueOpen`, `IsInAlbumView`.
- The mini player's tree (1.1792, 350 px wide): `MiniPlayerPage > Grid > ImmersionLockup, BlurControl,
  MiniPlayerView`; in the view, `Grid#MainContent` holds `MiniNowPlayingControl#LCD` (80 px: `ArtworkImage#ActualArtwork`
  60 px at 16,16 with an `AspectRatioGrid` frame, `TrackTitleAndArtist` with the two ticker tapes and the
  `AudioBadgeControl`) and `Grid#BottomContent` (the lyrics and Up Next panes, then a `Grid` of the scrubber with
  `CurrentTime` and `Duration` and `Grid#TransportGrid`: a `StackPanel` with `VolumeControl` and `Button#ActionButton`,
  the `TransportControl`, and a `StackPanel` with the lyrics and Up Next toggles). `CustomCaptionButtons#CaptionButtons`
  stands at the top right, faded until the pointer comes.
- Apple's album pages play motion artwork in a `MediaPlayerElement#MotionArtworkPlayer`; the catalog gives it as
  `editorialVideo` of an album (`motionDetailSquare.video`, an HLS master of fragmented MP4 files, H.264 and HEVC,
  no sound), only with `extend=editorialVideo`. Ampify's mini player cover plays the H.264 file of one variant
  (`src/win/mini_motion.cpp`).
- The favourite of the song playing (the mini player's star, `src/win/player_controls.cpp`): `IItemStatusProvider`
  (5E8D6C28-32B3-525B-8C3D-D4C4BC65BB5E, `CanFavorite` slot 24, `IsFavorited` 22) on the now playing item reads it;
  `ActionHelper.CreateFavoriteOrSuggestLessAction` (statics 69765FDD-B278-535B-81F6-DF696B0BF3D3, slot 16) builds
  the action of Apple's "Favorite" menu entry, dispatched by `UIBroker.GlobalDispatchActionAsync`. Its `isFavorited`
  is the state the song has now, not the one wanted: told `true` for a song that was no favourite, it did nothing
  (seen 24/11/2026); told the state it has, `IsFavorited` reads the other one a moment later, both ways.
- WinUI's `ToggleButton` raises no `Click` when a screen reader toggles it (UI Automation `Toggle`): Ampify's toggles
  answer `Checked` and `Unchecked` instead (`OnPersonToggle` in `src/win/ui_service_player.cpp`).
- The class names themes use for these views are in [names.md](../docs/themes/names.md).
