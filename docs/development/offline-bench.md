# Offline bench

A theme can be checked without Apple Music on screen. The inspector saves the interface tree to `logs\inspector.json`.
`ampify theme replay` loads that tree into a backend of its own and runs the real theme engine on it, the same pass the
kernel's theme service runs (`ThemeWorker::Tick`, then `ThemeSession::Tick`). The report shows what the engine decides.
It runs anywhere, as often as needed, and several at once.

## What it checks

- Which elements each rule of `user.css` reaches, and the selectors that reach nothing.
- The values the engine writes, element by element, and the value each property ends with.
- What the live backend would refuse, with the reason: a property the element's class does not have (a `background` on
  a `TextBlock`), a value of the wrong kind, a negative padding, border or corner (which ends Apple Music at the next
  layout), a font weight out of range, a `::before` on an element that is not a panel, an animated transform on an
  element that already has a transform.
- The values under the pointer, while focused and while pressed (`:hover`, `:focus`, `:active`).
- That the theme settles. Each pass after the first must write nothing. A theme whose values fight (the 3-second freeze
  in [xaml-lessons.md](xaml-lessons.md)) does not settle, and the report names what the last pass still wrote.
- `@media` rules, at the recorded window size or the one given with `--window`.
- The problems the loader reports (a value it cannot read, a missing file), as the kernel logs them.
- The contrast of the texts the theme recolours or puts on a colour of its own.

## What it cannot check

- Real rendering. The wireframe is boxes and colours; images, acrylic, shadows and brushes the inspector could not
  express are not drawn.
- Animations, transitions and visual states as WinUI plays them.
- Apple's own code paths: Apple Music writing a value back after the theme, rebuilding a page, templates realized on the
  first hover, and crash conditions found on screen.
- Resource keys outside the inspector's fixed list (listed as unverified). `resources.xaml` is only checked to be a
  `ResourceDictionary`.
- The mini player window.
- Apple's colours in the other mode than the dump's. Their contrast is not judged.

A screenshot is still the proof that a change works.

## Record a tree

1. Start Apple Music with Ampify and open the page to check.
2. Run `ampify devtools`, or set `theme.inspect = 1` in `settings.txt`. At its next pass the kernel writes
   `logs\inspector.txt` and `logs\inspector.json`, then sets `theme.inspect` back to 0.
3. Copy `inspector.json` away under a name that says the page and the date.

The dump is version 2. It records the window size, the theme on screen, each element's flags (panel, icon text, player
window, playing row), its box (position and size against its window's root) and, for the elements the theme on screen
changed, the values the theme will give back. A dump taken under a theme can then replay another theme on Apple Music's
own values; `--as-recorded` keeps the values as they were on screen. A value the element had none of its own for (it
showed its template's) stays as recorded, and the report says how many.

Version 1 dumps still replay, without flags, boxes or window: `@media` rules apply only with `--window`, and the
wireframe is a list. The report's warnings say what else is guessed.

Scrub a dump before it leaves the PC (a bug report, a test fixture), even though the inspector records no text:

```
ampify theme scrub inspector.json inspector-scrubbed.json
```

The scrubber replaces every text, every element name that is not an identifier (a playlist's or a person's name), every
address and file path by placeholders, and the ids by the element's index. It masks what `ampify report` masks (profile
folders, account and computer names, e-mail addresses). It keeps identifier names, types, sizes, boxes, flags, colours,
numbers, keywords and visual states, and rebuilds each selector from the scrubbed names. `ampify theme replay --tree
inspector.json --scrub out.json` does the same. Raw dumps never go into the repository.
`tests/data/replay/transport-bar.json` is a scrubbed play bar of the CI bench's Apple Music, which has no account.

## Record a corpus

`scripts\record-corpus.ps1` records every page the bench should know in one go, on a PC where Apple Music runs with
Ampify.

```
pwsh -File scripts\record-corpus.ps1 -Corpus D:\corpus\2026-11-06
pwsh -File scripts\record-corpus.ps1 -Pages home,album,lyrics -States idle
pwsh -File scripts\record-corpus.ps1 -ListPages
pwsh -File scripts\record-corpus.ps1 -ScrubOnly %USERPROFILE%\Ampify\logs\inspector.json -Pages home -States idle
```

It goes from page to page in the background through UI Automation, by AutomationId (`Sidebar_Home`,
`Sidebar_Library_Albums`, `LyricsToggleButton`, `PlayQueueToggleButton`, `MoreButton`, `AMPIFY_ModsOpenMarketplace`...)
and the elements' own patterns (Invoke, Toggle, Select, Value), without the mouse or keyboard. On each page it sets
`theme.inspect = 1`, waits for `logs\inspector.json` to be written again, and keeps only the scrubbed copy as
`<Corpus>\<state>\<page>.json`. `<Corpus>\index.json` lists each page, its file and why one was skipped.

| Option | What it does |
|---|---|
| `-Corpus <folder>` | Where the dumps go (`.\corpus` by default) |
| `-Pages a,b` | Pages, in order: home, new, radio, albums, album, artist, playlists, songs, search, settings, lyrics, queue, marketplace, dialog. All by default |
| `-States idle,playing` | Nothing playing, then a song playing (skipped when nothing is loaded) |
| `-Theme <id>` | The theme on screen while recording. Empty by default: Apple Music's own look, on which any theme replays |
| `-Search <words>` | What the search page looks for (`piano`) |
| `-Ampify <ampify.exe>`, `-Kit <folder>` | The CLI for the scrub step (the PATH's, else `build-msvc\release`), and the Ampify folder |
| `-TimeoutSeconds <n>` | How long to wait for a page, a pane or a dump (30) |
| `-ListPages`, `-ScrubOnly <file>` | Print the plan and stop; or only scrub a dump already recorded |

It never buys, signs in, changes the account or writes to the library: an element whose AutomationId, or whose name for
a button or menu entry, speaks of that (buy, account, delete, add to, love, share...) is refused before it is invoked.
It never brings Apple Music to the front and reports a step after which Apple Music took the focus. At the end it puts
back `theme.active` and `theme.inspect`, closes what it opened and leaves the playback as it found it.
`tools/test_record_corpus.py` checks what runs without Apple Music.

## Run it

```
ampify theme replay themes\glass --tree home.json --out out\glass
ampify theme replay themes\greenroom --tree home.json --out out\greenroom --window 1280x800 --scheme Dark
ampify theme replay none --tree home.json --out out\snippet --snippet themes\snippets\square-corners
ampify theme replay none --tree home.json --out out\bottom --set apple.playbar=bottom
ampify theme replay themes\glass --tree home.json --out out\glass --compare themes\greenroom
ampify theme replay themes\artwork --tree home.json --out out\artwork --artwork #204080,#40C0FF,#102030
ampify theme replay none --tree home.json --out out\removal --snippet themes\snippets\square-corners --then none
ampify theme diff out\before out\after --out diff.json
```

| Option | What it does |
|---|---|
| `<folder>` or `none` | The theme folder, anywhere on disk. `none` replays snippets alone, or Apple Music's look |
| `--tree <inspector.json>` | The recorded tree |
| `--out <folder>` | Where `report.json`, `wireframe.svg` and the scratch kit go |
| `--settings <settings.txt>` | Settings to start from, such as your own. `theme.active` is replaced |
| `--set key=value` | One setting, applied after the file (`--set themes.glass.blur=0`). A `themes.<id>.` or `snippets.<id>.` key is checked against that manifest, a `theme.` key against the theme service's own. A misfit is a warning and exit code 2 |
| `--snippet <folder>` | A snippet folder, enabled. Repeat for several |
| `--scheme <name>` | The colour scheme |
| `--window <W>x<H>` | The window size for `@media`, over the recorded one |
| `--passes <n>` | The most passes before giving up on settling (8) |
| `--minute <n>` | The local time for `theme.schedule`, in minutes from midnight (720) |
| `--as-recorded` | Keep a dump's values as recorded under its theme |
| `--artwork <colours>` | The artwork playing: one to three colours (`#RRGGBB`), comma-separated, or a palette file with one per line. They become `art-1`, `art-2`, `art-3`, `art-text` and the `-hex` forms as the theme service makes them from an image; `page-art-*` take the same. Without it, `var(--art-1, fallback)` takes its fallback, `theme.accent = artwork` gives no accent, and the warnings say so |
| `--compare <folder>` | Replay a second theme on the same tree into `<out>\compare` and write `<out>\diff.json` |
| `--then <folder>` or `none` | Removal check. Once the theme has settled, replay a second state on the same tree and session, as a switch in the live app. The same state is also replayed on a fresh tree into `<out>\fresh`, and `<out>\diff.json` compares the two. `report.json` describes the second state and its `leftovers` list what the first left behind. Not with `--compare` |
| `--then-snippet <folder>` | A snippet that stays or comes on in the `--then` state. Every other snippet is off. Repeat for several |

The theme and snippets are copied into `<out>\kit\themes\<id>` and `<out>\kit\snippets\<id>`, the layout of
`%USERPROFILE%\Ampify` that the worker reads. Exit code 0 means the replay settled. 1 means it did not, it failed, or
`--then` found something left behind. 2 means wrong use: a misfit `--set`, an `--out` that is a file, a snippet folder
given as the theme (or the reverse, told by `manifest.json`), or a `--window` of 0 wide or high.

To compare one theme across two builds or before and after an edit, replay it into two folders and run `ampify theme
diff` on them. Its exit code is 1 when something changed.

## Outputs

`report.json`:

| Member | Content |
|---|---|
| `theme`, `scheme`, `dump` | What was replayed: theme id and scheme, the dump's file, version, element count, boxes, the theme it was recorded under, the window |
| `settled`, `passes` | Whether a pass ended with nothing written; per pass, the changes made and the calls refused |
| `warnings` | What the dump lacks and how the bench made up for it, loader problems, unverified resource keys, and what the last pass still wrote when nothing settled |
| `rules` | Each declaration of the composed theme (theme, snippets, built-in layouts): selector, property, value, file and line, state or `::before`, `@media` and whether it holds at this size, match count and the first 200 indexes |
| `unmatched` | Selectors that match no element, with their first file and line |
| `refused` | What the backend or the engine refused (element, property, value, reason), each once, with `count` and the first 20 `nodes` |
| `contrast`, `texts` | Text legibility: the dump's mode and the one the theme asks for, how many texts are fine, low, not judged or unknown, and each text that is not fine with its colours, ratio and need |
| `final` | Each element the theme changed, with each property's value before and at the end |
| `hover` | The values each element shows under the pointer, while focused or pressed |
| `artwork` | The colours given with `--artwork`, empty without |
| `decors`, `resources`, `dictionary` | The `::before` and `::after` images with their values, the resource keys written, whether `resources.xaml` was merged |
| `then`, `leftovers` | With `--then`: the first theme, its passes and whether it settled; and each value the second state shows that a fresh tree does not (element, property, `value`, `fresh`) |
| `journal` | The engine's log, as the kernel would write it |

Reports name no folder of the PC they were made on (`%USERPROFILE%` stands for the profile), so one can go into a
ticket as it is.

`wireframe.svg` (open it in a browser) draws every laid-out, visible element at its recorded box with its final
colours, corners and opacity; hovering shows its label and index in the dump. `diff.json` (from `--compare` or `ampify
theme diff`) holds the values that differ, the rules whose match count changed, the `decors` that differ, and the
refusals, unmatched selectors and pointer values found on one side only.

### Contrast

Each visible text whose colour, or the colour under it, the theme changed is judged. The colour under a text is the
nearest element up the tree with a known opaque background, with translucent colours between laid over it. Backgrounds
held only as "present", images, gradients and acrylic are passed over and counted (`passedOver`). The text's colour is
faded by its own and its parents' opacity. It needs 4.5:1, or 3:1 for large text (24 px, or 18.66 px bold) and icons.
The verdict is `unknown` only when no element up the tree has a known opaque colour.

The dump holds Apple's colours in the mode it was recorded in (judged from its base background, else its primary
text). A text that rests on an Apple colour of the other mode is `not judged`, and the warnings say the theme changes
the mode (`SetAppMode`, `color-scheme`).

## Where it lives

- `src/core/theme/replay.cpp`: `ReplayBackend`, the `TreeBackend` over a dump (`tree.hpp`). Properties the inspector
  does not read are judged from those it does: an element with a `BorderBrush` has a `BorderThickness`, one with a
  `FontSize` and a `Background` is a control and has a `Template`.
- `src/core/theme/replay_contrast.cpp`: the contrast of the texts.
- `src/core/theme/inspector.cpp`: the dump.
- `src/cli/theme_replay.cpp` (replay, removal check, report), `replay_wireframe.cpp`, `theme_diff.cpp`,
  `replay_command.cpp` (command line and its checks), `replay_util.hpp`.
- `src/cli/dump_scrub.cpp`: the scrubber.
- `tests/replay_tests.cpp`, `replay_report_tests.cpp`, `replay_cli_tests.cpp`, `replay_contrast_tests.cpp`,
  `replay_removal_tests.cpp`.
