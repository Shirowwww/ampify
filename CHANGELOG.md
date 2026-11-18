# Changelog

## 1.5.3 (2026-11-18)

Word-by-word lyrics now fill smoothly, a click on an album of your library can open its tracks right in the grid as
iTunes did, and two bugs are fixed.

- Smooth word-by-word lyrics (#5). Apple Music moves the fill of a word in small steps and leaves the end of a word
  dark until the next one starts. Ampify now lights each word at the screen's pace as it is sung, whole by its end, in
  the side panel, the full-screen player and the mini player. It is on by default; Ampify's settings, Player >
  Word-by-word lyrics (`apple.lyricsFill`), give Apple's own fill back. A line Ampify cannot match to the song keeps
  Apple's fill.
- Album track lists in the library grid (#7). With Ampify's settings, Window > Click on an album > Shows its tracks
  (`apple.albumClick`, off by default), a click on an album in Library > Albums or Library > Recently Added opens a
  panel under its row and slides the rows below down: the cover, Play, Shuffle, Open album, and the tracks in one or
  two columns, with a heading per disc for an album of several. A double-click on a track, or a click on its number,
  plays the album from there. Escape or a second click closes it.
- The mini player no longer shows a black bar with the window title once a theme is applied (#8).
- Apple Music no longer crashes while it closes after it was minimized.
- Extensions: `Ampify.library.tracks()` gives each track its `trackNumber`, `discNumber` and `albumId`.

## 1.5.2 (2026-11-17)

Sing works on your own song files, its Light quality is rebuilt, and AutoMix can mix songs that have a Dolby Atmos
version.

- Sing on local files. Apple Music plays the songs of your library files with a player of its own, not the one it
  uses for streaming, so Sing did not see a skip or a pause there: Studio could go silent, or play the last seconds of
  the previous song over the next one. Ampify now follows that player too. A skip drops what Studio held, and playing
  again after a pause goes on from the same spot. The time Apple Music shows for a local song is not yet held back by
  Studio's delay, as it is for streaming.
- Sing's Light quality is rebuilt. It used to keep each channel minus their middle, which put the two sides out of
  phase and left the music hollow. It now turns each frequency down by how alike the two channels are, with a delay
  of 5.8 ms, and leaves mono songs alone. On 46 clips of the MUSDB18 test set, the voice left drops from -9.4 to
  -15.8 dB and the music comes out much cleaner (its SDR goes from 1.0 to 6.3 dB).
- AutoMix 1.1.0 can mix songs that have a Dolby Atmos version: its new "Mix Dolby Atmos songs in stereo" setting, off
  by default, asks Apple Music for their stereo version while AutoMix is on. A song already playing in Dolby Atmos is
  never switched under way. Extensions get the same through `Ampify.automix.preferStereo(want)`.

## 1.5.1 (2026-11-17)

Sing's Studio gains a precise voice model, keeps up with pauses and skips, runs on far less of the processor, and
AutoMix mixes with it on.

- Sing and AutoMix see Apple's player again after Apple Music starts. When it opens, Apple Music restores the last
  session and often makes its player before Ampify is in: pauses and skips then went unseen, so Sing played the end of
  the previous song after a skip and AutoMix planned nothing. Ampify now finds the player by the first command Apple
  sends it.
- Sing's Studio quality works out only the part of each window that is new, which takes about a sixth of the processor
  time it took (20 ms instead of 125 ms per pass on a recent PC), and the model now gets 0.5 s of margin instead of
  0.3 s. It runs the model once when it loads it, so the first song does not wait for it.
- Studio and pauses: playing again goes on from where the sound stopped, at once, with no second of silence and no jump
  back of the position. A skip, a move in the song, or a song at another sample rate no longer lets a moment of an
  older song through, and the last second of a song is heard when nothing follows it.
- Sing no longer silences Apple Music on Dolby Atmos songs. To get a stereo stream it switched the song playing to its
  stereo version, and Apple Music does that badly: the song went on in silence with its position running three to
  four times too fast, song after song, until something else was played. Sing now asks for stereo versions only
  where no Atmos song is under way: when Apple Music starts playing, or while a stereo song plays. A song already in
  Dolby Atmos stays so, and Sing waits for the next stereo one. Turned off, Sing gives Dolby Atmos back at once.
- AutoMix mixes with Studio on. Both songs of a mix are held back alike, and the second player plays on until the end
  of the old song's fade has been heard. Before, AutoMix stood aside, or Sing dropped to Light mid-mix and gave the old
  song its voice back. With Sing on, songs that have a Dolby Atmos version are mixed too, since they play in stereo.
- Sing's Light quality takes away what both channels share between 150 Hz and 12 kHz. Measured on two songs split by
  Studio, about 5 dB less of the voice is left and the music changes less; a sound on one side alone keeps half its
  level in that band.
- Studio leaves the graphics card when it has not been used for two minutes, and the journal says when the model ran
  late and let the voice through. The journal's memory lines say how much of a process is in RAM: the graphics
  card's memory Studio uses counts in the private bytes Windows reports, never in RAM.
- Sing 1.0.1 saves its slider once it rests, not at each step.
- The lyrics blur keeps the songwriter credits at the end of a song sharp.
- Studio's voice model is now the same Voc_FT with its weights in float16: it runs in 36 ms instead of 61 per pass on
  the graphics card, its download weighs 33 MB instead of 67 (58 MB in all for Studio), and its output differs from
  the original's by about -58 dB. Studio's files are fetched once more after the update.
- Sing's popup is clearer: a karaoke switch, a status line with a dot (a ring while it gets ready, a bar while it
  downloads, the reason in colour when Sing cannot run), the voice in per cent, a line saying what each quality does,
  and the size of Studio's first download. Extensions get the same per cent: sliders take a `unit`, and
  `Ampify.audio.sing.state()` says whether Studio's files are `installed` and their `downloadSize`.
- Studio offers a second voice model, Precise: Kim's Mel-Band RoFormer, chosen under Studio in Sing's popup.
  Measured on ten MUSDB18 songs as Sing streams them, it leaves about 0.9 dB less of the voice than the standard
  model (11.1 dB against 10.3). It holds the sound back 2.8 s instead of 1.3, runs in about 57 ms per second of music
  on a recent graphics card, uses about 1.6 GB of its memory and downloads 465 MB once. It was exported again for DirectML,
  where the published export gave wrong vocals, and runs three times faster than it. The standard model stays the
  default; `Ampify.audio.sing.set` takes a `model`, and `ampify sing install` and `status` take `standard` or
  `precise`. Each part of Studio's files is now checked on its own, so a second model downloads only its model.
- Studio's Fourier transforms run about three times faster (a transform of 7680 points made of three of 1280, where
  the generic one went through a slow factor of 15), and Sing checks once a second whether the song is in Dolby
  Atmos, where it read every track's format ten times a second. Sing's own thread now takes about 5 % of one core.
- Apple Music's window thread no longer wakes in a loop while a step of Ampify is under a millisecond away, which
  cost about 1 % of a core with the music paused.
- The tempo the analysis reports is worked out twice a second instead of twenty times, and the Dolby Atmos check no
  longer allocates 64 KB each time it runs.

## 1.5.0 (2026-11-16)

Sing turns the singer's voice down, Apple Music's own lyrics are word by word again, and AutoMix's beat mixes stay on
the beat.

- Sing, in the Marketplace: the singer's voice turned down, as Apple Music Sing does on the iPhone. A microphone
  button in the playback bar opens a switch, a slider for how much of the voice stays, and two qualities. Light takes
  the centre of the stereo image out in the voice's band, at once. Studio separates the voice on the graphics card
  with UVR's MDX-Net Voc_FT model, whose files (about 92 MB) download the first time: the sound is held back 1.3 s and
  Apple's position, lyrics included, is moved back as much, so both stay on what is heard. The music stops for that
  long when Studio starts, after a move in the song and when playback resumes; a pause is silent at once. A song
  playing in Dolby Atmos is switched to its stereo version while Sing is on and back to Dolby Atmos after (Apple Music
  reloads it each time). Elements get the same through `Ampify.audio.sing`, and `ampify sing` installs, checks or
  removes the files.
- Atmos leveler: turning it off and on again kept only the songs measured in between, and forgot every other level.
- AutoMix tells a song playing in Dolby Atmos again: the description of its format ran past what Ampify read.
- Playback guard 1.0.2: leaves a song alone for 10 s after Sing changes, while Apple Music reloads it.
- AutoMix and Sing see a move made with Apple's progress bar: Apple seeks there through a function Ampify did not
  follow.
- Apple Music's own lyrics are word by word again with Ampify on: Ampify asked Apple's player for a lyrics source that
  does not exist, and Apple then kept the line-by-line lyrics for its own panel (#5). Ampify's lyrics, the Song
  panel's karaoke and `player.lyrics()` now get the word times too, from the song's start.
- AutoMix 1.0.2: a mix on the beat takes its speed from the very bars it mixes, and is planned only when every beat
  of one song falls within 25 ms of one of the other's, from Apple's beat grids. Before, the speed came from the
  tempo near the end of one song and the start of the other: over 2,337 mixes planned between chart songs, the beats
  drifted up to 34 ms apart in half of them and past 227 ms in one in ten. A pair that cannot keep together swaps on
  a bar instead.
- Fluent 2.0.4: the lyrics and Up Next panel, which Apple lays over the page, is frosted with the bar's tint, so the
  page no longer reads through the lines.
- Album colors 1.2.3: with a light scheme, the moving background's glass and the wash under an album's header are
  light, so their dark text reads (the page went dark under dark text).
- The Marketplace reads `catalog-1.5.json`, and passes over an element meant for a later Ampify instead of refusing
  the whole catalog: Ampify 1.4.1 would have shown none over Sing's new permission, so it keeps reading a catalog of
  what it can install.
- Play history, a new example app: every song played on this PC once it has played for 10 seconds, newest first and
  grouped by day, stations such as Discovery included, which Apple Music keeps no history for.

## 1.4.1 (2026-11-13)

The accent colour you pick now reaches every button, and the Song panel's lyrics stop catching the wheel by accident.

- The repeat button, the dot under shuffle, Ampify's entry in the sidebar and the Song panel's button take the
  chosen accent, and follow it when it changes, instead of staying in Apple Music's blue.
- The Song panel's lyrics scroll with the wheel only once the pointer has rested on them for a second: before that,
  the wheel scrolls the panel, so passing over the lyrics no longer moves them.
- A setting's slider shows and saves 0.329 instead of 0.32900000000000007.
- The full-screen player's full-screen button really fills the screen, over the taskbar.
- With Lyrics style's blur, word-by-word lyrics light the whole sung line again, not only its last row.

## 1.4.0 (2026-11-13)

AutoMix, steadier lyrics, and an Ampify that does less while nothing changes.

- AutoMix, in the Marketplace: each song flows into the next. It reads Apple's analysis of both songs and mixes on
  the beat when the tempos are close, swaps on a bar or crossfades when they are not. Albums in order, classical music
  and "repeat one" keep Apple's gapless playback, and Dolby Atmos songs are left alone. It is young, and the mixes
  will keep getting better. Extensions can plan their own with `Ampify.automix`.
- Lyrics in the full-screen player no longer settle a line too low before each new line, and the Song panel lights a
  whole line at once, as Apple's own pane does.
- The settings page orders the entries Ampify, its apps and the extensions add to the sidebar, as it already did for
  Apple's entries and the playback bar's buttons.
- Now Playing, Time on hover, the Song panel and AutoMix now come from the Marketplace and update from it, like every
  other element. Now Playing and Time on hover are installed and on at the first start; the copies you have stay.
- Time on hover works with the centred playback bar.
- AutoMix and Time on hover have a picture in the Marketplace.
- Themes and settings are read again only when something changes, instead of every three seconds, and nothing is
  redrawn while Apple Music's window is covered.

## 1.3.4 (2026-11-11)

Lyrics that glide from line to line, a full-screen button in the player, more room for the song on the playback bar,
and your own order for the sidebar and the playback bar's buttons.

- Full-screen lyrics move with Apple's own smooth scroll, the credits no longer overlap the last lines, and the lyrics'
  menu button stays sharp under Lyrics style's blur.
- The full-screen player has a full-screen button next to its close button.
- "Song on the playback bar" (Look tab) gives the song a quarter or a half more of the bar, under every theme.
- An Order group on the Look tab: drag the sidebar's entries or the playback bar's buttons into your own order.
- A theme's `sidebar.json` works again with Apple Music 1.1792.

## 1.3.3 (2026-11-11)

The playback bar's place back in the settings, a finished progress line under the buttons, and a moving background for
every theme.

- The playback bar's place is on the Look tab, under the theme it overrides.
- The moving background is the song's artwork as soft fields of colour drifting behind the pages, under every theme.
- Under the buttons, the progress line shows the elapsed time and the song's length at its ends, as Spotify does.
- With a number of lyrics lines around the sung one, the sung line stands in the middle of the full-screen player.
- A theme can set `"accent": "artwork"`; Artwork uses it, and Cinema puts its progress line under the buttons.
- Ampify's timers stop while Apple Music is minimised, hidden or on another desktop.

## 1.3.2 (2026-11-11)

- The song panel docks again in narrow windows (1.3.1 could leave it over the pages).
- Volume percentage sits next to the volume button (`beside: "volume"` for extension buttons).
- New stable name `.audio-formats`, and Declutter's quality badges setting hides that whole row.

## 1.3.1 (2026-11-10)

- The sidebar that opens under the pointer slides shut instead of snapping.
- A theme's colours reach the song's title and artist in the playback bar.
- Small fixes to the panels' column and to the times above a thick progress line.

## 1.3.0 (2026-11-10)

A new installer, a settings page in tabs, fewer snippets with more settings each, and one place for the playback bar
under every theme.

- Install and updates: an installer and an uninstaller with a window of their own, `ampify uninstall`, update
  offers in a notification, and Marketplace elements that update themselves once a day.
- Settings: five tabs and a search box that finds any setting, each element's settings folded under its name.
- Marketplace: a picture on every card, Install turns the element on, and a Developers tab for the examples.
- Playback bar: one setting places it for every theme, at the top, the bottom or centred under the buttons.
- Themes: Fluent rebuilt on Windows 11's materials, liquid glass for Glass, Apple's mini player themed too, colour
  filters (`filter`, `tint()`, `duotone()`), and the `:checked`, `:selected` and `:disabled` states. Ember joins
  Artwork; High contrast is gone.
- Snippets: merged into fewer, richer ones (Corners, Lyrics style, Progress line, Sidebar style, Declutter, Artwork
  colours), your choices carried over. New: Artwork filters and an equalizer in four shapes.
- Extensions: new Time on hover, A-B loop marks on the progress line, new building blocks (checkbox, progress,
  spinner, divider), a smoother Song panel, and song pages in Listening stats.
- Choose how many lyrics lines show around the sung one in the full-screen player.
- Ampify speaks Italian and Korean too, ten languages in all.

## 1.2.1 (2026-11-07)

The stable release of the 1.2.0 work. Ampify now loads in every build of an Apple Music version it was tested on.

## 1.2.0 (2026-11-07)

A top bar, grouped settings, more of Spicetify's API, and many fixes to the sidebar and the panels.

- Settings: a top bar with Apple's search and a back arrow; settings grouped under headings, with search, reset,
  export and import.
- Reliability: a freeze log, a fix for a stuck library agent (error 9039), skipping proxy detection, IPv4 first
  for songs stuck at 0:00, and warnings about programs known to crash Apple Music.
- Themes: Cosy, Fluent and Ember; a centred playback bar layout; "Follows the artwork" accent; `move-to`,
  `:current` for the sung lyrics line, `@media` for light and dark; new snippets such as Progress mascot, Lyrics
  spotlight and Compact sidebar.
- Extensions: the Song panel, Bookmarks, A-B loop, Copy link; grids, tabs, menus and lyrics nodes; much more of
  Spicetify's API (progress events, mute, clipboard, popups).
- The hover sidebar can be pinned, works on the right (Mirror) and from the keyboard.
- Titles cut with "…" scroll under the pointer, and the full-screen player's cover is sharp on large screens.

## 1.1.1 (2026-11-05)

- Shuffle takes the theme's accent while on, and the progress line reaches a rounded display's corner.
- The full-screen player in a window can be dragged, snaps with Windows' layouts and gives the main window back where
  it was.

## 1.1.0 (2026-11-04)

- Extensions can read and set shuffle, repeat and the volume, add submenus and lit buttons, and be written in
  TypeScript or JSX.
- Themes get `overflow`, `clip-path`, `:playing`, animated backgrounds and names for the lyrics.
- `ampify devtools pick` names the element under the pointer.

## 1.0.0 (2026-11-04)

The first release of Ampify: themes, extensions and a Marketplace for the official Apple Music app on Windows.

- Nine themes and 21 schemes, applied at once, plus snippets to stack on any theme.
- Discord presence, Last.fm scrobbling, a sleep timer, global shortcuts, a skip list, listening stats and new releases.
- Now Playing, an Atmos leveler and a playback guard, shipped with Ampify.
- A Marketplace inside Apple Music, the `ampify` command line, eight languages, and a one-line install without
  administrator rights.
- Nothing is written into Apple Music's package and copy protection is never touched.
