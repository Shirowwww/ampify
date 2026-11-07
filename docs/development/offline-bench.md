# Offline bench

A theme can be checked without Apple Music on screen. The inspector saves the interface tree to
`logs\inspector.json`; `ampify theme replay` loads that tree into a backend of its own and runs the real theme engine
on it, the same pass the kernel's theme service runs (`ThemeWorker::Tick`, then `ThemeSession::Tick`). Nothing is
copied from the engine: what the bench reports is what the engine decides.

One PC has one Apple Music, so checks on screen run one at a time. The bench runs anywhere, as often as needed, and
several at once.

## What it checks, and what it cannot

It checks:

- which elements each rule of `user.css` reaches, and the selectors that reach nothing;
- the values the engine writes, element by element, and the value each property ends with;
- what the live backend would refuse, with the reason: a property the element's class does not have (a `background`
  on a `TextBlock`), a value of the wrong kind, a negative padding, border or corner (which ends Apple Music at the next
  layout), a font weight out of range, a `::before` on an element that is not a panel, an animated transform on an
  element that also has a transform;
- the values kept under the pointer, while focused and while pressed (`:hover`, `:focus`, `:active`);
- that the theme settles: each pass after the first must write nothing. A theme whose values fight (a value the engine
  writes again on every pass, the 3-second freeze of `xaml-lessons.md`) does not settle, and the report names what the
  last pass still wrote;
- `@media` rules, at the recorded window size or the one given with `--window`;
- the problems the loader reports (a value it cannot read, a missing file), as the kernel logs them;
- the contrast of the texts the theme recolours, or puts on a colour of its own (see "Contrast").

It cannot check:

- real rendering: the wireframe is boxes and colours, not pixels. Brushes the inspector could not express read
  "present" and are not drawn; images, acrylic and shadows are not drawn either;
- animations, transitions and visual states as WinUI plays them: the bench sees what the engine asks for, not what the
  compositor shows;
- Apple's own code paths: Apple Music writing a value back after the theme (a lyrics line's alignment, a bound
  `Image.Source`), rebuilding a page, templates realized on the first hover, and the crash conditions found on screen;
- resource keys outside the inspector's fixed list: they are taken as the live app takes WinUI's control keys, and the
  report lists them as unverified. `resources.xaml` is only checked to be a `ResourceDictionary`;
- the mini player window, which the live theme service does not reach either;
- Apple's colours in the other mode than the dump's: a theme that turns a dark dump light (or the reverse) leaves the
  colours it does not write as recorded, and their contrast is not judged.

A screenshot is still the proof that a change works (rule 2 of `xaml-lessons.md`). The bench narrows down what to look
at, and catches a refusal or a fight before Apple Music is opened.

## Record a tree

1. Start Apple Music with Ampify, open the page to check (home, an album, a playlist, the queue).
2. Run `ampify devtools`, or set `theme.inspect = 1` in `settings.txt`. The kernel writes `logs\inspector.txt` and
   `logs\inspector.json` at its next pass, then sets `theme.inspect` back to 0.
3. Copy `inspector.json` away, under a name that says the page and the date.

Since November 2026 the dump is version 2: it also records the window size, the theme on screen, each element's flags
(panel, icon text, player window, playing row), its box (position and size against its window's root) and, for the
elements the theme on screen changed, the values the theme will give back. A dump taken under a theme can then replay
another theme on Apple Music's own values; `--as-recorded` keeps the values as they were on screen instead. A value the
element had none of its own for (it showed its template's) stays as recorded, and the report says how many.

Version 1 dumps still replay. They have no flags, no boxes and no window: panels are judged from the recorded
properties, no text is taken for icons, `@media` rules apply only with `--window`, and the wireframe is a list of
elements with their colours instead of a layout. The report's warnings say all of this.

The inspector records no text: an element's name (`x:Name`), type, sizes, colours and states. Before a dump leaves the
PC (a bug report, a test fixture), scrub it anyway:

```
ampify theme scrub inspector.json inspector-scrubbed.json
```

The scrubber replaces every text, every element name that is not an identifier (a playlist's or a person's name), every
address and file path (an image URL, a font file under the profile folder) by placeholders, the ids by the element's
index, and masks what `ampify report` masks (profile folders, account and computer names, e-mail addresses). It keeps
element names that are identifiers, types, sizes, boxes, flags, colours, numbers, keywords and visual states, and
rebuilds each selector from the scrubbed names. `ampify theme replay --tree inspector.json --scrub out.json` does the
same. Raw dumps never go into the repository; `tests/data/replay/transport-bar.json` is a scrubbed play bar of the CI
bench's Apple Music, which has no account signed in.

## Record a corpus

`scripts\record-corpus.ps1` records every page the bench should know in one go, on a PC where Apple Music runs with
Ampify:

```
pwsh -File scripts\record-corpus.ps1 -Corpus D:\corpus\2026-11-06
pwsh -File scripts\record-corpus.ps1 -Pages home,album,lyrics -States idle
pwsh -File scripts\record-corpus.ps1 -ListPages
pwsh -File scripts\record-corpus.ps1 -ScrubOnly %USERPROFILE%\Ampify\logs\inspector.json -Pages home -States idle
```

It goes from page to page in the background through UI Automation, by AutomationId (`Sidebar_Home`, `Sidebar_New`,
`Sidebar_Radio`, `Sidebar_Library_Albums`, `Sidebar_AllPlaylists`, `Sidebar_Library_Songs`, `LyricsToggleButton`,
`PlayQueueToggleButton`, `MoreButton`, `AMPIFY_ModsOpenMarketplace`...), with the elements' own patterns (Invoke,
Toggle, Select, Value), never the mouse or the keyboard. On each page it sets `theme.inspect = 1` in `settings.txt`,
waits for `logs\inspector.json` to be written again, and keeps only its scrubbed copy (`ampify theme scrub`) as
`<Corpus>\<state>\<page>.json`; `<Corpus>\index.json` lists each page, its file and why one was skipped.

| Option | What it does |
|---|---|
| `-Corpus <folder>` | Where the dumps go (`.\corpus` by default) |
| `-Pages a,b` | The pages, in this order: home, new, radio, albums, album (the library's first album), artist (that album's artist), playlists, songs, search, settings (Settings > Playback and the Ampify card), lyrics, queue, marketplace, dialog (About, closed again). All by default |
| `-States idle,playing` | Nothing playing, then a song playing (play and pause through `TransportControl_PlayPauseStop`; skipped when nothing is loaded) |
| `-Theme <id>` | The theme on screen while recording; empty by default, Apple Music's own look, on which any theme replays |
| `-Search <words>` | What the search page looks for (`piano`) |
| `-Ampify <ampify.exe>`, `-Kit <folder>` | The CLI for the scrub step (the PATH's, else `build-msvc\release`), and the Ampify folder (`%USERPROFILE%\Ampify`) |
| `-TimeoutSeconds <n>` | How long to wait for a page, a pane or a dump (30) |
| `-ListPages` | Prints the plan and stops; nothing is read or changed |
| `-ScrubOnly <inspector.json>` | Only the scrub step, on a dump already recorded, into the first state and page given |

It never buys, signs in, changes the account or writes to the library: an element whose AutomationId, or whose name for
a button or a menu entry, speaks of that (buy, account, delete, add to, love, share...) is refused before it is
invoked. It never brings Apple Music to the front, and reports a step after which Apple Music took the focus. At the
end it puts back `theme.active` and `theme.inspect`, closes the panes and the dialog it opened, goes back home and
leaves the playback as it found it. `tools/test_record_corpus.py` checks what runs without Apple Music.

## Run it

```
ampify theme replay themes\glass --tree home.json --out out\glass
ampify theme replay themes\greenroom --tree home.json --out out\greenroom --window 1280x800 --scheme Dark
ampify theme replay none --tree home.json --out out\snippet --snippet themes\snippets\bottom-playbar
ampify theme replay themes\glass --tree home.json --out out\glass --compare themes\greenroom
ampify theme replay themes\ember --tree home.json --out out\ember --artwork #204080,#40C0FF,#102030
ampify theme replay none --tree home.json --out out\removal --snippet themes\snippets\bottom-playbar --then none
ampify theme diff out\before out\after --out diff.json
```

| Option | What it does |
|---|---|
| `<folder>` or `none` | The theme folder, anywhere on disk; `none` replays snippets alone, or Apple Music's look |
| `--tree <inspector.json>` | The recorded tree |
| `--out <folder>` | Where `report.json`, `wireframe.svg` and the scratch kit go |
| `--settings <settings.txt>` | Settings to start from, such as your own `%USERPROFILE%\Ampify\settings.txt` (theme settings, accent, parts). `theme.active` is replaced |
| `--set key=value` | One setting, after the file: `--set themes.glass.blur=0`, `--set theme.accent=#FF3B30`. Checked before the replay: a `themes.<id>.` or `snippets.<id>.` key against the manifest of that theme or snippet (a key it does not declare, a number out of its bounds, a choice it does not offer, a toggle that is not on or off), a `theme.` key against the theme service's own; a misfit is a warning and the exit code 2 |
| `--snippet <folder>` | A snippet folder, enabled (repeat for several) |
| `--scheme <name>` | The colour scheme |
| `--window <W>x<H>` | The window size for `@media`, over the recorded one |
| `--passes <n>` | The most passes before giving up on settling (8) |
| `--minute <n>` | The local time for `theme.schedule`, in minutes from midnight (720) |
| `--as-recorded` | Keep a dump's values as recorded under its theme |
| `--artwork <colours>` | The artwork playing: one to three colours, `#RRGGBB` or `RRGGBB`, separated by commas (`--artwork #204080,#40C0FF,#102030`), or a palette file holding them one per line. They become `art-1` (the first), `art-2` (the second, or the first) and `art-3` (the third, or the second, darkened as the live app darkens it under white text), with `art-text` and the `-hex` forms, the way the theme service makes them from an image; `page-art-*` (the album or playlist on screen) take the same. Without it, `var(--art-1, fallback)` takes its fallback, `theme.accent = artwork` gives no accent, and the report's warnings say so |
| `--compare <folder>` | Replay a second theme on the same tree into `<out>\compare`, and write `<out>\diff.json` |
| `--then <folder>` or `--then none` | The removal check: once the theme has settled, replay this second state on the SAME tree and session, as a switch in the live app (the first comes off through the engine's restore path). The second state has the snippets of `--then-snippet` on and every other one off, so `--then none` takes everything off. The same state is also replayed on a fresh tree into `<out>\fresh`, and `<out>\diff.json` compares the two: `report.json` describes the second state, and its `leftovers` list what the first left behind. Not with `--compare` |
| `--then-snippet <folder>` | A snippet that stays or comes on in the `--then` state (repeat for several) |

The theme and the snippets are copied into `<out>\kit\themes\<id>` and `<out>\kit\snippets\<id>`, the layout of
`%USERPROFILE%\Ampify` that the worker reads. The exit code is 0 when the replay settles, 1 when it does not (or fails, or `--then` finds something left behind),
2 for a wrong use: besides a misfit `--set`, an `--out` that is a file, a snippet folder given as the theme or a theme
folder given with `--snippet` (told by their `manifest.json`), and a `--window` of 0 wide or high.

To compare the same theme on two builds, or before and after a change, replay it with each build (or before and after
the edit) into two folders, then run `ampify theme diff` on them. Its exit code is 1 when something changed.

## What the outputs hold

`report.json`:

| Member | Content |
|---|---|
| `theme`, `scheme`, `dump` | What was replayed: the theme's id and scheme, the dump's file, version, element count, whether it has boxes, the theme it was recorded under, the window |
| `settled`, `passes` | Whether a pass ended with nothing written; per pass, the changes made and the calls refused |
| `warnings` | What the dump lacks and how the bench made up for it, the loader's problems, unverified resource keys, and what the last pass still wrote when nothing settled |
| `rules` | Each declaration of the composed theme (theme, snippets, built-in layouts): selector, property, value, file and line, state or `::before`, `@media` and whether it holds at this size, how many elements it matched and their indexes in the dump (the first 200) |
| `unmatched` | The selectors that match no element of this tree, with their first file and line |
| `refused` | What was refused, by the backend (element, property, value, reason) or by the engine before it wrote (its log line), each once: `count` is how many elements it was refused on, `nodes` their indexes (the first 20) |
| `contrast`, `texts` | The legibility of the texts (see below): the dump's mode and the one the theme asks for, how many texts are fine, low, not judged or unknown; and each text that is not fine, with its colour, the colour under it, the ratio and what it needs |
| `final` | Each element the theme changed: its index, label and selector, and for each property the value before and the value at the end |
| `hover` | The values each element shows under the pointer, while focused or pressed, and the element whose state shows them |
| `artwork` | The artwork colours given with `--artwork` (`art-1`...), empty without |
| `decors`, `resources`, `dictionary` | The `::before` and `::after` images with their other values (`values`: width, height, margin, colour, opacity, alignment...), the resource keys written (before, after), whether `resources.xaml` was merged |
| `then`, `leftovers` | With `--then`: the first theme, its passes and whether it settled; and each value the second state shows that a fresh tree replaying it does not (element, property, `value` here, `fresh` there), among values, positions (`parent`, `Order`), resources, the dictionary, decors and values under the pointer |
| `journal` | The engine's log, as the kernel would write it |

### Contrast

Each visible text whose colour, or the colour under it, the theme changed is judged; a text with Apple's colours on both
sides is Apple's design and left out. The colour under a text is the nearest element up the tree with a known opaque
background, with the translucent colours between laid over it. Backgrounds the dump holds only as "present" (Apple's
brushes the inspector cannot express), images, gradients and acrylic are passed over and counted (`passedOver`): what
they draw is not known, so a text over one of them may read better or worse than the ratio says. The text's colour is
faded by its own opacity and its parents' up to that element. It needs 4.5:1, or 3:1 for large text (24 px, or 18.66
px bold) and icons. The verdict is `unknown` only when no element up the tree has a known opaque colour.

Apple's colours follow the app's mode, which the theme service sets after each pass from the theme's (`SetAppMode`), and
a `color-scheme` changes it under an element. The bench replays the mode, not the colours: the dump holds Apple's
colours in the mode it was recorded in (judged from its base background, else its primary text), and the other
mode's are not known. A text whose judgement rests on a colour of Apple's shown in the other mode is `not judged`, and
the warnings say the theme changes the mode.

Neither file names a folder of the PC it was made on: a path under the scratch kit is written relative to it
(`themes/glass/images/x.png`), and the rest of the profile folder as `%USERPROFILE%`. A report can go into a ticket as
it is.

`wireframe.svg` (open it in a browser): every laid-out, visible element drawn at its recorded box, filled with its final
background, outlined with its final border colour and width, rounded by its corner radius, faded by its opacity and its
parents'; a text is a bar in its final colour, as tall as its letters. Hovering an element shows its label and its index
in the dump. A version 1 dump gives a list instead: one row per element that shows a colour, indented by depth, with
swatches of its background, border and text colours, in bold when the theme changed it.

`diff.json` (from `--compare` or `ampify theme diff`): the values that differ (element, property, before, after; an
element only one side changed shows Apple's value on the other), the rules whose match count changed, each value of a
`::before` or `::after` that differs (`decors`: its image under `Content`, `(none)` on the side without it), and the
refusals (with their count), unmatched selectors and values under the pointer found on one side only.

## Where it lives

- `src/core/theme/replay.cpp`: `ReplayBackend`, the `TreeBackend` over a dump (`tree.hpp`). Properties the inspector
  does not read are judged from those it does: an element with a `BorderBrush` has a `BorderThickness`, one with a
  `FontSize` and a `Background` is a control and has a `Template`.
- `src/core/theme/replay_contrast.cpp`: the contrast of the texts.
- `src/cli/theme_replay.cpp`: the replay, the removal check and the report; `replay_wireframe.cpp` the wireframe,
  `theme_diff.cpp` the diff, `replay_command.cpp` the command line and its checks, `replay_util.hpp` what they share.
- `src/cli/dump_scrub.cpp`: the scrubber.
- `src/core/theme/inspector.cpp`: the dump, version 2.
- `scripts/record-corpus.ps1`: the corpus recorder (see "Record a corpus").
- `tests/replay_tests.cpp`, `replay_report_tests.cpp`, `replay_cli_tests.cpp`, `replay_contrast_tests.cpp`,
  `replay_removal_tests.cpp`.
