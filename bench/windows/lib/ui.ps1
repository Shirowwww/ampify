# UI Automation helpers for the cases that look at Apple Music's window: find an element by id or name, activate it,
# bring the window forward. Loaded by run-bench.ps1 after harness.ps1, before the cases.

Set-StrictMode -Version Latest

# SendKeys comes from here, for every case that presses a key in the window.
Add-Type -AssemblyName System.Windows.Forms

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
    $window = Get-AppleMusicWindow
    if ($window -eq [IntPtr]::Zero) { return }
    [AmpifyWindowCapture]::ShowWindow($window, 3) | Out-Null  # SW_MAXIMIZE
    [AmpifyWindowCapture]::SetForegroundWindow($window) | Out-Null
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
