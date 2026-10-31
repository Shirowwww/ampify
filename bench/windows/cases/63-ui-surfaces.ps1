# Native rendering of the interfaces (UI) in the real Apple Music, with the bench UI build (AMPIFY_XAML=ON, AMPIFY_BENCH=ON):
#   1. the kernel gets the main window through AMP.Utils (GetAppProperty) and its DispatcherQueue runs work;
#   2. the react-panel example extension, started hot through reload.txt (like ampify extension enable), mounts its
#      panel; UI Automation finds its "AMPIFY_..." elements in Apple Music's window;
#   3. a click on "Add one" through UI Automation updates the panel in place;
#   4. the midnight theme applies on top;
#   5. Settings > Playback, opened through UI Automation if possible, shows the "Ampify" card.
# A PNG capture of the window is taken at each step. The useful log lines are copied into the job log.
# Job: MSVC (bench-msvc), with the MSVC kit (artifact ampify-msvc-kit) in $script:BenchKitRoot\ui (ampify_launcher.exe,
# ampify_kernel.dll, themes\examples, examples\react-panel). Placement of the panels and buttons: provisional, see
# docs/development/testing-on-a-pc.md ("Interface: panels, Marketplace and the Ampify card").

# .NET UI Automation (WindowsDesktop), loaded once. $false when it is not available.
function Initialize-UiAutomation {
    try {
        Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes -ErrorAction Stop
        return $true
    } catch {
        Write-Host "  UI Automation not available: $($_.Exception.Message)"
        return $false
    }
}

# Apple Music's main window as a UI Automation element, or $null.
function Get-UiAppWindow {
    $process = Get-Process -Name 'AppleMusic' -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero } |
        Select-Object -First 1
    if (-not $process) { return $null }
    try {
        return [System.Windows.Automation.AutomationElement]::FromHandle($process.MainWindowHandle)
    } catch {
        return $null
    }
}

# The first descendant whose AutomationId is $Id, or $null.
function Find-UiById {
    param($Root, [string]$Id)
    if (-not $Root) { return $null }
    $condition = New-Object System.Windows.Automation.PropertyCondition ([System.Windows.Automation.AutomationElement]::AutomationIdProperty), $Id
    try {
        return $Root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $condition)
    } catch {
        return $null
    }
}

# The first descendant whose name matches $Pattern (regular expression).
function Find-UiByName {
    param($Root, [string]$Pattern)
    if (-not $Root) { return $null }
    try {
        $all = $Root.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)
    } catch {
        return $null
    }
    foreach ($element in $all) {
        $name = [string]$element.Current.Name
        if ($name -match $Pattern) { return $element }
    }
    return $null
}

# The "AMPIFY_..." AutomationIds of the window (at most 40), for the report.
function Get-UiAmpifyIds {
    param($Root)
    if (-not $Root) { return @() }
    try {
        $all = $Root.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)
    } catch {
        return @()
    }
    $ids = @()
    foreach ($element in $all) {
        $id = [string]$element.Current.AutomationId
        if ($id.StartsWith('AMPIFY_')) { $ids += $id }
        if ($ids.Count -ge 40) { break }
    }
    return $ids
}

# Activates an element: Invoke, else selection, else Toggle. Returns the way used, or '' if nothing worked.
function Invoke-UiElement {
    param($Element)
    if (-not $Element) { return '' }
    $patterns = @(
        @{ Pattern = [System.Windows.Automation.InvokePattern]::Pattern; Name = 'Invoke'; Action = { param($p) $p.Invoke() } },
        @{ Pattern = [System.Windows.Automation.SelectionItemPattern]::Pattern; Name = 'Select'; Action = { param($p) $p.Select() } },
        @{ Pattern = [System.Windows.Automation.TogglePattern]::Pattern; Name = 'Toggle'; Action = { param($p) $p.Toggle() } }
    )
    foreach ($entry in $patterns) {
        $object = $null
        try {
            if ($Element.TryGetCurrentPattern($entry.Pattern, [ref]$object)) {
                & $entry.Action $object
                return $entry.Name
            }
        } catch {
        }
    }
    return ''
}

# Brings Apple Music to the front, for the captures and the keyboard shortcuts.
function Show-UiAppWindow {
    try {
        if (-not ('AmpifyBench.Foreground' -as [type])) {
            Add-Type -Namespace AmpifyBench -Name Foreground -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(System.IntPtr window);
[DllImport("user32.dll")] public static extern bool ShowWindow(System.IntPtr window, int command);
'@
        }
        $process = Get-Process -Name 'AppleMusic' -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero } |
            Select-Object -First 1
        if ($process) {
            [AmpifyBench.Foreground]::ShowWindow($process.MainWindowHandle, 3) | Out-Null  # SW_MAXIMIZE
            [AmpifyBench.Foreground]::SetForegroundWindow($process.MainWindowHandle) | Out-Null
        }
    } catch {
    }
}

# The lines of AppleMusic.exe's log about the interface, the settings, the themes or react-panel.
function Write-UiJournalLines {
    $content = Read-Journal 'AppleMusic.log'
    Write-Host '----- AppleMusic.log: Interface, Settings, Themes and react-panel lines'
    if (-not $content) {
        Write-Host '  (missing or empty)'
        return
    }
    $content -split "`r?`n" | Where-Object { $_ -match '\] (Interface|Settings|Themes|Theme|Extension host|Extension react-panel|\[react-panel\]|Start requested)' } |
        Select-Object -First 120 | ForEach-Object { Write-Host "  $_" }
}

function Set-UiSettings {
    param([string]$Path, [hashtable]$Values)
    $lines = @()
    if (Test-Path -LiteralPath $Path) { $lines = @(Get-Content -LiteralPath $Path -Encoding UTF8) }
    foreach ($key in $Values.Keys) {
        $pattern = '^\s*' + [regex]::Escape($key) + '\s*='
        $lines = @($lines | Where-Object { $_ -notmatch $pattern })
        $lines += "$key = $($Values[$key])"
    }
    [System.IO.File]::WriteAllLines($Path, [string[]]$lines, [System.Text.UTF8Encoding]::new($false))
}

Register-Case -Id 'ui-surfaces' -Title 'UI: native rendering of a React panel, click, theme and Ampify card in Settings > Playback' -Requires 'apple-music' -Action {
    param($Ctx)

    $ui = Join-Path $script:BenchKitRoot 'ui'
    $launcher = Join-Path $ui 'ampify_launcher.exe'
    if (-not (Test-Path -LiteralPath $launcher)) {
        Add-Skip -Ctx $Ctx -Name 'UI build present' -Reason "no UI build in this bench (MinGW does not compile XAML): $launcher"
        return
    }
    $example = Join-Path $ui 'examples\react-panel'
    if (-not (Test-Path -LiteralPath $example)) { $example = Join-Path $repo 'examples\react-panel' }
    $midnight = Join-Path $ui 'themes\examples\midnight'

    # The Ampify folder starts clean: the extension is installed but off, to capture the interface before the mount.
    Reset-KitState
    New-Item -ItemType Directory -Force -Path (Join-Path $script:KitDir 'extensions') | Out-Null
    $extensionDir = Join-Path $script:KitDir 'extensions\react-panel'
    Remove-Item -LiteralPath $extensionDir -Recurse -Force -ErrorAction SilentlyContinue
    Copy-Item -LiteralPath $example -Destination $extensionDir -Recurse -Force
    if (Test-Path -LiteralPath $midnight) {
        New-Item -ItemType Directory -Force -Path (Join-Path $script:KitDir 'themes') | Out-Null
        Remove-Item -LiteralPath (Join-Path $script:KitDir 'themes\midnight') -Recurse -Force -ErrorAction SilentlyContinue
        Copy-Item -LiteralPath $midnight -Destination (Join-Path $script:KitDir 'themes\midnight') -Recurse -Force
    }
    $settings = Join-Path $script:KitDir 'settings.txt'
    $reload = Join-Path $script:KitDir 'reload.txt'
    Remove-Item -LiteralPath $reload -Force -ErrorAction SilentlyContinue
    Set-UiSettings -Path $settings -Values @{
        'extensions.react-panel.enabled' = 'false'
        'theme.active' = 'none'
        'theme.inspect' = '0'
        'expose_api' = '1'
    }

    $uia = Initialize-UiAutomation
    $since = Get-Date
    $launcherProcess = Start-Launcher -LauncherPath $launcher
    try {
        $launched = Wait-Journal -FileName 'launcher.log' -Text 'Apple Music started' -TimeoutSeconds 120
        Add-Check -Ctx $Ctx -Name 'Apple Music started by the launcher' -Passed $launched
        if (-not $launched) { return }
        # New runner: the MPEG LA licence opens before the main window, which the kernel waits for.
        $firstRun = Close-AppleMusicFirstRun
        Add-Info -Ctx $Ctx -Name 'first launch of Apple Music' -Detail "$firstRun screen(s) closed (licence, welcome)"

        # 1. The window and the DispatcherQueue.
        $window = Wait-Journal -FileName 'AppleMusic.log' -Text 'Interface: main window obtained through AMP.Utils' -TimeoutSeconds 90
        Add-Check -Ctx $Ctx -Name 'log: main window obtained through AMP.Utils (GetAppProperty)' -Passed $window
        $ran = Wait-Until -TimeoutSeconds 60 -Condition {
            (Test-JournalContains -FileName 'AppleMusic.log' -Text "Interface: first work run by the main window's DispatcherQueue") -or
            (Test-JournalContains -FileName 'AppleMusic.log' -Text 'first refresh run by its DispatcherQueue')
        }
        Add-Check -Ctx $Ctx -Name 'log: the DispatcherQueue ran work' -Passed $ran
        $bound = Wait-Journal -FileName 'AppleMusic.log' -Text 'Interface: window "MainWindow" bound' -TimeoutSeconds 60
        Add-Check -Ctx $Ctx -Name 'log: interface service bound to "MainWindow"' -Passed $bound
        Start-Sleep -Seconds 5
        Show-UiAppWindow
        Start-Sleep -Seconds 2
        Save-Screenshot -Ctx $Ctx -Name 'ui-before-mount' | Out-Null

        # 2. Hot start of react-panel, like ampify extension enable, then the panel mounts.
        Set-UiSettings -Path $settings -Values @{ 'extensions.react-panel.enabled' = 'true' }
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

        # 4. The midnight theme on top of the panel.
        if (Test-Path -LiteralPath (Join-Path $script:KitDir 'themes\midnight')) {
            Set-UiSettings -Path $settings -Values @{ 'theme.active' = 'midnight'; 'theme.scheme' = 'Dark' }
            $themed = Wait-Until -TimeoutSeconds 60 -Condition {
                $null -ne (Find-JournalMatch -FileName 'AppleMusic.log' -Pattern 'Theme "midnight": \d+ value\(s\) applied')
            }
            Add-Check -Ctx $Ctx -Name 'midnight theme applied with the panel mounted' -Passed $themed
            Start-Sleep -Seconds 2
            Show-UiAppWindow
            Save-Screenshot -Ctx $Ctx -Name 'ui-theme-applied' | Out-Null
            Set-UiSettings -Path $settings -Values @{ 'theme.active' = 'none' }
            Wait-Journal -FileName 'AppleMusic.log' -Text 'Theme removed: ' -TimeoutSeconds 30 | Out-Null
        } else {
            Add-Skip -Ctx $Ctx -Name 'midnight theme' -Reason "example theme missing from the UI build: $midnight"
        }

        # 5. Settings > Playback and the Ampify card. Ctrl+, opens the settings in many apps; otherwise we look for a
        # "Settings" entry, then the "Playback" entry. The labels are Apple Music's own, so the French ones of a French
        # Windows are matched too.
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
            $playback = Find-UiByName -Root $root -Pattern '^(Playback|Lecture)$'
            $playbackHow = Invoke-UiElement -Element $playback
            Add-Info -Ctx $Ctx -Name 'UI Automation navigation to Settings > Playback' `
                -Detail "settings: $(if ($settingsOpened) { $settingsOpened } else { 'not found' }); Playback: $(if ($playbackHow) { $playbackHow } else { 'not found' })"
        }
        $card = Wait-Until -TimeoutSeconds 30 -Condition {
            (Test-JournalContains -FileName 'AppleMusic.log' -Text 'Settings: card "Ampify" added') -or
            (Test-JournalContains -FileName 'AppleMusic.log' -Text 'Settings: card "Ampify" refreshed')
        }
        $pageSeen = Test-JournalContains -FileName 'AppleMusic.log' -Text 'Settings: PlaybackSettingsPage seen'
        if ($card) {
            Add-Check -Ctx $Ctx -Name 'log: Ampify card added to Settings > Playback' -Passed $true
            Start-Sleep -Seconds 2
            $root = Get-UiAppWindow
            $button = Find-UiById -Root $root -Id 'AMPIFY_ModsOpenFolder'
            $toggle = Find-UiById -Root $root -Id 'AMPIFY_ModsToggle_react-panel'
            Add-Check -Ctx $Ctx -Name 'UI Automation: "Open the Ampify folder" button and react-panel switch' `
                -Passed (($null -ne $button) -and ($null -ne $toggle))
            # The card comes last in the page, below the fold of the runner's screen: brought into view for the capture.
            if ($button) {
                try {
                    $button.GetCurrentPattern([System.Windows.Automation.ScrollItemPattern]::Pattern).ScrollIntoView()
                } catch {
                }
                Start-Sleep -Milliseconds 800
            }
        } else {
            # Opening the settings by automation is a bonus: without it, the card is checked on a real PC.
            Add-Info -Ctx $Ctx -Name 'Ampify card' -Detail "not seen (Playback page seen: $pageSeen): to check on a real PC"
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
        Set-UiSettings -Path $settings -Values @{ 'extensions.react-panel.enabled' = 'false'; 'theme.active' = 'none' }
    }
}
