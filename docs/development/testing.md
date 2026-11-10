# Testing

Unit tests cover the portable code (commands in [building.md](building.md#tests)). What happens inside Apple Music
needs the Windows bench, which runs unattended in CI, and a pass on a real PC for sound and looks.

## CI

`.github/workflows/ci.yml` runs on each push to `dev` that touches code, schemas, knowledge, docs, scripts or the
bench (not `bench/windows/live/`), and by hand from the Actions tab. A new push cancels the run in progress.

| Job | What it does |
|---|---|
| Unit tests (Linux) | `ampify_tests`, then the Python and Node tests and the linter against the Linux `ampify` |
| Unit tests (Windows, MSVC) | `ampify_tests` |
| Build the release kit, Build the bench kit | The two MSVC kits with XAML, in parallel |
| Split the bench cases, Bench (group 1 to 4) | `bench/windows/shards.sh` reads the case ids from the case files and deals them over four runners, each with its own Apple Music. A new case needs no edit in `ci.yml` |

The release build injects only on tested versions. The bench build (`AMPIFY_BENCH=ON`) accepts the version installed on
the runner. Results are in the job summary and an artifact: `report.md`, `results.json`, `winget.log`,
`preparation.txt`, and per case the Ampify logs and screenshots. The report records the runner image (`ImageOS`,
`ImageVersion`).

The `pre-push` hook runs `python -m unittest test_validate test_pack` in `tools/` (install `jsonschema` so the
reference comparison runs) and `node --test test_examples.mjs`. The full list of commands is in
[building.md](building.md#tests).

## Writing a unit test

`tests/test.hpp` is the harness: `TEST_CASE`, `CHECK`, `CHECK_NEAR`, `REQUIRE` (reports and ends the case, for a
precondition) and `SKIP(reason)` (ends the case as not run; the summary counts skips apart). Shared helpers are in
`tests/support.hpp`:

| Piece | Use |
|---|---|
| `ScratchDir` | An empty folder under a per-run root (`%TEMP%\ampify-tests-<pid>`), removed with the object |
| `ManifestSpec`, `ElementFolder`, `WriteElement` | An element folder with a manifest.json written from named fields |
| `ElementFixture<H>` | Elements running against a host `H`, with the journal on a string |
| `KernelFixture`, `KitFixture` | The loader with its host, hosts, settings and journal, over a kit folder |
| `LanguageScope` | Sets the interface language and puts the previous one back |
| `Sine`, `Stereo`, `Clicks`, `ToneWithClicks` | Test signals |

Other shared headers: `fake_host.hpp`, `service_host.hpp`, `pump.hpp`, `replay_support.hpp`. A new required manifest
field is one edit in `ManifestJson`, not one per test file. Windows-only tests are in `tests/win/`. Test data is in
`tests/data/`.

## The bench

The runner installs Apple Music with `winget`, then runs `bench/windows/run-bench.ps1`. Cases are
`bench/windows/cases/NN-name.ps1`, run in name order: launch and version gate (10 to 31), safe mode (40), CLI tools,
installer, hello extension and Atmos leveler (50, 51), then the interface cases (60 to 66).

| Status | Meaning |
|---|---|
| `PASS` | Verified |
| `FAIL` | Does not work; the job fails |
| `INFO` | A finding with no verdict |
| `SKIP` | Impossible on this runner, with the reason. Does not fail the job |

A case lists its needs with `-Requires`: `apple-music`, `apple-account` (needs `AMPIFY_SIGNED_IN=true`), and kit parts
`kit:release`, `kit:bench`, `kit:ui`, `kit:cli`, laid out by `scripts/bench-kit.ps1`. If Apple Music does not install on
the runner, the app cases are skipped and the job stays green, so read the summary and `winget.log`. A missing kit part
fails the case, since that is a broken build. `-Only` with an unknown id stops the bench and lists the known ids.

On a PC, not one where you use Apple Music: the bench kills it and empties `%USERPROFILE%\Ampify\logs` and `state.txt`.

```powershell
pwsh -File bench\windows\run-bench.ps1 -KitRoot <folder with release\ and bench\> -RepoRoot <repository> -OutputRoot <folder>
```

Options: `-Only <id>,...`, `-SkipInstall`, `-CasesDir` (another case folder, relative to the script).

Six cases need no Apple Music: in `65-ui-features.ps1` `ui-hover-sidebar`, `ui-bar-position`, `ui-compact-sidebar`,
`ui-current-lyrics`, and in `66-ui-release.ps1` `ui-lyrics-window`, `ui-builtin-layouts`. They ask the UI build's
`ampify.exe`, in a temporary `--kit-dir`, whether a setting takes its value and whether a snippet or theme is valid, so
they run in seconds. Whether the look is right is checked on a PC.

The `ui-inspector` case reports the stable names it finds in no tree as INFO. It opens no page, so names of the lyrics,
the queue or the full-screen player are missing from its tree.

### Add a case

```powershell
# What the case checks, in one sentence.

Register-Case -Id 'my-case' -Title 'Sentence that says what is checked' -Requires 'apple-music', 'kit:bench' -Action {
    param($Ctx)

    Reset-KitState
    $launcher = Join-Path $script:BenchKitRoot 'bench\ampify_launcher.exe'
    $launcherProcess = Start-Launcher -LauncherPath $launcher

    $launched = Wait-Journal -FileName 'launcher.log' -Text 'Apple Music started' -TimeoutSeconds 120
    Add-Check -Ctx $Ctx -Name 'Apple Music started' -Passed $launched

    Close-AppleMusic | Out-Null
    Add-Check -Ctx $Ctx -Name 'launcher stopped' -Passed ($launcherProcess.WaitForExit(15000))
}
```

- `-Id` is lowercase with dashes and is what `-Only` takes. `shards.sh` finds ids by the line starting with
  `Register-Case -Id '`, so keep that form.
- `Add-Check` records PASS or FAIL, `Add-Info` a finding, `Add-Skip` a skip with its reason.
- Start with `Reset-KitState` and end with `Close-AppleMusic`. Cases run under strict mode. A case never calls a
  function of another case: shared code goes in `lib/harness.ps1` or `lib/ui.ps1`.
- A case that waits for Apple Music's interface calls `Clear-FirstRun` right after "Apple Music started".
- A case that needs a new executable adds it to the layout in `scripts/bench-kit.ps1`.
- Every program the case runs needs a timeout. In strict mode, check that a registry property exists before reading it.

Helpers in `bench/windows/lib/harness.ps1` include `Reset-KitState`, `Start-Launcher`, `Start-AppNormally`,
`Wait-Journal`, `Test-JournalContains`, `Find-JournalMatch`, `Get-KernelState`, `Get-AppCrashEvents`, `Save-Screenshot`,
`Close-AppleMusic`, `Set-SafeModeCount`, `Get-SafeModeCount`, `Clear-FirstRun`, and `Set-Settings` with `Save-Settings`
and `Restore-Settings` (the one writer of `settings.txt`). UI Automation helpers are in `bench/windows/lib/ui.ps1`
(`Find-UiById`, `Find-UiByName`, `Invoke-UiElement`, `Show-UiAppWindow`).

### Signed-in bench

`bench/windows/live/` holds cases that play music with the kernel in a signed-in Apple Music: `audio-output`, `ui-tree`
and `playback-with-kit`. They need an Apple account with a subscription and no workflow runs them. Run them with
`run-bench.ps1 -CasesDir live\cases`, or `scripts\listen-test.ps1`. On a machine without a sound card, `audio-output`
needs a virtual output (VB-CABLE or Scream). Helpers are in `bench/windows/live/lib/uia.ps1` (`Find-Element`,
`Invoke-Click`, `Set-Text`, `Send-Key`, `Get-TreeDump`) and `audio.ps1`.

`scripts\listen-test.ps1` lays out the kit from existing builds (run `build-msvc.ps1` with and without `-Bench` first),
sets Apple Music's Dolby Atmos setting to "Always on", plays "Take Five" (stereo) and "bad guy" (Dolby Atmos) for about
45 s each with the leveler, and writes `results\report.md`. `-Storefront` sets the account's country (`fr` by default).

Lessons from these scripts:

- Find Settings entries and dialog buttons by AutomationId, which does not change with the language
  (`PlaybackNavigationViewItem`, `PrimaryButton`, `CloseButton`). Ctrl+, opens Settings.
- UI Automation's Invoke throws on a disabled element. The transport controls stay disabled until a track is loaded.
- Several forced kills in a row can leave Apple Music unable to open its library (error 9039). A normal start and close
  brings it back. A forced kill can also lose a changed setting.
- An Alt press shows Apple Music's access keys, which swallow the next keys.
- For mouse drags, send absolute `mouse_event` moves. `SetCursorPos` with zero-pixel moves is not seen as a drag.
- Run the scripts with `pwsh` (PowerShell 7): they are UTF-8 without a BOM.

## Coverage

Visual Studio's code coverage console measures `ampify_tests.exe` (no admin; the path depends on your Visual Studio
version):

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\Extensions\Microsoft\CodeCoverage.Console\Microsoft.CodeCoverage.Console.exe" collect --output coverage.cobertura.xml --output-format cobertura --nologo build-msvc\release\ampify_tests.exe
```

The Cobertura file lists every line of `src/` with its hits. A line that runs is not a line that is checked.

## Test pass on a PC

You need Apple Music signed in, headphones or speakers, and a few Dolby Atmos songs.

1. Close Apple Music and install Ampify (install line or `ampify_setup.exe`).
2. Open Apple Music from its icon. The sidebar ends with an "Ampify" entry, and Settings has an "Ampify" row that opens
   Ampify's settings page. Take a screenshot of the page.
3. In the Marketplace, use Glass, then Artwork with a song playing, then "Restore Apple Music's look".
4. Switch on the Atmos leveler under Sound (Sound and extensions tab) in Ampify's settings, then play a stereo song and three Atmos songs for 30
   seconds each at a fixed volume. The loudness should stay about the same.
5. `ampify restore`, restart Apple Music: no Ampify entry. `ampify apply`, restart: it is back.
6. Uninstall from Settings > Apps, keep the settings, and check that Apple Music opens and plays as before.

Send back the `%USERPROFILE%\Ampify\logs\` folder (read it first: it holds Windows paths with the user name), the
screenshots, and what looked wrong. `ampify report` packs it all, masked.

What the logs should show:

- Launcher log (`attach.log`, or `background.log` / `launcher.log` where the launcher runs resident or by hand): the
  package and version found, the signature recognized (5 exports), Apple Music started, threads paused and resumed in
  equal numbers, the kernel loaded (also in `AMPLibraryAgent.exe`).
- `AppleMusic.log`: kernel loaded, version tested, extension host ready, "Healthy after 8 s" about 8 seconds later.
- Safe mode: with Apple Music closed, set `unhealthy_launches = 2` in `%USERPROFILE%\Ampify\state.txt` and open it. The
  launcher's log says safe mode is active, and the counter returns to 0 after the healthy mark.
- Atmos leveler (`AMPLibraryAgent.log`): a LUFS line per stereo song, and for each Atmos song its measured LUFS against
  the stereo target (within 1 LU) and `gain A -> B dB` lines. A refused request naming CoreFP is the DRM guard at work.

## Debugging tips

- The theme inspector. `ampify devtools` (or `theme.inspect = 1`) makes the running kernel write the page on screen to
  `logs\inspector.txt` and `logs\inspector.json`, with each element's selector, sizes, properties and visual states
  (`States=`). It is the quickest way to find a name or see why a rule does not apply. The JSON replays a theme without
  Apple Music: see [offline-bench.md](offline-bench.md).
- Screenshots without bringing Apple Music forward: `PrintWindow(hwnd, dc, PW_RENDERFULLCONTENT)` captures the window
  even behind others. Make the capturing process DPI aware (`SetProcessDpiAwarenessContext(-4)`), or a screen at 125%
  cuts off the right side.
- Driving Apple Music: UI Automation finds controls by AutomationId, the same in every language (`Sidebar_Home`,
  `Sidebar_Library_Albums`, `LyricsToggleButton`, `PlayQueueToggleButton`, `TransportControl_PlayPauseStop`,
  `MoreButton`, and Ampify's `AMPIFY_ModsOpenMarketplace`). Select a list row with `SelectionItemPattern.Select`. An
  Invoke on a row plays it.
- Song notifications come from `AMPLibraryAgent.exe` (`src/win/notifications.cpp`, `src/win/toast_hold.cpp`). To prove one was shown or held back,
  read `%LOCALAPPDATA%\Microsoft\Windows\Notifications\wpndatabase.db`.
- A crash in `ampify_kernel.dll`: read the "Application Error" event (Event Viewer > Windows Logs > Application) for the
  module, exception code and fault offset. Then resolve the offset on the same build; each MSVC binary has its `.pdb`
  beside it.

```powershell
& "${env:ProgramFiles(x86)}\Windows Kits\10\Debuggers\x64\cdb.exe" -z build-msvc\bench\ampify_kernel.dll `
    -y build-msvc\bench -c "ln ampify_kernel+0x13a1d; q"
```

Crash dumps, when Windows keeps them, are in `%LOCALAPPDATA%\CrashDumps`. A later build moves the code, so keep the
build that crashed.
