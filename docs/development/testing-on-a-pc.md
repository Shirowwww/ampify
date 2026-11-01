# Testing on a PC

The Windows bench ([bench-and-ci.md](bench-and-ci.md)) covers what can run with nobody present. Real playback, sound and
how the interface looks need a PC with Apple Music, a signed-in account and headphones or speakers. This page has the full
test pass for a tester, then the detailed checks for each area.

Logs and reports contain Windows paths, with the user name in them. The test kit replaces the user name in its reports
(except in `trace.etl`, a binary file). Read any other log before you send it.

## Full test pass (about 20 minutes, in one sitting)

You need the PC, Apple Music for Windows signed in, headphones or speakers, and a few Dolby Atmos songs. You can stop at any
step. Say where.

1. **Download the zip** `ampify-<version>-windows-x64.zip`. Right-click it, then "Extract all". Always work from the
   extracted folder, never from inside the zip.
2. **Close Apple Music** if it is open.
3. **Install Ampify.** Double-click `ampify_setup.exe`, then answer Yes. If Windows shows "Windows protected your PC",
   click "More info", then "Run anyway": Ampify is not code-signed yet. At the end, a window says that "Ampify for Apple
   Music" is installed.
4. **Prepare the test.** Double-click `prepare-test.cmd`. A black window copies the example extensions and themes, then
   switches on the Atmos leveler and a few extensions. It ends with "Ready". Press a key to close it.
5. **Open Apple Music with Ampify.** In the Start menu, search "Apple Music with Ampify" and start it. Apple Music should
   open as usual, maybe one or two seconds slower.
6. **Look at what Ampify adds.** A panel called "React panel" appears on the right of the window. Click "Add one": the
   counter goes to 1. Then open Apple Music's Settings, Playback page. At the bottom, a card called "Ampify" lists the
   extensions with a switch each. Switch "React panel" off: the panel must disappear at once. Switch it on again. Take a
   screenshot of the card.
7. **Change the theme.** Click the small bag to the right of the lyrics and queue buttons at the top: the Marketplace
   page opens. Under "Themes", click "Use this theme" below "Glass": the sidebar turns night blue and the playback bar
   becomes glass. Also try "Artwork" with a song playing: its artwork becomes the background. Click "Restore Apple Music's
   look": the original colors return. "Back" closes the page. If one of the panel, the card or the Marketplace is missing,
   note it and continue.
8. **Close Apple Music.** The next test starts it again by itself.
9. **Run the test kit.** Double-click `Ampify-Test.exe`, then answer Yes. Windows may ask for permission to record a trace:
   answer Yes (or No if you prefer, the test continues without). Apple Music opens by itself. A small window stays on top:
   do not click OK yet.
10. **Listen.** Set Apple Music's volume to a comfortable level and do not touch it again. Play a stereo song and listen for
    30 seconds. Then play three Dolby Atmos songs, 30 seconds each (Apple Music's "Dolby Atmos" playlist has plenty). At
    each change, look at Apple Music's volume slider and Windows' one: they must never move. Listen to whether the loudness
    stays about the same from one song to the next. A Windows notification must announce each new song. Also try pause, play
    and next. At the end, open Settings, Playback page, once.
11. **End the test.** Click OK in the small window. The test writes its report and opens the `reports` folder. It holds a
    `.zip` file with today's date. That file is the one to send.
12. **Uninstall.** Close Apple Music. Open Windows Settings, Apps, Installed apps. Find "Ampify for Apple Music", click the
    three dots, then Uninstall. Answer Yes. When it asks whether to keep your settings and reports, answer Yes.
13. **Check that Apple Music is back to normal.** Start Apple Music from its usual icon. It must open and play as before,
    with no panel and no Ampify card. The "Apple Music with Ampify" shortcut must be gone from the Start menu.

### What to send back

- The `.zip` file from the `reports` folder (step 11). It is also in `%USERPROFILE%\Ampify\reports\` if you closed the
  window.
- The screenshot of the Ampify card (step 6), and a screenshot of anything that looked odd.
- A short answer to these questions. Did the panel, the Ampify card and the theme choice appear? Did the "midnight" theme
  change the colors? Did a volume slider move on its own? Was the loudness steady between the stereo song and the Atmos
  songs? If not, which one was too loud or too quiet? Did the uninstall go well, and did Apple Music start normally after?

### If something goes wrong

- The usual Apple Music icon always starts Apple Music with no mods. You can use it at any time.
- If Apple Music crashes or freezes at start with Ampify, close it from Task Manager (Ctrl + Shift + Esc). After two failed
  starts in a row, Ampify turns itself off: at the next start, Apple Music opens with no mods. That is safe mode. Once
  Apple Music has run for 20 seconds, Ampify tries again with mods at the following start.
- To remove everything, do step 12, even in the middle of the test. If the uninstall says Apple Music is open, close it,
  wait 10 seconds and try again.
- If an antivirus blocks a file, note the name of the threat and allow nothing before you know what it is. In every case,
  send the folder `%USERPROFILE%\Ampify\logs\`: it says what Ampify did.

## The test kit and uninstall

`Ampify-Test.exe` installs Ampify, starts Apple Music through the launcher, records an ETW trace while you use the app, and
writes a report. It has an automatic mode (no dialog, waits a set number of seconds instead of the OK button) and a switch
that skips the ETW trace, so no administrator prompt appears. The exact switches are in the usage comment at the top of
`testkit/testkit.cpp`.

After the run, a window says "Test finished: PASSED", "NEEDS REVIEW" or "NOT RUN". The folder
`%USERPROFILE%\Ampify\reports\<date>\` holds `report.txt` and a zip of the folder. In `report.txt`, each line starts with
`[OK]`, `[FAIL]`, `[?]` or `[i]`:

- `[OK]`: good.
- `[FAIL]`: did not work. Note the line.
- `[?]`: to look at. For example "0 threads resumed": nothing was suspended at launch.
- `[i]`: information.

If the "Apple Music found" line says "not found", Apple Music is not installed for the account, and the rest of the test
does not count.

Exit codes: 0 passed, 2 needs review, 4 Apple Music did not run, 1 no report.

After the uninstall, check that the Start menu shortcut is gone and that `%LOCALAPPDATA%\Programs\Ampify` no longer exists.
Apple Music from its usual icon must start and close as before, with no error window.

## Launcher and kernel

What to learn: the launcher opens Apple Music and the app stays usable. The kernel loads into `AppleMusic.exe` and
`AMPLibraryAgent.exe`. The launcher pauses the first Apple Music process while the kernel loads. The launcher stays open
while Apple Music runs, even if the app hands over to another process at start. Without the launcher, Apple Music starts as
usual with no kernel.

Put `ampify_launcher.exe` and `ampify_kernel.dll` in one folder. The Apple Music version must be in the tested list.

1. Close Apple Music completely. In Task Manager, no `AppleMusic.exe` or `AMPLibraryAgent.exe` may remain. Delete the old
   logs in `%USERPROFILE%\Ampify\logs\`.
2. Double-click `ampify_launcher.exe`. Apple Music must open within 15 seconds, with no error window. Use the app for 2
   minutes: search an album, play a stereo song, then a Dolby Atmos song, pause, next, open Settings. Close Apple Music.
   The launcher must close within 10 seconds.
3. Read the logs in `%USERPROFILE%\Ampify\logs\`: `launcher.log`, `AppleMusic.log` and `AMPLibraryAgent.log`.

`launcher.log` should say, in this order (times and process numbers vary, and the wording can differ between versions):

- the package found, with its version;
- that the signature is recognized (5 exports found in `AVFoundationCF.dll`). If it says the signature is not recognized,
  the launcher refuses the kernel;
- that the version is tested and the kernel is injected;
- that Apple Music started (process N);
- the number of threads paused, at least 1. If it is 0, the pause did not take;
- that the kernel loaded during the pause. If it says the load did not answer, the load may finish later: look at
  `AppleMusic.log`;
- the number of threads resumed, the same as paused;
- that the kernel loaded in `AMPLibraryAgent.exe`;
- at the end, that Apple Music closed and the launcher stops.

A second "kernel loaded" line for another `AppleMusic.exe` process number, without a pause, is normal: Apple Music can hand
over to another process at start.

`AppleMusic.log` shows the kernel loaded, the version tested, the extension host ready, and about 20 seconds after start
"Healthy after 20 s". `AMPLibraryAgent.log` has the same first lines.

4. Start Apple Music from the Start menu, as usual, with no launcher. Use it for 30 seconds, close it. `AppleMusic.log` must
   be unchanged: a normal launch creates no log.
5. **Safe mode.** It turns the kernel off after two launches that did not stay healthy for 20 seconds. To test it without
   crashing the app: close Apple Music, open `%USERPROFILE%\Ampify\state.txt` and set `unhealthy_launches = 2`, then
   start `ampify_launcher.exe`. `launcher.log` must say that safe mode is active and Apple Music starts without the kernel,
   with no "kernel loaded" line. Wait 25 seconds without closing the app: `launcher.log` must say the app is healthy after
   20 s, and `state.txt` must hold `unhealthy_launches = 0`.

Problems:

- Apple Music freezes at start: close it from Task Manager and keep the three logs. This is the most important case.
- The launcher closes by itself while the app runs: keep `launcher.log`, it is an Ampify bug.
- An error window from the launcher: note its exact text.
- Nothing happens: check that `ampify_kernel.dll` sits next to `ampify_launcher.exe`. `launcher.log` says so.

## Atmos leveler

What to learn:

1. Apple's Atmos stream (`ISpatialAudioObjectRenderStreamForMetadata`) is found, and the log shows its hooks installed on
   the fly at the first Atmos song.
2. Both measurements run, each at its own sample rate: stereo on the `IAudioRenderClient` stream (96 kHz on the reference
   output), Atmos on the spatial objects (48 kHz).
3. On three Dolby Atmos songs, the measured loudness is within 1 LU of the stereo target, and the gain follows.
4. The volume bar never moves on its own, during playback or at a song change.
5. The sound stays clean: no crackle, no dropout, no silence.
6. Measured levels are kept from one launch to the next.
7. The log shows no hook in CoreFP, CoreADI64 or FairPlay.

Steps:

1. With Apple Music closed, switch the leveler on: `ampify extension enable atmos-leveler` (or add
   `extensions.atmos-leveler.enabled = true` to `%USERPROFILE%\Ampify\settings.txt`).
2. Start `ampify_launcher.exe`. Note where the volume bar is. It must stay there for the whole test.
3. Play two stereo songs for at least a minute each (the target builds on them). Then play the first Atmos song in full, or
   at least two minutes, without cutting it. Do the same with the second and third Atmos songs. Close Apple Music.
4. Start the launcher again, replay the first Atmos song for ten seconds, and close Apple Music.
5. Read `%USERPROFILE%\Ampify\logs\AMPLibraryAgent.log`. Each line starts with the time, then the Atmos prefix. Expect, in
   order: "no saved level" at the first launch, or the saved levels at the next ones; about ten hook lines saying a hook was
   installed, then the leveler started with N audio hooks; the stereo measurement rate; for each stereo song, a line with
   the measured LUFS and "saved"; at the first Atmos song, the spatial stream activated, the Atmos measurement rate, and six
   hook lines for `ISpatialAudioObjectRenderStreamForMetadata::...` plus `GetBuffer`, all installed; for each Atmos song, a
   start line with the starting gain (5.0 dB), "measuring" lines with the LUFS and the stereo target, and a line with the
   Atmos LUFS measured and saved; `gain A -> B dB` lines when the gain changes. At the second launch, the saved levels are
   read, and the replayed song jumps straight to its gain with no slow ramp.
6. For each Atmos song, compare the measured Atmos LUFS with the stereo target. The gap must be 1 LU or less. The gain
   that follows (the last gain line of the song) must be about target minus measured, between 0 and 15 dB.
7. Search for `CoreFP`, `CoreADI` and `FairPlay`. A line saying a hook was installed there must not exist. A line saying
   a request was refused with one of these names is normal: it is the guard working.

If no Atmos song line appears, the song was not recognized as Atmos. Note the song name and keep the log. If a song says it
was measured on the stereo stream while it is Atmos, the objects measured nothing.

If the sound is choppy, cut, or too loud: close Apple Music, set `extensions.atmos-leveler.enabled = false`, restart, and
keep the log.

Send back: `AMPLibraryAgent.log` and `AppleMusic.log`, the measured and target lines for each Atmos song, whether the
volume bar moved, whether the sound was clean, and `%USERPROFILE%\Ampify\atmos-levels.txt`.

### Listening test script

For a run with a script and a report:

```powershell
pwsh -File scripts\build-msvc.ps1 -Bench
pwsh -File scripts\build-msvc.ps1
pwsh -File scripts\listen-test.ps1
```

Close Apple Music first and sign in. The script builds the bench kit folder, then plays "Take Five" (stereo) for 45 s,
"bad guy" (Dolby Atmos) for 45 s, and "Take Five" again, with the kernel and the leveler. It sets Apple Music's Dolby Atmos
setting to "Always on" and accepts Apple Music's warning; the setting stays that way afterwards (Settings > Playback to
change it back). It reads the volume slider before and after (it must not move) and checks the logs. Recent versions keep
the slider in a flyout, so the report may skip that check: watch the volume yourself. Sound plays: listen for a similar
perceived loudness between the two. The report prints at the end and is saved in `results\report.md`.
The `-Storefront` parameter sets the store country code of the account (`fr` by default).

## JavaScript extension

Goal: Apple Music loads a JavaScript extension from the user folder and it runs.

1. Open `%USERPROFILE%\Ampify\` (created by the launcher on first use).
2. Copy `examples\hello-extension` to `%USERPROFILE%\Ampify\extensions\`. The folder holds `manifest.json` and `main.js`.
3. Switch it on: `ampify extension enable hello-extension` (or `extensions.hello-extension.enabled = true` in
   `settings.txt`).
4. Start Apple Music with the launcher.
5. In `%USERPROFILE%\Ampify\logs\AppleMusic.log`, look for the number of JavaScript elements kept (1), the line that the
   extension started, and its own greeting with launch number 1. `AMPLibraryAgent.log` must hold no `hello-extension` line:
   the example declares `"processes": ["ui"]`.
6. Open `%USERPROFILE%\Ampify\storage\hello-extension.json`. It must hold `{"launches":1}` (or a bigger number if Apple
   Music already ran with the launcher).
7. Close and restart Apple Music: the counter must go up by exactly 1 per launch.

## Interface: panels, Marketplace and the Ampify card

The Windows bench does part of this (the XAML interface case, MSVC build). What to look at on a PC:

1. Copy `examples\react-panel` to `%USERPROFILE%\Ampify\extensions\` and run `ampify extension enable react-panel`.
2. Start Apple Music with `ampify launch`. The "React panel" panel appears on the right. Click "Add one": the counter goes
   to 1.
3. Open Settings > Playback. At the bottom, the "Ampify" card lists your extensions with a switch each, and a button to
   open the Marketplace. Switch `react-panel` off: the panel disappears at once. If AMExclusive is installed, its cards stay
   in place and Ampify's comes after.
4. Take a screenshot of each step.

`logs\AppleMusic.log` shows, in order: the main window obtained through `AMP.Utils` (`GetAppProperty "MainWindow"`); the
first work run by the window's DispatcherQueue (thread N); the interface service started; the window bound and the first
refresh run on its DispatcherQueue; the discovery (N nodes walked, P pages, C containers tracked); the panel host added or
opened in a Popup; the panel `react-panel/react-panel` mounted (N elements, AutomationId ...), then updated in place
(created 0, modified 1, rebuilt 0, removed 0) after a click; the Playback settings page spotted, then the card added; and
the extension switched off from the card. On failure the log says that `AMP.Utils.dll` or its export is unavailable, a
placement was not found, the Playback page has no list, or a render failed (0x...). Keep these lines.

Each element is named `AMPIFY_<extension>_<node>` (for example `AMPIFY_react-panel_panel-react-panel.0`), visible in
Accessibility Insights or Inspect. A panel's root node is `panel-<id>`, then the position of each child (`.0`, `.2.1`), or
its `key`.

## Themes

Goal: learn the real names and resources of the app, check that a test theme changes a color without a restart and
restores it, and check the example themes.

1. **Check the example themes** without Apple Music: run `ampify_theme_check.exe` on each folder of `themes\examples\`. Each
   must end as usable with no problem line.
2. **Copy a theme** into `%USERPROFILE%\Ampify\themes\` (the `midnight` folder).
3. **Read the names** (inspector): set `theme.inspect = 1` in `settings.txt`, start Apple Music with `ampify_launcher.exe`,
   open Settings, then the General page, and stay there 10 seconds. Close Apple Music and read
   `%USERPROFILE%\Ampify\logs\inspector.txt`. The setting goes back to 0 by itself.
4. **Apply the test theme:** `theme.active = midnight` and `theme.scheme = Dark`. Start Apple Music. The page background must
   turn dark and the text light. Note what you see.
5. **Go back:** choose "Restore Apple Music's look" in the Marketplace (or set `theme.active = none` and restart). The colors
   must return with no restart.
6. **Read the log:** the line that the UI thread is ready, the number of values applied, any "Refused" and "missing resource"
   lines (names that do not match the app), and the "Theme removed" line.
7. **The Glass redesign:** copy `glass`, choose it, and note what changes: a gradient background, blurred sidebar and
   playback bar (acrylic), content in a rounded card with a shadow, larger text. Move the pointer over the playback buttons
   and over the artwork: they must grow, then come back when the pointer leaves. Choose no theme with the pointer on a
   button: everything must return, and the button must no longer grow. The log has one line per tracked element on hover.

Measure also whether `StaticResource` or `ThemeResource` changes the behavior of a resource modified at run time, and test a
font and an image from outside the package.
