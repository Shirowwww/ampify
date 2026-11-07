# Native rendering of the interfaces (UI) in the real Apple Music, with the bench UI build (AMPIFY_XAML=ON, AMPIFY_BENCH=ON):
#   1. the kernel gets the main window through AMP.Utils (GetAppProperty) and its DispatcherQueue runs work;
#   2. the react-panel example extension, started hot through reload.txt (like ampify extension enable), mounts its
#      panel; UI Automation finds its "AMPIFY_..." elements in Apple Music's window;
#   3. a click on "Add one" through UI Automation updates the panel in place;
#   4. the palettes theme applies on top;
#   5. Settings, opened through UI Automation if possible, has an "Ampify" row that opens Ampify's settings page.
# A PNG capture of the window is taken at each step. The useful log lines are copied into the job log.
# Job: MSVC (bench-msvc), with the MSVC kit (artifact ampify-msvc-kit) in $script:BenchKitRoot\ui (ampify_launcher.exe,
# ampify_kernel.dll, themes, examples\react-panel). Placement of the panels and buttons: provisional, see
# docs/development/testing.md.

Register-Case -Id 'ui-surfaces' -Title 'UI: native rendering of a React panel, click, theme and Ampify settings page' -Requires 'apple-music', 'kit:ui' -Action {
    param($Ctx)

    $ui = Join-Path $script:BenchKitRoot 'ui'
    $launcher = Join-Path $ui 'ampify_launcher.exe'
    $example = Join-Path $ui 'examples\react-panel'
    $palettes = Join-Path $ui 'themes\palettes'

    # The Ampify folder starts clean: the extension is installed but off, to capture the interface before the mount.
    Reset-KitState
    New-Item -ItemType Directory -Force -Path (Join-Path $script:KitDir 'extensions') | Out-Null
    $extensionDir = Join-Path $script:KitDir 'extensions\react-panel'
    Remove-Item -LiteralPath $extensionDir -Recurse -Force -ErrorAction SilentlyContinue
    Copy-Item -LiteralPath $example -Destination $extensionDir -Recurse -Force
    if (Test-Path -LiteralPath $palettes) {
        New-Item -ItemType Directory -Force -Path (Join-Path $script:KitDir 'themes') | Out-Null
        Remove-Item -LiteralPath (Join-Path $script:KitDir 'themes\palettes') -Recurse -Force -ErrorAction SilentlyContinue
        Copy-Item -LiteralPath $palettes -Destination (Join-Path $script:KitDir 'themes\palettes') -Recurse -Force
    }
    $settings = Join-Path $script:KitDir 'settings.txt'
    $reload = Join-Path $script:KitDir 'reload.txt'
    Remove-Item -LiteralPath $reload -Force -ErrorAction SilentlyContinue
    Set-Settings -Values @{
        'extensions.react-panel.enabled' = 'false'
        'theme.active' = 'none'
        'theme.inspect' = '0'
        'expose_api' = '1'
    }

    $uia = Initialize-UiAutomation
    $since = Get-Date
    $launcherProcess = Start-Launcher -LauncherPath $launcher
    try {
        $launched = Wait-Journal -FileName 'launcher.log' -Text $script:LogPhrase.AppleMusicStarted -TimeoutSeconds 120
        Add-Check -Ctx $Ctx -Name 'Apple Music started by the launcher' -Passed $launched
        if (-not $launched) { return }
        Clear-FirstRun -Ctx $Ctx

        # 1. The window and the DispatcherQueue.
        $window = Wait-Journal -FileName 'AppleMusic.log' -Text 'Interface: main window obtained through AMP.Utils' -TimeoutSeconds 90
        Add-Check -Ctx $Ctx -Name 'log: main window obtained through AMP.Utils (GetAppProperty)' -Passed $window
        $ran = Wait-Until -TimeoutSeconds 60 -Condition {
            (Test-JournalContains -FileName 'AppleMusic.log' -Text "Interface: first work run by the main window's DispatcherQueue") -or
            (Test-JournalContains -FileName 'AppleMusic.log' -Text 'first refresh run by its DispatcherQueue')
        }
        Add-Check -Ctx $Ctx -Name 'log: the DispatcherQueue ran work' -Passed $ran
        $bound = Wait-Journal -FileName 'AppleMusic.log' -Text $script:LogPhrase.MainWindowBound -TimeoutSeconds 60
        Add-Check -Ctx $Ctx -Name 'log: interface service bound to "MainWindow"' -Passed $bound
        Start-Sleep -Seconds 5
        Show-UiAppWindow
        Start-Sleep -Seconds 2
        Save-Screenshot -Ctx $Ctx -Name 'ui-before-mount' | Out-Null

        # 2. Hot start of react-panel, like ampify extension enable, then the panel mounts.
        Set-Settings -Values @{ 'extensions.react-panel.enabled' = 'true' }
        [System.IO.File]::AppendAllText($reload, "start react-panel`n", [System.Text.UTF8Encoding]::new($false))
        $mounted = Wait-Journal -FileName 'AppleMusic.log' -Text 'Interface: panel "react-panel/react-panel" mounted' -TimeoutSeconds 60
        Add-Check -Ctx $Ctx -Name 'log: panel "react-panel/react-panel" mounted' -Passed $mounted
        Start-Sleep -Seconds 3
        Show-UiAppWindow
        Start-Sleep -Seconds 1
        Save-Screenshot -Ctx $Ctx -Name 'ui-panel-mounted' | Out-Null

        $root = $null
        if ($uia) { $root = Get-UiAppWindow }
        if (-not $uia -or -not $root) {
            Add-Skip -Ctx $Ctx -Name 'UI Automation: AMPIFY_ elements in the window' -Reason 'UI Automation or the main window is not available'
        } else {
            $title = Find-UiById -Root $root -Id 'AMPIFY_react-panel_panel-react-panel.0'
            $ids = @(Get-UiAmpifyIds -Root $root)
            Add-Check -Ctx $Ctx -Name 'UI Automation: AMPIFY_react-panel_panel-react-panel.0 (panel title) found' -Passed ($null -ne $title) `
                -Detail "$(if ($title) { 'name: ' + $title.Current.Name } else { 'missing' }); AMPIFY_ seen: $($ids.Count) ($(($ids | Select-Object -First 8) -join ', '))"

            # 3. A click on "Add one": the counter goes to 1, the panel is updated in place.
            $plus = Find-UiById -Root $root -Id 'AMPIFY_react-panel_panel-react-panel.2.1'
            $how = Invoke-UiElement -Element $plus
            if ($how) {
                $updated = Wait-Journal -FileName 'AppleMusic.log' -Text 'Interface: panel "react-panel/react-panel" updated in place' -TimeoutSeconds 20
                Add-Check -Ctx $Ctx -Name 'click "Add one" (UI Automation): panel updated in place' -Passed $updated -Detail "through $how"
                Start-Sleep -Seconds 1
                $counter = Find-UiById -Root $root -Id 'AMPIFY_react-panel_panel-react-panel.2.0'
                $text = if ($counter) { [string]$counter.Current.Name } else { '' }
                Add-Check -Ctx $Ctx -Name 'counter shown: "Clicks: 1"' -Passed ($text -eq 'Clicks: 1') -Detail "read: `"$text`""
                Save-Screenshot -Ctx $Ctx -Name 'ui-panel-click' | Out-Null
            } else {
                Add-Info -Ctx $Ctx -Name 'click "Add one"' -Detail 'button AMPIFY_react-panel_panel-react-panel.2.1 not found or without the Invoke pattern'
            }
        }

        # 4. The palettes theme on top of the panel.
        if (Test-Path -LiteralPath (Join-Path $script:KitDir 'themes\palettes')) {
            Set-Settings -Values @{ 'theme.active' = 'palettes'; 'theme.scheme' = 'Midnight' }
            $themed = Wait-Until -TimeoutSeconds 60 -Condition {
                $null -ne (Find-JournalMatch -FileName 'AppleMusic.log' -Pattern 'Theme "palettes": \d+ value\(s\) applied')
            }
            Add-Check -Ctx $Ctx -Name 'palettes theme applied with the panel mounted' -Passed $themed
            Start-Sleep -Seconds 2
            Show-UiAppWindow
            Save-Screenshot -Ctx $Ctx -Name 'ui-theme-applied' | Out-Null
            Set-Settings -Values @{ 'theme.active' = 'none' }
            Wait-Journal -FileName 'AppleMusic.log' -Text $script:LogPhrase.ThemeRemoved -TimeoutSeconds 30 | Out-Null
        } else {
            Add-Skip -Ctx $Ctx -Name 'palettes theme' -Reason "example theme missing from the UI build: $palettes"
        }

        # 5. Settings and Ampify's settings page. Ctrl+, opens the settings in many apps; otherwise we look for a
        # "Settings" entry. The labels are Apple Music's own, so the French ones of a French Windows are matched too.
        # The kernel copies the look of its page from Settings > Playback, so that entry is opened first, then the
        # "Ampify" row the kernel adds after Advanced.
        $settingsOpened = ''
        if ($uia -and $root) {
            Show-UiAppWindow
            Start-Sleep -Seconds 1
            $entry = Find-UiByName -Root $root -Pattern '^(Settings|Réglages|Paramètres)(…|\.\.\.)?$'
            $settingsOpened = Invoke-UiElement -Element $entry
            if (-not $settingsOpened) {
                try {
                    Add-Type -AssemblyName System.Windows.Forms
                    [System.Windows.Forms.SendKeys]::SendWait('^,')
                    $settingsOpened = 'Ctrl+,'
                } catch {
                }
            }
            Start-Sleep -Seconds 3
            $root = Get-UiAppWindow
            # Found by the AutomationId of the entry's text, the same in every language ("Lecture" also names the
            # Play buttons of a French Windows); the button is its parent.
            $playbackText = Find-UiById -Root $root -Id 'PlaybackNavigationViewItem'
            $playback = if ($playbackText) { [System.Windows.Automation.TreeWalker]::ControlViewWalker.GetParent($playbackText) } else { $null }
            $playbackHow = Invoke-UiElement -Element $playback
            Start-Sleep -Seconds 2
            $ampifyHow = ''
            if (Wait-Until -TimeoutSeconds 15 -Condition {
                    Test-JournalContains -FileName 'AppleMusic.log' -Text 'Settings: row "Ampify" added' }) {
                $root = Get-UiAppWindow
                $ampifyHow = Invoke-UiElement -Element (Find-UiById -Root $root -Id 'AMPIFY_SettingsEntry')
            }
            Add-Info -Ctx $Ctx -Name 'UI Automation navigation to Ampify settings' `
                -Detail "settings: $(if ($settingsOpened) { $settingsOpened } else { 'not found' }); Playback: $(if ($playbackHow) { $playbackHow } else { 'not found' }); Ampify row: $(if ($ampifyHow) { $ampifyHow } else { 'not found' })"
        }
        $card = Wait-Until -TimeoutSeconds 30 -Condition {
            Test-JournalContains -FileName 'AppleMusic.log' -Text 'Settings: Ampify page built'
        }
        $pageSeen = Test-JournalContains -FileName 'AppleMusic.log' -Text 'Settings: PlaybackSettingsPage seen'
        if ($card) {
            Add-Check -Ctx $Ctx -Name 'log: Ampify settings page built' -Passed $true
            Start-Sleep -Seconds 2
            $root = Get-UiAppWindow
            $button = Find-UiById -Root $root -Id 'AMPIFY_ModsOpenFolder'
            $toggle = Find-UiById -Root $root -Id 'AMPIFY_ModsToggle_react-panel'
            Add-Check -Ctx $Ctx -Name 'UI Automation: "Open the Ampify folder" button and react-panel switch' `
                -Passed (($null -ne $button) -and ($null -ne $toggle))
            # The folder button comes last in the page, below the fold of the runner's screen: brought into view for
            # the capture.
            if ($button) {
                try {
                    $button.GetCurrentPattern([System.Windows.Automation.ScrollItemPattern]::Pattern).ScrollIntoView()
                } catch {
                }
                Start-Sleep -Milliseconds 800
            }
        } else {
            # Opening the settings by automation is a bonus: without it, the card is checked on a real PC.
            Add-Info -Ctx $Ctx -Name 'Ampify settings page' -Detail "not seen (Playback page seen: $pageSeen): to check on a real PC"
        }
        Show-UiAppWindow
        Save-Screenshot -Ctx $Ctx -Name 'ui-settings-card' | Out-Null
    } finally {
        Write-UiJournalLines
        $closed = Close-AppleMusic
        $exited = $launcherProcess.WaitForExit(15000)
        Add-Check -Ctx $Ctx -Name 'launcher: stops when Apple Music closes' -Passed ($closed -and $exited)
        $crashes = @(Get-AppCrashEvents -Since $since)
        Add-Check -Ctx $Ctx -Name 'no Apple Music crash' -Passed ($crashes.Count -eq 0) `
            -Detail "$($crashes.Count) event(s): $(Format-CrashEvents -Events $crashes)"
        Copy-JournalToResults -Ctx $Ctx
        Remove-Item -LiteralPath $extensionDir -Recurse -Force -ErrorAction SilentlyContinue
        Set-Settings -Values @{ 'extensions.react-panel.enabled' = 'false'; 'theme.active' = 'none' }
    }
}
