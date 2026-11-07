# Shared functions of the Windows test bench. Loaded by run-bench.ps1: not run on its own.
# To add a case, see docs/development/testing.md.

Set-StrictMode -Version Latest

# App identity and Ampify paths on this PC, from the facts the kernel is built with.
$script:FactsDir = Join-Path $PSScriptRoot '..\..\..\facts'
$script:AppleFacts = Get-Content -LiteralPath (Join-Path $script:FactsDir 'apple.json') -Raw | ConvertFrom-Json
$script:ReleaseFacts = Get-Content -LiteralPath (Join-Path $script:FactsDir 'release.json') -Raw | ConvertFrom-Json
$script:PackageFamily = "$($script:AppleFacts.package.name)_$($script:AppleFacts.package.publisherId)"
$script:AppUserModelId = "$($script:PackageFamily)!$($script:AppleFacts.package.appId)"
$script:StoreProductId = $script:AppleFacts.package.storeProductId
$script:KitDir = Join-Path $env:USERPROFILE $script:ReleaseFacts.kitFolder
$script:JournalDir = Join-Path $script:KitDir 'logs'
$script:StateFile = Join-Path $script:KitDir 'state.txt'
$script:SettingsFile = Join-Path $script:KitDir 'settings.txt'

# The modules src/core/drm_guard.cpp refuses, as one case-insensitive pattern for -match (names and parts alike).
$script:ProtectedModulePattern = (@($script:AppleFacts.protectedModules.names) + @($script:AppleFacts.protectedModules.nameContains) |
    ForEach-Object { [regex]::Escape($_) }) -join '|'

# Names the kernel and Windows give that the cases look for. The mutex is kBackgroundMutex (src/win/background.hpp);
# PackagesDebug is where Windows keeps a package's debugger (src/win/package_debug.cpp).
$script:BackgroundMutex = 'Local\AmpifyBackground'
$script:PackagesDebugKey = 'HKCU:\Software\Classes\PackagesDebug'

# Log lines that more than one case waits for, as the launcher and the kernel write them. A phrase only one case
# reads stays in that case, next to its check. An object, so a mistyped name stops the case under strict mode.
$script:LogPhrase = [pscustomobject]@{
    AppleMusicStarted = 'Apple Music started'
    ExtensionHostReady = 'Extension host ready'
    ThemesThreadReady = 'Themes: interface thread ready (DispatcherQueue of the main window)'
    MainWindowBound = 'Interface: window "MainWindow" bound'
    ThemeRemoved = 'Theme removed: '
}

# Filled in by run-bench.ps1 before the cases.
# Names distinct from the run-bench.ps1 parameters: the harness is loaded in the same scope as the script.
$script:BenchKitRoot = $null
$script:BenchOutputRoot = $null
$script:AppleMusicInstalled = $false
$script:AppleMusicVersion = ''
$script:KnownVersions = @()
$script:Cases = New-Object System.Collections.Generic.List[object]

# What each part of the kit holds, as laid out by scripts/bench-kit.ps1. A case names the parts it needs ('kit:ui').
$script:KitParts = @{
    release = @($script:ReleaseFacts.files.launcher, $script:ReleaseFacts.files.setup)
    bench = @($script:ReleaseFacts.files.launcher)
    cli = @($script:ReleaseFacts.files.cli)
    ui = @($script:ReleaseFacts.files.launcher, $script:ReleaseFacts.files.cli, 'themes', 'examples\react-panel')
}

# The files of a kit part that are not there, as full paths.
function Get-MissingKitFiles {
    param([string]$Part)
    if (-not $script:KitParts.ContainsKey($Part)) { throw "Unknown kit part: $Part" }
    return @($script:KitParts[$Part] | ForEach-Object { Join-Path (Join-Path $script:BenchKitRoot $Part) $_ } |
        Where-Object { -not (Test-Path -LiteralPath $_) })
}

function Write-Step {
    param([string]$Text)
    Write-Host "==> $Text"
}

# Registers a case. Action receives the case context ($Ctx) and must call Add-Check.
function Register-Case {
    param(
        [string]$Id,
        [string]$Title,
        [string[]]$Requires = @(),
        [scriptblock]$Action
    )
    $script:Cases.Add([pscustomobject]@{ Id = $Id; Title = $Title; Requires = $Requires; Action = $Action })
}

function New-CaseContext {
    param($Case)
    $dir = Join-Path $script:BenchOutputRoot $Case.Id
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    return [pscustomobject]@{
        Id = $Case.Id
        Title = $Case.Title
        Dir = $dir
        Checks = New-Object System.Collections.Generic.List[object]
    }
}

# PASS, FAIL, INFO (a finding without a verdict) or SKIP (case impossible on this runner, with the reason).
function Add-Check {
    param($Ctx, [string]$Name, [bool]$Passed, [string]$Detail = '')
    $status = if ($Passed) { 'PASS' } else { 'FAIL' }
    Add-Result -Ctx $Ctx -Name $Name -Status $status -Detail $Detail
}

function Add-Info {
    param($Ctx, [string]$Name, [string]$Detail = '')
    Add-Result -Ctx $Ctx -Name $Name -Status 'INFO' -Detail $Detail
}

function Add-Skip {
    param($Ctx, [string]$Name, [string]$Reason)
    Add-Result -Ctx $Ctx -Name $Name -Status 'SKIP' -Detail $Reason
}

function Add-Result {
    param($Ctx, [string]$Name, [string]$Status, [string]$Detail)
    $Ctx.Checks.Add([pscustomobject]@{ Name = $Name; Status = $Status; Detail = $Detail })
    $suffix = if ($Detail) { " - $Detail" } else { '' }
    Write-Host "  [$Status] $Name$suffix"
}

# Waits for a condition to become true. Returns $false when the time runs out.
function Wait-Until {
    param([scriptblock]$Condition, [int]$TimeoutSeconds, [int]$PollSeconds = 2)
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        if (& $Condition) { return $true }
        Start-Sleep -Seconds $PollSeconds
    } while ((Get-Date) -lt $deadline)
    return $false
}

# Content of an Ampify log (launcher.log, AppleMusic.log, AMPLibraryAgent.log). $null when it does not exist.
function Read-Journal {
    param([string]$FileName)
    $path = Join-Path $script:JournalDir $FileName
    if (-not (Test-Path -LiteralPath $path)) { return $null }
    try {
        return Get-Content -LiteralPath $path -Raw -Encoding UTF8
    } catch {
        return $null
    }
}

function Test-JournalContains {
    param([string]$FileName, [string]$Text)
    $content = Read-Journal $FileName
    return [bool]($content -and $content.Contains($Text))
}

function Wait-Journal {
    param([string]$FileName, [string]$Text, [int]$TimeoutSeconds)
    return Wait-Until -TimeoutSeconds $TimeoutSeconds -Condition { Test-JournalContains -FileName $FileName -Text $Text }
}

# End of an Ampify log, printed in the job log: to read why a case fails without opening the artifact.
function Write-JournalTail {
    param([string]$FileName, [int]$Lines = 40)
    Write-Host "----- $FileName (end of the log)"
    $content = Read-Journal $FileName
    if (-not $content) {
        Write-Host '  (missing or empty)'
        return
    }
    $content -split "`r?`n" | Select-Object -Last $Lines | ForEach-Object { Write-Host "  $_" }
}

# First match of a regular expression in a log, to read a number (for example the threads).
function Find-JournalMatch {
    param([string]$FileName, [string]$Pattern)
    $content = Read-Journal $FileName
    if (-not $content) { return $null }
    $match = [regex]::Match($content, $Pattern)
    if ($match.Success) { return $match }
    return $null
}

function Start-Launcher {
    param([string]$LauncherPath)
    return Start-Process -FilePath $LauncherPath -WorkingDirectory (Split-Path -Parent $LauncherPath) -PassThru
}

# Starts Apple Music the way a user would from the Start menu: without the launcher, so without the kernel.
# Starts AMPLibraryAgent.exe by activating the COM class that Apple Music's package declares (out-of-process server
# AMP.Core.IAMPMusicLibrary). The activation runs in a separate pwsh, stopped after 20 s if it hangs: only the
# process start matters, the object obtained is not used. Returns a line for the report.
function Start-AgentByCom {
    $clsid = '68E7097C-F969-4006-AAC3-95115F0ED1C4'
    $script = "try { `$t = [Type]::GetTypeFromCLSID('$clsid'); `$o = [Activator]::CreateInstance(`$t); 'activated' } catch { 'error: ' + `$_.Exception.Message }"
    $out = Join-Path ([IO.Path]::GetTempPath()) 'ampify-agent-com.txt'
    Remove-Item -LiteralPath $out -ErrorAction SilentlyContinue
    $process = Start-Process -FilePath 'pwsh' -ArgumentList @('-NoProfile', '-Command', $script) -RedirectStandardOutput $out -PassThru -WindowStyle Hidden
    if (-not $process.WaitForExit(20000)) { $process | Stop-Process -Force -ErrorAction SilentlyContinue }
    $result = if (Test-Path -LiteralPath $out) { (Get-Content -LiteralPath $out -Raw).Trim() } else { '' }
    $started = Wait-Until -TimeoutSeconds 15 -Condition { @(Get-Process -Name 'AMPLibraryAgent' -ErrorAction SilentlyContinue).Count -gt 0 }
    return "class ${clsid}: $(if ($result) { $result } else { 'no answer' }); AMPLibraryAgent.exe $(if ($started) { 'started' } else { 'still missing' })"
}

function Start-AppNormally {
    Start-Process -FilePath 'explorer.exe' -ArgumentList "shell:AppsFolder\$($script:AppUserModelId)"
}

# The processes come out one by one: callers count them with @(...), which gives an array even when empty or with one item.
function Get-AppProcesses {
    Get-Process -Name 'AppleMusic' -ErrorAction SilentlyContinue
}

# Closes Apple Music like a user (window closed), then by force if it does not leave.
# Returns $true when it left on its own.
function Close-AppleMusic {
    foreach ($process in Get-AppProcesses) {
        $process.CloseMainWindow() | Out-Null
    }
    $gone = Wait-Until -TimeoutSeconds 20 -Condition { @(Get-AppProcesses).Count -eq 0 }
    if (-not $gone) {
        Get-AppProcesses | Stop-Process -Force -ErrorAction SilentlyContinue
        Wait-Until -TimeoutSeconds 10 -Condition { @(Get-AppProcesses).Count -eq 0 } | Out-Null
    }
    return $gone
}

# First launch of Apple Music on a new runner: the codec licence (MPEG LA, buttons Agree / Decline / Save As) opens
# before the main window, then, after a sign-in, the "Welcome to Apple Music" screen (Start Listening). While they
# are open, the kernel has no XAML window, and the inspector and UI Automation see the licence instead of the app.
# Accepts them like a new user would, through UI Automation only (same rules as Close-FirstRunDialogs in
# live\live-common.ps1, which needs uia.ps1). Returns when the app shows its navigation twice in a row with none of
# these screens, or after $TimeoutSeconds. Returns the number of screens closed, -1 when UI Automation is not available.
function Close-AppleMusicFirstRun {
    param([int]$TimeoutSeconds = 90)
    try {
        Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes -ErrorAction Stop
    } catch {
        Write-Host "  first launch: UI Automation not available ($($_.Exception.Message))"
        return -1
    }
    $AE = [System.Windows.Automation.AutomationElement]
    $buttonCondition = New-Object System.Windows.Automation.PropertyCondition ($AE::ControlTypeProperty), ([System.Windows.Automation.ControlType]::Button)
    # Apple Music's own button names, in English and in French (a French Windows shows the French ones).
    $agreePattern = '^(Agree|I Agree|Accept|Accepter|J.accepte)$'
    $declinePattern = '^(Decline|Refuser|Save As|Save As\.\.\.|Enregistrer sous.*)$'
    $welcomePattern = '^(Start Listening|Commencer l.écoute)$'
    $laterPattern = '^(Continue|Continuer|Get Started|Commencer|Not Now|Plus tard|Pas maintenant)$'
    $readyPattern = '^(Search|Rechercher|Home|Accueil|Listen Now|Écouter|Browse|Explorer|Parcourir|Radio|Library|Bibliothèque)$'

    # The visible button of $Buttons whose name matches $Pattern, or $null.
    $findButton = {
        param($Buttons, [string]$Pattern)
        foreach ($button in $Buttons) {
            try {
                if (-not $button.Current.IsOffscreen -and ([string]$button.Current.Name -match $Pattern)) { return $button }
            } catch {
            }
        }
        return $null
    }
    $invoke = {
        param($Element)
        $pattern = $null
        try {
            if ($Element.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$pattern)) {
                $pattern.Invoke()
                return $true
            }
        } catch {
        }
        return $false
    }

    $closed = 0
    $calm = 0
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        $ids = @(Get-Process -Name 'AppleMusic' -ErrorAction SilentlyContinue | ForEach-Object { $_.Id })
        $windows = @()
        if ($ids.Count -gt 0) {
            try {
                foreach ($child in $AE::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, [System.Windows.Automation.Condition]::TrueCondition)) {
                    try {
                        if ($ids -contains $child.Current.ProcessId) { $windows += $child }
                    } catch {
                    }
                }
            } catch {
            }
        }
        $buttons = @()
        foreach ($window in $windows) {
            try {
                $buttons += @($window.FindAll([System.Windows.Automation.TreeScope]::Descendants, $buttonCondition))
            } catch {
            }
        }

        $agree = & $findButton $buttons $agreePattern
        $decline = & $findButton $buttons $declinePattern
        $welcome = & $findButton $buttons $welcomePattern
        $dialog = $null
        $what = ''
        if ($agree -and $decline) {
            $dialog = $agree
            $what = 'MPEG LA licence accepted (Agree)'
        } elseif ($welcome) {
            $dialog = $welcome
            $what = "welcome screen passed ($([string]$welcome.Current.Name))"
        }

        $ready = $false
        if (-not $dialog -and $windows.Count -gt 0) {
            foreach ($window in $windows) {
                try {
                    foreach ($element in $window.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)) {
                        try {
                            if ([string]$element.Current.Name -match $readyPattern) {
                                $ready = $true
                                break
                            }
                        } catch {
                        }
                    }
                } catch {
                }
                if ($ready) { break }
            }
            # A generic "Continue" or "Not Now" only counts on a screen without the app's navigation.
            if (-not $ready) {
                $later = & $findButton $buttons $laterPattern
                if ($later) {
                    $dialog = $later
                    $what = "welcome screen passed ($([string]$later.Current.Name))"
                }
            }
        }

        if ($dialog) {
            $calm = 0
            if (& $invoke $dialog) {
                Write-Host "  first launch: $what"
                $closed++
            }
            Start-Sleep -Seconds 3
            continue
        }
        if ($ready) {
            $calm++
            if ($calm -ge 2) { return $closed }
        } else {
            $calm = 0
        }
        Start-Sleep -Seconds 2
    }
    Write-Host "  first launch: Apple Music navigation not seen after $TimeoutSeconds s ($closed screen(s) closed)"
    return $closed
}

# Any case that waits for Apple Music's interface calls this right after "Apple Music started": on a new runner the
# codec licence (and the welcome screen) open before the main window, which the kernel waits for.
function Clear-FirstRun {
    param($Ctx)
    $closed = Close-AppleMusicFirstRun
    Add-Info -Ctx $Ctx -Name 'first launch of Apple Music' -Detail "$closed screen(s) closed (licence, welcome)"
}

# Stops everything the bench may leave open. Called before and after each case.
function Stop-BenchProcesses {
    Get-Process -Name 'ampify_launcher', 'AppleMusic', 'AMPLibraryAgent' -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2
}

# Resets Ampify for a case: logs and the safe mode counter. Does not touch the settings.
function Reset-KitState {
    Stop-BenchProcesses
    Remove-Item -LiteralPath $script:JournalDir -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $script:StateFile -Force -ErrorAction SilentlyContinue
}

# Copies the Ampify logs into the case folder, to keep them in the results.
function Copy-JournalToResults {
    param($Ctx)
    if (-not (Test-Path -LiteralPath $script:JournalDir)) { return }
    $target = Join-Path $Ctx.Dir 'logs'
    New-Item -ItemType Directory -Force -Path $target | Out-Null
    Copy-Item -Path (Join-Path $script:JournalDir '*') -Destination $target -Force -ErrorAction SilentlyContinue
}

function Set-SafeModeCount {
    param([int]$Count)
    New-Item -ItemType Directory -Force -Path $script:KitDir | Out-Null
    Set-Content -LiteralPath $script:StateFile -Value "unhealthy_launches = $Count" -Encoding UTF8
}

function Get-SafeModeCount {
    if (-not (Test-Path -LiteralPath $script:StateFile)) { return $null }
    $match = [regex]::Match((Get-Content -LiteralPath $script:StateFile -Raw), 'unhealthy_launches\s*=\s*(\d+)')
    if ($match.Success) { return [int]$match.Groups[1].Value }
    return $null
}

# settings.txt, the file the bench cases switch Ampify's features with. Set-Settings replaces the lines of the keys it
# is given and keeps every other; a case that must leave the file as found keeps Save-Settings for Restore-Settings.
function Set-Settings {
    param([hashtable]$Values)
    New-Item -ItemType Directory -Force -Path $script:KitDir | Out-Null
    $lines = @()
    if (Test-Path -LiteralPath $script:SettingsFile) { $lines = @(Get-Content -LiteralPath $script:SettingsFile -Encoding UTF8) }
    foreach ($key in $Values.Keys) {
        $pattern = '^\s*' + [regex]::Escape($key) + '\s*='
        $lines = @($lines | Where-Object { $_ -notmatch $pattern })
        $lines += "$key = $($Values[$key])"
    }
    [System.IO.File]::WriteAllLines($script:SettingsFile, [string[]]$lines, [System.Text.UTF8Encoding]::new($false))
}

# The text of settings.txt, or $null when the file does not exist.
function Save-Settings {
    if (-not (Test-Path -LiteralPath $script:SettingsFile)) { return $null }
    return [System.IO.File]::ReadAllText($script:SettingsFile)
}

function Restore-Settings {
    param($Saved)
    if ($null -eq $Saved) {
        Remove-Item -LiteralPath $script:SettingsFile -Force -ErrorAction SilentlyContinue
    } else {
        [System.IO.File]::WriteAllText($script:SettingsFile, $Saved, [System.Text.UTF8Encoding]::new($false))
    }
}

# Where the kernel stands in Apple Music's processes: how many have the ampify_kernel.dll module.
function Get-KernelState {
    param([string]$ProcessName = 'AppleMusic')
    $processes = @(Get-Process -Name $ProcessName -ErrorAction SilentlyContinue)
    $loaded = 0
    $unreadable = 0
    foreach ($process in $processes) {
        try {
            if ($process.Modules | Where-Object { $_.ModuleName -ieq $script:ReleaseFacts.files.kernel }) { $loaded++ }
        } catch {
            $unreadable++
        }
    }
    return [pscustomobject]@{
        Processes = $processes.Count
        Loaded = $loaded
        Unreadable = $unreadable
    }
}

# Apple Music crashes recorded by Windows since $Since, as simple objects (Id, Time, Message).
# Callers wrap it in @(...): a comma here would put the array inside a second array, and the count would be wrong.
function Get-AppCrashEvents {
    param([datetime]$Since)
    $records = @(Get-WinEvent -FilterHashtable @{ LogName = 'Application'; StartTime = $Since } -ErrorAction SilentlyContinue)
    $crashes = @()
    foreach ($record in $records) {
        if ($record.Id -notin 1000, 1001, 1002) { continue }
        $message = [string]$record.Message
        if ($message -notmatch 'AppleMusic\.exe|AMPLibraryAgent\.exe') { continue }
        $crashes += [pscustomobject]@{ Id = $record.Id; Time = $record.TimeCreated; Message = $message }
    }
    return $crashes
}

# Readable summary of the crashes: event id and start of the message, to know what crashed.
function Format-CrashEvents {
    param([object[]]$Events)
    $lines = @()
    foreach ($crash in $Events) {
        if ($lines.Count -ge 2) { break }
        $text = [string]$crash.Message -replace '\s+', ' '
        $lines += "id $($crash.Id): " + $text.Substring(0, [Math]::Min(240, $text.Length))
    }
    return ($lines -join ' | ')
}

# Screenshot of the main screen in the case folder. Returns the path, or $null when the screen is not reachable.
# Win32 calls for the captures: bring Apple Music's window forward and draw it whole, even when another window
# (the runner's console) sits on top. PrintWindow with PW_RENDERFULLCONTENT (2) also draws WinUI's composition content.
if (-not ('AmpifyWindowCapture' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class AmpifyWindowCapture {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
    [DllImport("kernel32.dll")] public static extern IntPtr GetConsoleWindow();
}
'@
}

# Main window handle of Apple Music, or zero when it has none yet.
function Get-AppleMusicWindow {
    $process = Get-Process -Name 'AppleMusic' -ErrorAction SilentlyContinue |
        Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero } | Select-Object -First 1
    if ($process) { return $process.MainWindowHandle }
    return [IntPtr]::Zero
}

# Two captures per call: <Name>.png is the screen with Apple Music brought forward and maximized (consoles are
# minimized first), <Name>-window.png is Apple Music's own window drawn by PrintWindow. Returns the screen capture.
# -AsIs leaves Apple Music's windows where they are: a window laid over the main one stays in front.
function Save-Screenshot {
    param($Ctx, [string]$Name, [switch]$AsIs)
    try {
        Add-Type -AssemblyName System.Windows.Forms, System.Drawing
        $window = Get-AppleMusicWindow
        if ($AsIs) {
            Get-Process -Name 'WindowsTerminal', 'conhost', 'OpenConsole', 'pwsh', 'powershell' -ErrorAction SilentlyContinue |
                Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero } |
                ForEach-Object { [AmpifyWindowCapture]::ShowWindow($_.MainWindowHandle, 6) | Out-Null }  # SW_MINIMIZE
            Start-Sleep -Milliseconds 500
            $window = [IntPtr]::Zero  # no PrintWindow of the main window: it is behind
        } elseif ($window -ne [IntPtr]::Zero) {
            Get-Process -Name 'WindowsTerminal', 'conhost', 'OpenConsole', 'pwsh', 'powershell' -ErrorAction SilentlyContinue |
                Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero } |
                ForEach-Object { [AmpifyWindowCapture]::ShowWindow($_.MainWindowHandle, 6) | Out-Null }  # SW_MINIMIZE
            [AmpifyWindowCapture]::ShowWindow($window, 3) | Out-Null  # SW_MAXIMIZE
            [AmpifyWindowCapture]::SetForegroundWindow($window) | Out-Null
            Start-Sleep -Milliseconds 1500  # let the window repaint after maximize
        }

        $bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
        $bitmap = New-Object System.Drawing.Bitmap $bounds.Width, $bounds.Height
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        $graphics.CopyFromScreen($bounds.Location, [System.Drawing.Point]::Empty, $bounds.Size)
        $path = Join-Path $Ctx.Dir "$Name.png"
        $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
        $graphics.Dispose()
        $bitmap.Dispose()

        if ($window -ne [IntPtr]::Zero) {
            $rect = New-Object AmpifyWindowCapture+RECT
            if ([AmpifyWindowCapture]::GetWindowRect($window, [ref]$rect)) {
                $width = [Math]::Max(1, $rect.Right - $rect.Left)
                $height = [Math]::Max(1, $rect.Bottom - $rect.Top)
                $own = New-Object System.Drawing.Bitmap $width, $height
                $ownGraphics = [System.Drawing.Graphics]::FromImage($own)
                $hdc = $ownGraphics.GetHdc()
                [AmpifyWindowCapture]::PrintWindow($window, $hdc, 2) | Out-Null
                $ownGraphics.ReleaseHdc($hdc)
                $own.Save((Join-Path $Ctx.Dir "$Name-window.png"), [System.Drawing.Imaging.ImageFormat]::Png)
                $ownGraphics.Dispose()
                $own.Dispose()
            }
        } else {
            Write-Host "  screenshot: no Apple Music window, only the screen is captured"
        }
        return $path
    } catch {
        Write-Host "  screenshot impossible: $($_.Exception.Message)"
        return $null
    }
}

# Apple Music versions of the release list, from facts/apple.json, which the kernel is built with.
function Get-KnownAppVersions {
    param([string]$RepoRoot)
    $facts = Get-Content -LiteralPath (Join-Path $RepoRoot 'facts\apple.json') -Raw | ConvertFrom-Json
    return @($facts.testedBuilds | ForEach-Object { $_.version })
}

# Full name of the Apple Music package installed for the current user, or '' when it is not installed.
# Queried in Windows PowerShell 5.1: Get-AppxPackage goes through a compatibility module under PowerShell 7.
function Get-AppleMusicFullName {
    $query = "(Get-AppxPackage -Name '$($script:AppleFacts.package.name)' | Select-Object -First 1).PackageFullName"
    $name = & powershell.exe -NoProfile -NonInteractive -Command $query 2>$null
    return [string]($name | Select-Object -First 1)
}

# Version in a full package name: AppleInc.AppleMusicWin_1.1540.23042.0_x64__nzyj5cx40ttqa.
function Get-VersionFromFullName {
    param([string]$FullName)
    $parts = $FullName -split '_'
    if ($parts.Count -ne 5) { return '' }
    return $parts[1]
}

# Installs Apple Music with winget (Microsoft's official client, Microsoft Store source).
# Returns a sentence that says what happened. The full winget output goes to $LogFile.
function Install-AppleMusic {
    param([string]$LogFile)
    # winget writes progress to stderr: that must not stop the script.
    $ErrorActionPreference = 'Continue'
    if (Get-AppleMusicFullName) { return 'already installed on this runner' }

    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) { return 'winget missing from this runner' }

    $wingetArgs = @(
        'install', '--id', $script:StoreProductId, '--source', 'msstore', '--exact', '--silent',
        '--accept-package-agreements', '--accept-source-agreements', '--disable-interactivity'
    )
    $output = & winget @wingetArgs 2>&1 | Out-String
    $exitCode = $LASTEXITCODE
    Set-Content -LiteralPath $LogFile -Value $output -Encoding UTF8

    if (Get-AppleMusicFullName) { return 'installed by winget' }
    return "winget did not install Apple Music (exit code $exitCode, see winget.log)"
}
