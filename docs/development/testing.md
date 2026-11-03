# Testing

Unit tests cover the portable code ([building.md](building.md#tests)). What happens inside Apple Music needs the
Windows bench, which runs unattended in CI, and a pass on a real PC for sound and looks.

## CI

`.github/workflows/ci.yml` runs on each push to `dev` that touches the code, and by hand (Actions, "CI", "Run
workflow").

| Job | What it does |
|---|---|
| Unit tests (Linux) | `ampify_tests`, then the Python and Node tests against the Linux `ampify` |
| Unit tests (Windows, MSVC) | `ampify_tests` and `ampify_ui_thread_test` |
| Build the release kit, Build the bench kit | The two MSVC kits with XAML, in parallel |
| Bench (launch, safety, tools, ui) | The bench cases on four runners, each with its own Apple Music |

A run takes about five minutes with a warm cache. The release build injects only on tested versions; the bench build
(`AMPIFY_BENCH=ON`) accepts the version installed on the runner. Results are in the job summary and in an artifact
(`report.md`, `results.json`, `winget.log`, `preparation.txt`, and per case the Ampify logs and screenshots).

## The bench

The runner installs Apple Music with `winget`, then runs `bench/windows/run-bench.ps1`. Cases are
`bench/windows/cases/NN-name.ps1`, run in name order: normal launch, release version gate, bench injection, background
launch, safe mode, CLI tools, installer, hello extension, Atmos leveler, player, XAML interface (panels, theme, the
Ampify card, with a screenshot per step) and the inspector.

| Status | Meaning |
|---|---|
| `PASS` | Verified |
| `FAIL` | Does not work; the job fails |
| `INFO` | A finding with no verdict (the installed version, for example) |
| `SKIP` | Impossible on this runner, with the reason. Does not fail the job |

If Apple Music does not install on the runner, the app cases are skipped and the job stays green: read the summary and
`winget.log`.

On a PC (not one where you use Apple Music: the bench kills it and empties `%USERPROFILE%\Ampify\logs` and
`state.txt`):

```powershell
pwsh -File bench\windows\run-bench.ps1 -KitRoot <folder with release\ and bench\> -RepoRoot <repository> -OutputRoot <folder>
```

`-Only <id>` runs named cases, `-SkipInstall` skips installing Apple Music, `-CasesDir` picks another case folder.

### Add a case

```powershell
# What the case checks, in one sentence.

Register-Case -Id 'my-case' -Title 'Sentence that says what is checked' -Requires 'apple-music' -Action {
    param($Ctx)

    Reset-KitState
    $launcher = Join-Path $script:KitRoot 'bench\ampify_launcher.exe'
    $launcherProcess = Start-Launcher -LauncherPath $launcher

    $launched = Wait-Journal -FileName 'launcher.log' -Text 'Apple Music started' -TimeoutSeconds 120
    Add-Check -Ctx $Ctx -Name 'Apple Music started' -Passed $launched

    Close-AppleMusic | Out-Null
    Add-Check -Ctx $Ctx -Name 'launcher stopped' -Passed ($launcherProcess.WaitForExit(15000))
}
```

- `-Id` is lowercase with dashes, used by `-Only`. `-Requires 'apple-music'` is the only prerequisite today
  (`Test-Requirement` in `run-bench.ps1`).
- `Add-Check` records PASS or FAIL, `Add-Info` a finding, `Add-Skip` a skip with its reason.
- Start with `Reset-KitState` and end with `Close-AppleMusic`. Cases run under strict mode.
- A case that needs a new executable adds it to the kit layout in `ci.yml`. Add its id to one of the four bench
  groups, keeping them even.

Helpers in `bench/windows/lib/harness.ps1`: `Reset-KitState`, `Start-Launcher`, `Start-AppNormally`, `Wait-Journal`,
`Test-JournalContains`, `Find-JournalMatch`, `Get-KernelState`, `Get-AppCrashEvents`, `Save-Screenshot`,
`Close-AppleMusic`, `Set-SafeModeCount`, `Get-SafeModeCount`. UI Automation helpers in `bench/windows/lib/uia.ps1`:
`Find-Element`, `Wait-ForElement`, `Invoke-Click`, `Set-Text`, `Send-Key`, `Get-TreeDump`, `Write-TreeDump`,
`Show-AppleMusicWindow`.

### Signed-in bench

`bench/windows/live/` holds cases that sign in to Apple Music, play music with the kernel and read real element names.
They need a throwaway Apple account with a subscription, never a personal one, and no workflow ships for them.
`setup-audio.ps1` installs a virtual audio output (VB-CABLE, or Scream), the sign-in script signs in through UI
Automation, then `run-bench.ps1 -CasesDir live\cases` checks the audio output, dumps the interface tree, and plays a
stereo and a Dolby Atmos track with the leveler on.

Things learned the hard way:

- A cached session rarely survives a new runner, and each runner can count as a new device on the account.
- Find Settings entries and dialog buttons by AutomationId, which does not change with the language
  (`PlaybackNavigationViewItem`, `PrimaryButton`, `CloseButton`). Ctrl+, opens Settings.
- UI Automation's Invoke throws on a disabled element. The transport controls stay disabled until a track is loaded.
- Several forced kills in a row can leave Apple Music unable to open its library (error 9039). A normal start and
  close brings it back. A forced kill can also lose a changed setting.
- An Alt press shows Apple Music's access keys, which swallow the next keys.
- Run the scripts with `pwsh` (PowerShell 7): they are UTF-8 without a BOM.

### CI notes

- `cancel-in-progress: true`: a push during a run cancels it.
- ccache with MSVC needs the precompiled header off. The five XAML files (`xaml_backend.cpp`, `theme_service.cpp`,
  `xaml_host.cpp`, `ui_renderer.cpp`, `ui_service.cpp`) are the slow ones; a header they include brings a kit build
  back to about six minutes.
- Every program the bench runs needs a timeout. In strict mode, check that a registry property exists before reading
  it.
- For mouse drags, send absolute `mouse_event` moves; `SetCursorPos` with zero-pixel moves is not seen as a drag.

## Test pass on a PC

You need Apple Music signed in, headphones or speakers, and a few Dolby Atmos songs.

1. Close Apple Music and install Ampify (install line or `ampify_setup.exe`).
2. Open Apple Music from its icon. The sidebar ends with an "Ampify" entry; Settings has an "Ampify" row that opens
   Ampify's settings page. Take a screenshot of the page.
3. In the Marketplace, use Glass, then Artwork with a song playing, then "Restore Apple Music's look".
4. Switch on the Atmos leveler under Sound in Ampify's settings, then play a stereo song and three Atmos songs for
   30 seconds each at a fixed volume. The loudness should stay about the same.
5. `ampify restore`, restart Apple Music: no Ampify entry. `ampify apply`, restart: it is back.
6. Uninstall from Settings > Apps, keep the settings, and check that Apple Music opens and plays as before.

Send back the `%USERPROFILE%\Ampify\logs\` folder (read it first: it holds Windows paths with the user name), the
screenshots, and what looked wrong. `ampify report` packs it all, masked.

What the logs should show:

- `background.log`: the launcher started, the package and version found, the signature recognized (5 exports), Apple
  Music started, threads paused and resumed in equal numbers, the kernel loaded (also in `AMPLibraryAgent.exe`).
- `AppleMusic.log`: kernel loaded, version tested, extension host ready, "Healthy after 20 s" about 20 seconds later.
- Safe mode: with Apple Music closed, set `unhealthy_launches = 2` in `%USERPROFILE%\Ampify\state.txt`, then open
  it. `background.log` says safe mode is active; after 25 seconds the counter is back to 0.
- Atmos leveler (`AMPLibraryAgent.log`): the hooks installed, a LUFS line per stereo song, and for each Atmos song the
  spatial stream, its measured LUFS against the stereo target (within 1 LU), and `gain A -> B dB` lines. A refused
  request naming CoreFP is the DRM guard at work.

`scripts\listen-test.ps1` does the listening part with measurements: it builds both kits, plays "Take Five" (stereo)
and "bad guy" (Dolby Atmos) for 45 s each with the leveler, sets Apple Music's Dolby Atmos setting to "Always on", and
writes `results\report.md`. `-Storefront` sets the account's country (`fr` by default).

## Debugging tips

The theme inspector. `ampify devtools` (or `theme.inspect = 1`) makes the running kernel write the page on screen to
`logs\inspector.txt` and `logs\inspector.json`: each element with its selector, sizes, properties and visual states
(`States=`). It is the quickest way to find a name or see why a rule does not apply.

Screenshots without bringing Apple Music forward. `PrintWindow(hwnd, dc, PW_RENDERFULLCONTENT)` captures the window
even behind others. Make the capturing process DPI aware (`SetProcessDpiAwarenessContext(-4)`), or a screen at 125%
cuts off the right side.

Driving Apple Music. UI Automation finds controls by AutomationId, the same in every language: `Sidebar_Home`,
`Sidebar_Library_Albums`, `LyricsToggleButton`, `PlayQueueToggleButton`, `TransportControl_PlayPauseStop`,
`MoreButton`, and Ampify's own `AMPIFY_ModsOpenMarketplace`. Select a list row with `SelectionItemPattern.Select`; an
Invoke on a row plays it.

Song notifications. They come from `AMPLibraryAgent.exe` (`src/win/toast_hold.cpp`), not from the interface process.
To prove one was shown or held back, read `%LOCALAPPDATA%\Microsoft\Windows\Notifications\wpndatabase.db`.

A crash in `ampify_kernel.dll`. Read the "Application Error" event (Event Viewer, Windows Logs > Application) for the
module, exception code and fault offset, then resolve the offset with the debugger of the Windows SDK, on the same
build (each MSVC binary has its `.pdb` beside it):

```powershell
& "${env:ProgramFiles(x86)}\Windows Kits\10\Debuggers\x64\cdb.exe" -z build-msvc\bench\ampify_kernel.dll `
    -y build-msvc\bench -c "ln ampify_kernel+0x13a1d; q"
```

Crash dumps, when Windows keeps them, are in `%LOCALAPPDATA%\CrashDumps`. A later build moves the code, so keep the
build that crashed.
