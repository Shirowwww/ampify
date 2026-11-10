<#
.SYNOPSIS
Records the offline bench's corpus: one scrubbed inspector dump per page of Apple Music, with nothing playing and with
a song playing (docs/development/offline-bench.md, "Record a corpus").

.DESCRIPTION
Apple Music must run with Ampify. The script goes from page to page in the background through UI Automation (Invoke,
Toggle, Select and Value patterns found by AutomationId; never the mouse or the keyboard), asks the kernel for an
inspector dump on each page (theme.inspect = 1 in settings.txt), waits for logs\inspector.json to be written again,
and keeps only its scrubbed copy (ampify theme scrub) as <Corpus>\<state>\<page>.json. The raw dump never leaves the
logs folder.

It never buys, signs in, changes the account or writes to the library: any element whose name or id speaks of that is
refused before it is invoked. It never brings Apple Music to the front; a step after which Apple Music took the focus
is reported. At the end it puts back what it changed: theme.active and theme.inspect, the lyrics and queue panes, the
page, and the playback state.

  pwsh -File scripts\record-corpus.ps1 -Corpus D:\corpus\2026-11-06
  pwsh -File scripts\record-corpus.ps1 -Pages home,album,lyrics -States idle -Theme ''
  pwsh -File scripts\record-corpus.ps1 -ListPages
  pwsh -File scripts\record-corpus.ps1 -ScrubOnly logs\inspector.json -Pages home -States idle -Corpus D:\corpus

.PARAMETER Corpus
The folder the dumps go into, one subfolder per state. Created when missing.
.PARAMETER Ampify
ampify.exe, for the scrub step. Default: the one on the PATH, else build-msvc\release\ampify.exe of this repository.
.PARAMETER Kit
The Ampify folder (settings.txt, logs\). Default: %USERPROFILE%\Ampify.
.PARAMETER Theme
The theme on screen while recording, as theme.active takes it; empty (the default) records Apple Music's own look,
which replays any theme on Apple's values.
.PARAMETER Pages
The pages to record, in this order (see -ListPages). Default: all.
.PARAMETER States
idle (nothing playing) and playing (a song playing). Default: both.
.PARAMETER Search
The words typed into the search box for the search page.
.PARAMETER TimeoutSeconds
How long to wait for a page, a pane or a dump.
.PARAMETER ListPages
Prints the pages and the states, and stops: nothing is read or changed.
.PARAMETER ScrubOnly
An inspector.json already recorded: only the scrub step, into <Corpus>\<first state>\<first page>.json. Apple Music
and settings.txt are not touched.
#>
[CmdletBinding()]
param(
    [string]$Corpus = (Join-Path (Get-Location) 'corpus'),
    [string]$Ampify = '',
    [string]$Kit = (Join-Path ([Environment]::GetFolderPath('UserProfile')) 'Ampify'),
    [string]$Theme = '',
    [string[]]$Pages = @(),
    [string[]]$States = @('idle', 'playing'),
    [string]$Search = 'piano',
    [int]$TimeoutSeconds = 30,
    [switch]$ListPages,
    [string]$ScrubOnly = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Each page: what it shows, and its step name below.
$PageList = [ordered]@{
    'home'           = 'Home (Sidebar_Home)'
    'new'            = 'New (Sidebar_New)'
    'radio'          = 'Radio (Sidebar_Radio)'
    'albums'         = 'Library > Albums (Sidebar_Library_Albums)'
    'album'          = "an album's page: the first album of the library"
    'artist'         = "an artist's page: the artist of that album"
    'playlists'      = 'Library > Playlists (Sidebar_AllPlaylists)'
    'songs'          = 'Library > Songs (Sidebar_Library_Songs)'
    'search'         = 'search results for -Search'
    'settings'       = 'Settings > Playback, with the Ampify card (MoreButton, then Settings)'
    'lyrics'         = 'the lyrics pane open (LyricsToggleButton)'
    'queue'          = 'the queue pane open (PlayQueueToggleButton)'
    'marketplace'    = "Ampify's Marketplace (AMPIFY_ModsOpenMarketplace, on the Ampify card)"
    'dialog'         = 'a dialog: About Apple Music (MoreButton, then About), closed again'
}
$StateList = [ordered]@{ 'idle' = 'nothing playing'; 'playing' = 'a song playing' }

if ($Pages.Count -eq 0) { $Pages = @($PageList.Keys) }
$Pages = @($Pages | ForEach-Object { $_ -split ',' } | Where-Object { $_ } | ForEach-Object { $_.Trim().ToLowerInvariant() })
$States = @($States | ForEach-Object { $_ -split ',' } | Where-Object { $_ } | ForEach-Object { $_.Trim().ToLowerInvariant() })
foreach ($page in $Pages) {
    if (-not $PageList.Contains($page)) { throw "unknown page '$page': $($PageList.Keys -join ', ')" }
}
foreach ($state in $States) {
    if (-not $StateList.Contains($state)) { throw "unknown state '$state': $($StateList.Keys -join ', ')" }
}
if ($Pages.Count -eq 0 -or $States.Count -eq 0) { throw 'no page or no state to record' }

if ($ListPages) {
    Write-Output 'States:'
    foreach ($state in $States) { Write-Output ("  {0,-12} {1}" -f $state, $StateList[$state]) }
    Write-Output 'Pages, in order:'
    foreach ($page in $Pages) { Write-Output ("  {0,-12} {1}" -f $page, $PageList[$page]) }
    return
}

function Resolve-Ampify {
    if ($Ampify) {
        if (-not (Test-Path -LiteralPath $Ampify -PathType Leaf)) { throw "ampify.exe not found: $Ampify" }
        return (Resolve-Path -LiteralPath $Ampify).Path
    }
    $command = Get-Command 'ampify' -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) { return $command.Source }
    $built = Join-Path (Split-Path -Parent $PSScriptRoot) 'build-msvc\release\ampify.exe'
    if (Test-Path -LiteralPath $built) { return $built }
    throw 'ampify.exe not found: give -Ampify, or build it with scripts\build-msvc.ps1'
}

# The scrubbed copy of a dump, as <Corpus>\<state>\<page>.json. Returns its path.
function Save-Scrubbed {
    param([string]$Dump, [string]$State, [string]$Page)
    $folder = Join-Path $Corpus $State
    New-Item -ItemType Directory -Force -Path $folder | Out-Null
    $target = Join-Path $folder "$Page.json"
    $output = & $script:AmpifyExe theme scrub $Dump $target 2>&1
    if ($LASTEXITCODE -ne 0) { throw "ampify theme scrub failed on $Dump`: $output" }
    Write-Host "  $output"
    return $target
}

$script:AmpifyExe = Resolve-Ampify

if ($ScrubOnly) {
    if (-not (Test-Path -LiteralPath $ScrubOnly -PathType Leaf)) { throw "no dump at $ScrubOnly" }
    Save-Scrubbed -Dump $ScrubOnly -State $States[0] -Page $Pages[0] | Out-Null
    return
}

$SettingsFile = Join-Path $Kit 'settings.txt'
$InspectorFile = Join-Path $Kit 'logs\inspector.json'

# The value of one key, or $null. Read again before each write: the kernel rewrites the file too.
function Get-Setting {
    param([string]$Key)
    if (-not (Test-Path -LiteralPath $SettingsFile)) { return $null }
    foreach ($line in [IO.File]::ReadAllLines($SettingsFile)) {
        if ($line -match '^\s*([^#=][^=]*?)\s*=\s*(.*?)\s*$' -and $Matches[1] -eq $Key) { return $Matches[2] }
    }
    return $null
}

# Sets one key (or removes it with $null), keeping every other line as it is.
function Set-Setting {
    param([string]$Key, [AllowNull()]$Value)  # untyped: a [string] would turn $null into ''
    $lines = if (Test-Path -LiteralPath $SettingsFile) { [Collections.Generic.List[string]]([IO.File]::ReadAllLines($SettingsFile)) } else { [Collections.Generic.List[string]]::new() }
    $found = $false
    for ($i = $lines.Count - 1; $i -ge 0; $i--) {
        if ($lines[$i] -match '^\s*([^#=][^=]*?)\s*=' -and $Matches[1] -eq $Key) {
            if ($found -or $null -eq $Value) { $lines.RemoveAt($i) } else { $lines[$i] = "$Key = $Value"; $found = $true }
        }
    }
    if (-not $found -and $null -ne $Value) { $lines.Add("$Key = $Value") }
    [IO.File]::WriteAllLines($SettingsFile, $lines)
}

Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
if (-not ('CorpusWindows' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class CorpusWindows {
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
}
'@
}
$AE = [System.Windows.Automation.AutomationElement]
$Scope = [System.Windows.Automation.TreeScope]

# Words of what the recorder must never invoke: buying, the account, the library's contents, sharing. Checked on every
# AutomationId, and on the name of controls (buttons, menu entries); the name of a tile or a link is an album's or an
# artist's, which may hold any word.
$Forbidden = '(?i)\b(buy|purchase|subscri\w*|sign ?in|sign ?out|log ?in|log ?out|account|delete|remove|add to|love|' +
             'dislike|download|share|rate|acheter|abonn\w*|connexion|connecter|compte|supprimer|retirer|ajouter|' +
             'aimer|télécharger|partager|noter)\b'
$NamedControls = @('ControlType.Button', 'ControlType.MenuItem', 'ControlType.SplitButton', 'ControlType.CheckBox')

function Get-MainWindow {
    $process = Get-Process -Name 'AppleMusic' -ErrorAction SilentlyContinue |
        Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero } | Select-Object -First 1
    if (-not $process) { throw 'Apple Music is not running with a window: start it with Ampify first' }
    return @{ Handle = $process.MainWindowHandle; Element = $AE::FromHandle($process.MainWindowHandle); Id = $process.Id }
}

# Every top-level window of Apple Music: the main one, its dialogs and its popup hosts (menus open in one).
function Get-AppWindows {
    $ids = @(Get-Process -Name 'AppleMusic' -ErrorAction SilentlyContinue | ForEach-Object { $_.Id })
    $all = $AE::RootElement.FindAll($Scope::Children, [System.Windows.Automation.Condition]::TrueCondition)
    foreach ($window in $all) {
        try { if ($ids -contains $window.Current.ProcessId) { $window } } catch { continue }
    }
}

# The first element matching, in any window of Apple Music, waiting up to -Timeout seconds; $null when none appears.
function Find-Ui {
    param([string]$Id = '', [string]$NamePattern = '', [string]$ControlType = '', [string]$ClassName = '',
          [int]$Timeout = $TimeoutSeconds, $Root = $null)
    $conditions = @()
    if ($Id) { $conditions += [System.Windows.Automation.PropertyCondition]::new($AE::AutomationIdProperty, $Id) }
    if ($ClassName) { $conditions += [System.Windows.Automation.PropertyCondition]::new($AE::ClassNameProperty, $ClassName) }
    if ($ControlType) {
        $type = [System.Windows.Automation.ControlType].GetField($ControlType).GetValue($null)
        $conditions += [System.Windows.Automation.PropertyCondition]::new($AE::ControlTypeProperty, $type)
    }
    $condition = switch ($conditions.Count) {
        0 { [System.Windows.Automation.Condition]::TrueCondition }
        1 { $conditions[0] }
        default { [System.Windows.Automation.AndCondition]::new([System.Windows.Automation.Condition[]]$conditions) }
    }
    $deadline = (Get-Date).AddSeconds($Timeout)
    do {
        $roots = if ($Root) { @($Root) } else { @(Get-AppWindows) }
        foreach ($top in $roots) {
            try { $found = $top.FindAll($Scope::Subtree, $condition) } catch { continue }
            foreach ($element in $found) {
                try {
                    if ($NamePattern -and $element.Current.Name -notmatch $NamePattern) { continue }
                    if ($element.Current.IsOffscreen -and -not $Id) { continue }
                } catch { continue }
                return $element
            }
        }
        Start-Sleep -Milliseconds 400
    } while ((Get-Date) -lt $deadline)
    return $null
}

function Describe-Ui {
    param($Element)
    try { return "$($Element.Current.ControlType.ProgrammaticName -replace '^ControlType\.', '') id=$($Element.Current.AutomationId)" }
    catch { return '(gone)' }
}

# Acts on an element through its own pattern (Invoke, then Toggle, Select, Expand), never with the mouse. Refuses an
# element whose name or id names something the recorder must not do, and a disabled one.
function Invoke-Ui {
    param([Parameter(Mandatory)]$Element)
    $words = $Element.Current.AutomationId -replace '_', ' '
    if ($NamedControls -contains $Element.Current.ControlType.ProgrammaticName) { $words += ' ' + $Element.Current.Name }
    if ($words -match $Forbidden) { throw "refused to invoke $(Describe-Ui $Element): it speaks of '$($Matches[1])'" }
    if (-not $Element.Current.IsEnabled) { throw "disabled: $(Describe-Ui $Element)" }
    $before = [CorpusWindows]::GetForegroundWindow()
    $pattern = $null
    if ($Element.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$pattern)) { $pattern.Invoke() }
    elseif ($Element.TryGetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern, [ref]$pattern)) { $pattern.Toggle() }
    elseif ($Element.TryGetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern, [ref]$pattern)) { $pattern.Select() }
    elseif ($Element.TryGetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern, [ref]$pattern)) { $pattern.Expand() }
    else { throw "no pattern to act through: $(Describe-Ui $Element)" }
    Start-Sleep -Milliseconds 600
    $after = [CorpusWindows]::GetForegroundWindow()
    if ($after -ne $before -and $after -eq $script:Main.Handle) {
        Write-Warning "Apple Music took the focus after $(Describe-Ui $Element)"
    }
}

function Invoke-Id {
    param([string]$Id)
    $element = Find-Ui -Id $Id
    if (-not $element) { throw "not found: $Id" }
    Invoke-Ui $element
}

function Get-ToggleState {
    param($Element)
    $pattern = $null
    if ($Element.TryGetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern, [ref]$pattern)) {
        return $pattern.Current.ToggleState -eq [System.Windows.Automation.ToggleState]::On
    }
    return $null
}

# Opens a pane (lyrics, queue) when it is closed; $true when the recorder opened it and must close it again.
function Open-Pane {
    param([string]$Id)
    $button = Find-Ui -Id $Id
    if (-not $button) { throw "not found: $Id" }
    if (Get-ToggleState $button) { return $false }
    Invoke-Ui $button
    return $true
}

# The play bar's play and pause button: playing when its name says pause (Pause, Suspendre...).
function Test-Playing {
    $button = Find-Ui -Id 'TransportControl_PlayPauseStop' -Timeout 3
    return [bool]($button -and $button.Current.Name -match '(?i)(pause|suspend|stop)')
}

function Open-MainMenuItem {
    param([string]$NamePattern)
    Invoke-Id 'MoreButton'
    $item = Find-Ui -ControlType 'MenuItem' -NamePattern $NamePattern -Timeout 8
    if (-not $item) { throw "no menu entry matching $NamePattern" }
    Invoke-Ui $item
}

$script:Undo = [Collections.Generic.List[scriptblock]]::new()  # what the page opened, closed after its dump

function Show-Page {
    param([string]$Page)
    switch ($Page) {
        'home' { Invoke-Id 'Sidebar_Home' }
        'new' { Invoke-Id 'Sidebar_New' }
        'radio' { Invoke-Id 'Sidebar_Radio' }
        'albums' { Invoke-Id 'Sidebar_Library_Albums' }
        'playlists' { Invoke-Id 'Sidebar_AllPlaylists' }
        'songs' { Invoke-Id 'Sidebar_Library_Songs' }
        'album' {
            Invoke-Id 'Sidebar_Library_Albums'
            $tile = Find-Ui -ClassName 'GridViewItem' -Root $script:Main.Element
            if (-not $tile) { throw 'no album in the library' }
            Invoke-Ui $tile
            if (-not (Find-Ui -Id 'AlbumDetailHeaderLockupContent')) { Write-Warning "the album's header did not show" }
        }
        'artist' {
            Show-Page 'album'
            $header = Find-Ui -Id 'AlbumDetailHeaderLockupContent' -Timeout 5
            $link = if ($header) { Find-Ui -ControlType 'Hyperlink' -Root $header -Timeout 5 } else { $null }
            if (-not $link) { throw "no artist link in the album's header" }
            Invoke-Ui $link
            Start-Sleep -Seconds 2
        }
        'search' {
            $area = Find-Ui -Id 'AutoSuggestArea' -Timeout 5
            $box = Find-Ui -ControlType 'Edit' -Root $(if ($area) { $area } else { $script:Main.Element })
            $value = $null
            if (-not $box -or -not $box.TryGetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern, [ref]$value)) {
                throw 'no search box'
            }
            $value.SetValue($Search)
            $query = Find-Ui -Id 'QueryButton' -Timeout 5
            if (-not $query) { throw 'no button to run the search (QueryButton)' }
            Invoke-Ui $query
            Start-Sleep -Seconds 2
        }
        'settings' {
            Open-MainMenuItem '(?i)^(settings|preferences|réglages|paramètres|einstellungen|ajustes|configuración|impostazioni)'
            $entry = Find-Ui -Id 'PlaybackNavigationViewItem'
            if (-not $entry) { throw 'Settings did not open' }
            $walker = [System.Windows.Automation.TreeWalker]::ControlViewWalker
            $parent = $walker.GetParent($entry)  # the id is on the entry's text; the entry is its parent
            if ($parent) { Invoke-Ui $parent }
        }
        'marketplace' {
            Show-Page 'settings'
            $button = Find-Ui -Id 'AMPIFY_ModsOpenMarketplace'
            if (-not $button) { throw 'no Marketplace button on the Ampify card' }
            $scroll = $null
            if ($button.TryGetCurrentPattern([System.Windows.Automation.ScrollItemPattern]::Pattern, [ref]$scroll)) { $scroll.ScrollIntoView() }
            Invoke-Ui $button
            Start-Sleep -Seconds 2
        }
        'lyrics' {
            if (Open-Pane 'LyricsToggleButton') { $script:Undo.Add({ Invoke-Id 'LyricsToggleButton' }) }
        }
        'queue' {
            if (Open-Pane 'PlayQueueToggleButton') { $script:Undo.Add({ Invoke-Id 'PlayQueueToggleButton' }) }
        }
        'dialog' {
            Open-MainMenuItem '(?i)^(about|à propos|über|acerca|sobre|informazioni)'
            $close = Find-Ui -Id 'CloseButton' -Timeout 8
            if (-not $close) { $close = Find-Ui -Id 'PrimaryButton' -NamePattern '(?i)^(ok|close|fermer|schließen|cerrar|fechar|chiudi)$' -Timeout 3 }
            if (-not $close) { throw 'no dialog with a button to close it' }
            $script:DialogClose = $close
            $script:Undo.Add({ Invoke-Ui $script:DialogClose })
        }
    }
    Start-Sleep -Seconds 2  # the page's rows and images
}

# Asks the kernel for a dump and waits for inspector.json to be written again. Returns its path.
function Get-Dump {
    $before = if (Test-Path -LiteralPath $InspectorFile) { (Get-Item -LiteralPath $InspectorFile).LastWriteTimeUtc } else { [datetime]::MinValue }
    Set-Setting 'theme.inspect' '1'
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 500
        if ((Test-Path -LiteralPath $InspectorFile) -and (Get-Item -LiteralPath $InspectorFile).LastWriteTimeUtc -gt $before -and
            (Get-Setting 'theme.inspect') -ne '1') {
            Start-Sleep -Milliseconds 500  # the file is closed once the kernel clears the setting
            return $InspectorFile
        }
    }
    throw "no new inspector.json after $TimeoutSeconds s (is Ampify running in Apple Music?)"
}

$script:Main = Get-MainWindow
$saved = @{ 'theme.active' = (Get-Setting 'theme.active'); 'theme.inspect' = (Get-Setting 'theme.inspect') }
$wasPlaying = Test-Playing
$index = [Collections.Generic.List[object]]::new()
try {
    Set-Setting 'theme.inspect' '0'
    if ((Get-Setting 'theme.active') -ne $Theme) {
        Set-Setting 'theme.active' $Theme
        Start-Sleep -Seconds 6  # the kernel reloads within a tick
    }
    foreach ($state in $States) {
        $playing = Test-Playing
        if (($state -eq 'playing') -ne $playing) {
            $button = Find-Ui -Id 'TransportControl_PlayPauseStop' -Timeout 5
            if (-not $button -or -not $button.Current.IsEnabled) {
                Write-Warning "state '$state' skipped: nothing is loaded to play (play a song once, then run again)"
                foreach ($page in $Pages) { $index.Add([ordered]@{ state = $state; page = $page; file = $null; skipped = 'nothing loaded to play' }) }
                continue
            }
            Invoke-Ui $button
            Start-Sleep -Seconds 3
        }
        foreach ($page in $Pages) {
            Write-Host "$state / $page"
            $entry = [ordered]@{ state = $state; page = $page; file = $null; skipped = $null }
            try {
                Show-Page $page
                $dump = Get-Dump
                $scrubbed = Save-Scrubbed -Dump $dump -State $state -Page $page
                $entry.file = "$state/$page.json"  # relative: the corpus names no folder of this PC
                $theme = (Get-Content -LiteralPath $scrubbed -Raw | ConvertFrom-Json).theme
                if ("$theme" -ne $Theme) { Write-Warning "$page was recorded under theme '$theme', not '$Theme'" }
            } catch {
                $entry.skipped = "$_"
                Write-Warning "$state / $page skipped: $_"
            } finally {
                foreach ($undo in $script:Undo) { try { & $undo } catch { Write-Warning "could not close what $page opened: $_" } }
                $script:Undo.Clear()
            }
            $index.Add($entry)
        }
    }
} finally {
    try { Invoke-Id 'Sidebar_Home' } catch { Write-Warning "could not go back home: $_" }
    try {
        if ((Test-Playing) -ne $wasPlaying) { Invoke-Id 'TransportControl_PlayPauseStop' }
    } catch { Write-Warning "could not put the playback back: $_" }
    foreach ($key in $saved.Keys) { Set-Setting $key $saved[$key] }
    New-Item -ItemType Directory -Force -Path $Corpus | Out-Null
    $index | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $Corpus 'index.json') -Encoding UTF8
}
$recorded = @($index | Where-Object { $_.file }).Count
Write-Output "Recorded $recorded of $($index.Count) page(s) into $Corpus (index.json lists what was skipped and why)"
