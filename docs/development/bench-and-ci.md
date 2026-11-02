# The Windows bench and CI

The bench checks on a real Windows with Apple Music installed what Ampify does: launch, injection of the kernel into
`AppleMusic.exe` and `AMPLibraryAgent.exe`, safe mode, the CLI tools, the installer, the XAML interface and themes, and
the return to a normal launch. It runs with nobody at the keyboard.

It does not replace a test on a real PC. Listening to music, Atmos, lyrics and synchronization need an Apple Music account.
See [testing-on-a-pc.md](testing-on-a-pc.md).

## Run the bench

**Automatically.** `.github/workflows/ci.yml` runs on each push to `dev` that touches the code, and by hand on any
branch. Its jobs:

| Job | What it does |
|---|---|
| Unit tests (Linux) | Builds and runs `ampify_tests`, then the Python and Node tests against the Linux `ampify` (ccache) |
| Unit tests (Windows, MSVC) | Builds and runs `ampify_tests` and `ampify_ui_thread_test` with MSVC (ccache) |
| Build the release kit, Build the bench kit | The two MSVC kits with XAML, on two machines at once (ccache) |
| Bench (launch, safety, tools, ui) | The cases on four parallel runners, each with its own Apple Music, about two minutes each |

A run takes about five minutes when the cache is warm and a header that half the code includes has not changed. The
MSVC setup (WinUI metadata, the C++/WinRT projection, ccache) is shared by the jobs through
`.github/actions/msvc-setup`.

The two builds are:

- **release**: the build that ships. It injects only on versions of `src/core/version_gate.hpp`.
- **bench**: the same code compiled with `AMPIFY_BENCH=ON`. It accepts any Apple Music version.

On the Windows runner the job installs Apple Music with `winget` (Microsoft Store source), then runs
`bench/windows/run-bench.ps1`. Results are in the job summary (a table per case) and in an artifact: `report.md`,
`results.json`, `winget.log`, `preparation.txt`, and for each case a folder with the Ampify logs and the screenshots.

**By hand.** Actions tab, workflow "CI", "Run workflow".

**On a PC.** Not advised on a PC where you use Apple Music. The bench closes Apple Music by force and empties
`%USERPROFILE%\Ampify\logs` and `state.txt` (it does not touch `settings.txt`).

```powershell
pwsh -File bench\windows\run-bench.ps1 -KitRoot <folder with release\ and bench\> -RepoRoot <repository> -OutputRoot <folder>
```

Options: `-Only <id>` runs only the named cases, `-SkipInstall` does not try to install Apple Music, `-CasesDir` picks
another case folder.

## Reading the result

| Status | Meaning |
|---|---|
| `PASS` | Verified |
| `FAIL` | Does not work. The job fails and the detail says what is missing |
| `INFO` | A finding with no verdict, for example the installed version, or suspended launch unavailable |
| `SKIP` | Impossible on this runner, with the reason (for example Apple Music absent). Does not fail the job |

If Apple Music does not install on the runner, the app cases are skipped and the job stays green: read the summary and
`winget.log`. Cases that do not need Apple Music still run. The launch cases also check that Apple Music did not crash
(Windows "Application Error" events).

## The cases

The cases are files named `NN-name.ps1` in `bench/windows/cases/`, run in file name order.

| Case | What it checks |
|---|---|
| Normal launch | Apple Music started as usual, without the launcher: no kernel log, no `ampify_kernel.dll` module, window responds |
| Release version gate | Release build: injection if the installed version is in the tested list, refusal otherwise. The app starts in both cases |
| Bench injection | Bench build: signature of `AVFoundationCF.dll` recognized, kernel loaded in both processes, suspended launch (thread count resumed), app healthy after 20 s, launcher exits cleanly |
| Background launch | The background launcher (`--background`) loads the kernel into Apple Music opened from its own icon, twice in a row, keeps one instance per session, and stops when asked |
| Safe mode | Counter at 2: the launcher starts without the kernel, then resets the counter after 20 s |
| CLI tools, build, install | `ampify.exe` commands on Windows, including `build` and `install` from a local catalog |
| Installer | `ampify_setup.exe` install and uninstall: files, sign-in entry, background launcher, user Path, uninstall entry, refusal while Apple Music is open |
| Hello extension | An example extension loads and writes its log |
| Atmos leveler | Hooks and measurement on the bench build |
| Player | Apple's player interfaces |
| XAML interface | Window found through `AMP.Utils`, work run by the DispatcherQueue, `react-panel` mounted after a hot start (`reload.txt`), `AMPIFY_...` elements found with UI Automation, the "Add one" click updated in place, the midnight theme applied, the "Ampify" card in Settings > Playback if automatic navigation gets there. One PNG screenshot per step |
| XAML inspector and examples | The inspector writes its tree, and the example themes load |

## The bench build (`AMPIFY_BENCH`)

`cmake -DAMPIFY_BENCH=ON` builds a version that accepts any non-empty Apple Music version, instead of the list in
`src/core/version_gate.hpp`. It tests the injection mechanism on the version installed on the runner, which is not always the
one in the list. It turns off the unit tests, which check the shipping policy.

**Never ship this build.** The message `AMPIFY_BENCH=ON` shows during the build, and the artifact's `BUILD_INFO.txt` says so.

The release version gate case keeps the shipping policy: on an unknown version, it checks that the release build refuses to
inject.

## Build signature (`AVFoundationCF.dll`)

The launcher and the kernel read the export table of `AVFoundationCF.dll` in the package folder at each launch. The
required names are in `src/core/build_signature.hpp`. The kernel stores no address and resolves only by name. A tested version
whose DLL does not export these names is refused. The launcher log says whether the signature is recognized, with the
number of exports found, or which exports are missing. The bench injection case checks that line on the installed version.

To mark a new version as tested, both must hold: the bench injects cleanly on it, and its signature is recognized. Then add
it to `kKnownAppVersions` (and publish it in `versions.json` of ampify-catalog).

## Add a case

A case is a file `bench/windows/cases/NN-name.ps1`.

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

- `-Id`: a short id, in lowercase with dashes. It is used by `-Only` and for the results folder name.
- `-Requires`: a list of prerequisites. Today only `apple-music` exists. To add one, add it to `Test-Requirement` in
  `run-bench.ps1`. A case without `-Requires` runs even without Apple Music.
- `Add-Check`: PASS or FAIL according to `-Passed`. `Add-Info`: a finding. `Add-Skip`: impossible case, with the reason.
- Do not depend on the state left by another case: start with `Reset-KitState`.
- Close Apple Music at the end (`Close-AppleMusic`). The bench does it between cases anyway, but the case should say
  whether it closed cleanly.
- Logs are read as the C++ writes them. In a single-quoted string, double the apostrophe (`'the app''s'`), or use double
  quotes.
- A case that needs an executable adds it to the kit layout of the "Build the ... kit" jobs in `ci.yml`, then reads it
  from `$script:KitRoot`. Add the case's id to one of the four bench groups, keeping them about even.

Helpers in `bench/windows/lib/harness.ps1`:

| Helper | Purpose |
|---|---|
| `Reset-KitState` | Stops everything, empties the logs and `state.txt` |
| `Start-Launcher -LauncherPath` | Starts `ampify_launcher.exe`. Returns the process |
| `Start-AppNormally` | Starts Apple Music as from the Start menu (no kernel) |
| `Wait-Journal -FileName -Text -TimeoutSeconds` | Waits for a line to appear in a log |
| `Test-JournalContains -FileName -Text` | True if the text is already in the log |
| `Find-JournalMatch -FileName -Pattern` | A regular expression on a log, to read a number |
| `Get-KernelState -ProcessName` | How many processes have `ampify_kernel.dll` loaded |
| `Get-AppCrashEvents -Since` | Apple Music crashes since a date |
| `Save-Screenshot -Ctx -Name` | Screenshot of the main screen, in the case's folder |
| `Close-AppleMusic` | Closes the window, then by force if needed. Returns `$true` if it left on its own |
| `Set-SafeModeCount` / `Get-SafeModeCount` | Read and write the safe mode counter |

UI Automation helpers (`bench/windows/lib/uia.ps1`): `Find-Element`, `Wait-ForElement`, `Invoke-Click`, `Set-Text`,
`Send-Key`, `Get-TreeDump`, `Write-TreeDump`, `Show-AppleMusicWindow`, and masking of secrets.

A screenshot is taken with PrintWindow after Apple Music is brought to the front and maximized. The capturing process must be
DPI aware, otherwise a screen at 125% cuts off the right of the window (see [xaml-lessons.md](xaml-lessons.md)).

## What the bench does not cover

- Playing music, Atmos, lyrics, synchronization: they need an Apple Music account. See the signed-in bench below and
  [testing-on-a-pc.md](testing-on-a-pc.md).
- Anything that touches FairPlay (`CoreFP`, `CoreADI64`, FairPlay paths): never, in CI or anywhere else.
- Apple Music versions other than the one installed on the runner. The installed version is written in `preparation.txt`
  and in the summary.
- A real desktop Windows: `windows-latest` is a server image. The Microsoft Store may refuse to install Apple Music on it.
  The bench then says so.
- Apple Music updates: the bench tests only a fresh install.

## Signed-in bench

`bench/windows/live/` holds cases that sign in to Apple Music, play music with the kernel injected, and read the real names of
interface elements. They need a **throwaway Apple account** with an active subscription or trial, never a personal one. No
workflow for it ships in the repository, because it needs the account's credentials as CI secrets.

What the scripts do, in order:

1. **Virtual audio output.** Runners have no sound card. `setup-audio.ps1` installs the VB-CABLE virtual driver (or Scream
   as a fallback), checks the archives by SHA-256, checks that a default output exists, and tries to switch on Windows
   Sonic spatial sound. No sound is recorded.
2. **Sign in** (the sign-in script in the same folder), without the kernel: restore a cached session if there is one, otherwise sign in through
   UI Automation with the Apple ID, password and two-factor code.
3. **Cases** (`run-bench.ps1 -CasesDir live\cases`):
   - audio output: default output, spatial objects, the metadata stream (the one Atmos uses in Apple Music);
   - interface tree: the UI Automation tree of the main window and of Settings > Playback in the job log;
   - playback with the kernel: bench build, Atmos leveler on. It sets Dolby Atmos to "Always on" in Apple Music's
     settings, plays about 45 s of a stereo track, 45 s of a Dolby Atmos track, then a few seconds of the stereo track to
     close the Atmos measurement. It reads `AMPLibraryAgent.log` (stereo and LUFS measurement, Atmos variant, metadata
     stream hooks, gain, no hook on CoreFP, CoreADI64 or FairPlay) and checks that Apple Music's volume slider did not move.

Pitfalls found:

- **The session is tied to the machine.** Each runner is a new machine, so a cached session often does not restore, and each
  manual run asks to sign in, often with a code.
- **Device count.** Each runner can count as a new device on the account. Apple limits associated devices. Remove old
  devices from the test account if Apple refuses.
- **Element names.** Sign-in and playback find buttons by name (English on the runner). If Apple Music's interface changes,
  the job writes what is on screen and the UIA tree to its log, so the names can be fixed.
- **Atmos on a virtual output.** If the output does not offer the metadata stream, Apple Music plays the stereo version of
  the Atmos track. The case notes it (INFO) instead of failing.
- **Names shared in French.** On a French Windows, "Lecture" names the Play buttons and also the Playback entry of
  Settings. Settings entries and dialog buttons are found by AutomationId, which stays the same in every language:
  `PlaybackNavigationViewItem` is the text of the Playback entry (its parent is the button), `PrimaryButton` and
  `CloseButton` are the buttons of a dialog.
- **Settings.** Ctrl+, opens them as a page of the main window. The sidebar's "More" button (`Sidebar_MoreButton`) opens
  the library menu, which has no Settings entry.
- **Dolby Atmos "Always on"** opens a confirmation ("Not supported on all speakers", with a "Don't show again" check box,
  `AskUserCheckbox`). Escape cancels it and keeps the old value; the case presses the primary button.
- **Disabled elements.** UI Automation's Invoke throws on a disabled element ("Unrecognized error"). In Apple Music 1.1792
  the transport controls sit at the top of the window and stay disabled until a track is loaded (their Play button is
  `TransportControl_PlayPauseStop`), and right after a start the Settings page shows its entries disabled for a few
  seconds. The scripts leave the first aside and wait for the second.
- **Forced kills in a row.** Killing Apple Music several times in a row can leave it unable to open its library (error
  9039) or stuck on a loading spinner. A normal start without Ampify, then a normal close, brings it back.
- **Access keys.** An Alt press shows Apple Music's access keys, which swallow the next keys (Ctrl+, or Escape).
  `Show-AppleMusicWindow` presses Alt only when another window is in front, and Settings open with Escape, then Ctrl+,.
- **Saved on close.** Apple Music saves a changed setting when it closes normally; a forced kill can lose it.
  `Close-AppleMusic` closes the window first.
- **Volume slider.** In Apple Music 1.1792 the slider lives in a flyout and is not in the tree while it is closed, so the
  "volume slider unchanged" check is skipped.
- **PowerShell 7.** The scripts are UTF-8 without a BOM: Windows PowerShell 5.1 misreads their accented names. Run them
  with `pwsh`.
- **Secrets.** Masked in the log. The kernel is never loaded during sign-in. No screenshot is taken between typing the
  identifier and the confirmed sign-in.
- Every sign-in attempt alerts the account's devices, so automatic runs must not type a password on their own.

## CI speed-ups, in the order they mattered

1. **Parallel bench groups** (`-Only`): four runners of about two minutes each, instead of one long bench.
2. **ccache for MSVC with Ninja:** the first run fills the cache and later runs recompile only what changed. The
   precompiled header is off with ccache, because ccache cannot cache a file compiled with a PCH.
3. **Unit tests apart from the kits:** the Windows tests build their own targets, so the bench never waits for them.
4. **Limited builds:** the bench kit builds only `ampify_launcher ampify_kernel ampify_theme_check`, and the unit test
   job skips the WinUI projection, which it does not need.
5. **One workflow, triggered on `dev` only:** work reaches `main` through `dev` with the same commits, so `main` and
   pull requests need no run of their own, and a new push cancels the run in progress.
6. What costs time: the five XAML files (`src/win/xaml_backend.cpp`, `theme_service.cpp`, `xaml_host.cpp`,
   `ui_renderer.cpp`, `ui_service.cpp`), because of WinUI's C++/WinRT headers. A change to a header they include
   recompiles them, which brings a kit build back to about six minutes.

Before a push, `scripts/hooks/pre-push` runs the fast checks (validator, packer, examples) in a few seconds, so a broken
manifest never waits for CI to say so (`git config core.hooksPath scripts/hooks`).

## CI pitfalls

- `cancel-in-progress: true`: pushing during a run cancels it. Group commits, and wait for the useful shards to end before
  pushing again.
- `always()` kept the bench of a cancelled run alive: replaced by `!cancelled()`.
- `vendor/quickjs/VERSION` was taken for the C++ header `<version>` (Windows ignores case): renamed `UPSTREAM.txt`.
- XAML linking: `RoGetActivationFactory` missing, link `runtimeobject`.
- Uninstalling crashed on `IPackageDebugSettings`: the call was removed.
- A program the bench runs blocked a job for 25 minutes for lack of a maximum delay: the helper that runs the installer
  (`Invoke-KitProgram`) has a timeout.
- The cases run under strict mode: reading a registry value that may be absent as a property throws. Check that the
  property exists first.
- Mouse drags: `SetCursorPos` with moves of zero pixels let Apple's player see the press but never follow it. Absolute
  `mouse_event` moves are what a real mouse sends.
- The helper that closes the first-run dialogs returned an array, which made a `Wait-Until` condition true: add `| Out-Null`.
- Files of the signed-in bench restarted the main bench: they are excluded from the trigger paths.
