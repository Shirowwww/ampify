# Installs or updates Ampify from the latest GitHub release, in one line from PowerShell:
#
#   irm https://raw.githubusercontent.com/Shirowwww/ampify/main/scripts/install.ps1 | iex
#
# Downloads ampify-<version>-windows-x64.zip and SHA256SUMS.txt from the release, checks the zip against its SHA-256,
# then runs the installer it contains (ampify_setup.exe /silent). Nothing needs administrator rights: Ampify installs
# for the current user. Works in Windows PowerShell 5.1 and PowerShell 7.
#
# In a console that shows colours, the steps appear in Ampify's colours with a live progress bar; through a pipe, in a
# file or with NO_COLOR set, the output stays plain text.
#
# To install a given version instead of the latest: $env:AMPIFY_VERSION = '1.3.2' before the line above.

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'  # the progress bar of Invoke-WebRequest slows downloads a lot in 5.1

$script:Esc = [char]27
$script:Styled = (-not [Console]::IsOutputRedirected) -and (-not $env:NO_COLOR) -and $Host.UI.SupportsVirtualTerminal
$script:Unicode = $script:Styled -and ($env:WT_SESSION -or $env:TERM_PROGRAM)  # the classic console's font lacks the marks
$script:Accent = "$Esc[38;2;250;45;72m"
$script:Ok = "$Esc[38;2;92;207;140m"
$script:Muted = "$Esc[38;2;165;157;160m"
$script:Reset = "$Esc[0m"
$script:Turn = if ($script:Unicode) { @([char]0x280B, [char]0x2819, [char]0x2839, [char]0x2838, [char]0x283C, [char]0x2834, [char]0x2826, [char]0x2827, [char]0x2807, [char]0x280F) } else { @('|', '/', '-', '\') }
$script:Frame = 0

function Write-Raw([string]$Text) { [Console]::Write($Text) }

function Write-Heading([string]$Detail) {
    if (-not $script:Styled) { Write-Host 'Ampify: themes, extensions and a Marketplace for Apple Music'; return }
    $note = if ($script:Unicode) { "$([char]0x266B) " } else { '' }
    Write-Raw "`n  $($script:Accent)$Esc[1m${note}Ampify$($script:Reset)  $($script:Muted)$Detail$($script:Reset)`n`n"
}

# One frame of a step that runs: a turning mark, its text, and an optional tail (a progress bar).
function Write-Working([string]$Text, [string]$Tail = '') {
    if (-not $script:Styled) { return }
    $mark = $script:Turn[$script:Frame % $script:Turn.Count]
    $script:Frame++
    Write-Raw "`r$Esc[2K  $($script:Accent)$mark$($script:Reset) $Text$Tail"
}

function Write-Done([string]$Text) {
    if (-not $script:Styled) { Write-Host "  $Text"; return }
    $tick = if ($script:Unicode) { [char]0x2713 } else { '+' }
    Write-Raw "`r$Esc[2K  $($script:Ok)$tick$($script:Reset) $Text`n"
}

function Write-Failed([string]$Text) {
    if (-not $script:Styled) { Write-Host "Ampify was not installed: $Text" -ForegroundColor Red; return }
    $cross = if ($script:Unicode) { [char]0x2717 } else { 'x' }
    Write-Raw "`r$Esc[2K  $($script:Accent)$cross$($script:Reset) Ampify was not installed: $Text`n$Esc[?25h"
}

function Format-Bar([long]$Done, [long]$Total) {
    if ($Total -le 0) { return "  $($script:Muted)$([math]::Round($Done / 1MB, 1)) MB$($script:Reset)" }
    $width = 24
    $filled = [int][math]::Floor($width * $Done / $Total)
    $full = if ($script:Unicode) { [string][char]0x2501 } else { '#' }
    $empty = if ($script:Unicode) { [string][char]0x2500 } else { '-' }
    $percent = [int][math]::Floor(100 * $Done / $Total)
    return "  $($script:Accent)$($full * $filled)$($script:Muted)$($empty * ($width - $filled))$($script:Reset) $percent %"
}

# Downloads `Url` to `Path`, showing the bytes as they come in a console that shows colours.
function Save-Download([string]$Url, [string]$Path, [string]$Label) {
    if (-not $script:Styled) {
        Write-Host "  Downloading $Label..."
        Invoke-WebRequest -Uri $Url -OutFile $Path -UseBasicParsing
        return
    }
    Add-Type -AssemblyName System.Net.Http
    $client = New-Object System.Net.Http.HttpClient
    $client.DefaultRequestHeaders.UserAgent.ParseAdd('ampify-install')
    try {
        $response = $client.GetAsync($Url, [System.Net.Http.HttpCompletionOption]::ResponseHeadersRead).GetAwaiter().GetResult()
        [void]$response.EnsureSuccessStatusCode()
        $total = if ($response.Content.Headers.ContentLength) { [long]$response.Content.Headers.ContentLength } else { 0 }
        $source = $response.Content.ReadAsStreamAsync().GetAwaiter().GetResult()
        $target = [IO.File]::Create($Path)
        try {
            $buffer = New-Object byte[] 65536
            $done = 0L
            $shown = [Diagnostics.Stopwatch]::StartNew()
            while (($read = $source.Read($buffer, 0, $buffer.Length)) -gt 0) {
                $target.Write($buffer, 0, $read)
                $done += $read
                if ($shown.ElapsedMilliseconds -ge 80) { Write-Working "Downloading $Label" (Format-Bar $done $total); $shown.Restart() }
            }
        } finally {
            $target.Dispose()
            $source.Dispose()
        }
    } finally {
        $client.Dispose()
    }
    Write-Done "Downloaded $Label"
}

# Runs the installer and waits for it, the mark turning meanwhile. Not Start-Process -Wait: it waits for every process
# the installer starts, and the background launcher it starts runs until sign-out.
function Invoke-Installer([string]$Setup, [string]$Folder) {
    if (-not $script:Styled) { Write-Host '  Installing...' }
    $process = Start-Process -FilePath $Setup -ArgumentList '/silent' -WorkingDirectory $Folder -PassThru
    while (-not $process.WaitForExit(80)) { Write-Working 'Installing' }
    $process.WaitForExit()
    switch ($process.ExitCode) {
        0 { }
        3 { throw 'Apple Music is open: close it (also from the notification area, next to the clock), wait a few seconds, then run the same line again.' }
        default { throw "the installer stopped with code $($process.ExitCode) (details in $env:USERPROFILE\Ampify\logs\install.log)" }
    }
}

function Install-Ampify {
    if ($env:OS -ne 'Windows_NT') { throw 'Ampify runs on Windows only (Apple Music for Windows).' }
    # Windows PowerShell 5.1 may still default to TLS 1.0, which GitHub refuses.
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

    $repository = 'Shirowwww/ampify'
    $tag = if ($env:AMPIFY_VERSION) { "tags/v$($env:AMPIFY_VERSION.TrimStart('v'))" } else { 'latest' }
    Write-Heading 'install'
    if ($script:Styled) { Write-Raw "$Esc[?25l"; Write-Working "Looking for the release ($tag)" } else { Write-Host "  Looking for the release ($tag)..." }
    $release = Invoke-RestMethod -Uri "https://api.github.com/repos/$repository/releases/$tag" -Headers @{ 'User-Agent' = 'ampify-install' }
    $zipAsset = $release.assets | Where-Object { $_.name -match '^ampify-[0-9.]+-windows-x64\.zip$' } | Select-Object -First 1
    $sumsAsset = $release.assets | Where-Object { $_.name -eq 'SHA256SUMS.txt' } | Select-Object -First 1
    if (-not $zipAsset -or -not $sumsAsset) { throw "release $($release.tag_name) has no Ampify package" }
    $version = $release.tag_name.TrimStart('v')
    if ($script:Styled) { Write-Done "Release $version found" }

    $work = Join-Path ([IO.Path]::GetTempPath()) ("ampify-install-" + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $work | Out-Null
    try {
        $zip = Join-Path $work $zipAsset.name
        Save-Download $zipAsset.browser_download_url $zip $zipAsset.name
        $sums = (Invoke-WebRequest -Uri $sumsAsset.browser_download_url -UseBasicParsing).Content
        if ($sums -is [byte[]]) { $sums = [Text.Encoding]::UTF8.GetString($sums) }

        $line = ($sums -split "`n") | Where-Object { $_ -match "^([0-9a-fA-F]{64})\s+\*?$([regex]::Escape($zipAsset.name))\s*$" } | Select-Object -First 1
        if (-not $line) { throw "SHA256SUMS.txt does not list $($zipAsset.name)" }
        $expected = ([regex]::Match($line, '^[0-9a-fA-F]{64}')).Value.ToUpperInvariant()
        $actual = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToUpperInvariant()
        if ($actual -ne $expected) { throw "the download does not match its SHA-256 (expected $expected, got $actual)" }
        Write-Done 'SHA-256 checked.'

        $files = Join-Path $work 'package'
        Expand-Archive -LiteralPath $zip -DestinationPath $files
        $setup = Join-Path $files 'ampify_setup.exe'
        if (-not (Test-Path -LiteralPath $setup)) { throw 'the package has no ampify_setup.exe' }
        Invoke-Installer $setup $files
    } finally {
        Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    }

    $installed = Join-Path $env:LOCALAPPDATA 'Programs\Ampify'
    $principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
    $admin = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    if ($script:Styled) {
        Write-Done "Ampify $version installed in $installed"
        Write-Raw "`n  $($script:Muted)Open Apple Music as usual, then click $($script:Reset)Ampify$($script:Muted) at the bottom of the left sidebar to pick a theme.$($script:Reset)`n"
        if ($admin) { Write-Raw "  $($script:Muted)This PowerShell runs as administrator: Ampify starts with your next sign-in, or now with $($script:Accent)ampify apply$($script:Muted) in a normal terminal.$($script:Reset)`n" }
        Write-Raw "  $($script:Muted)In a new terminal, $($script:Accent)ampify help$($script:Muted) lists the commands; $($script:Accent)ampify uninstall$($script:Muted) removes Ampify.$($script:Reset)`n`n$Esc[?25h"
        return
    }
    Write-Host ''
    Write-Host "Ampify $version is installed in $installed."
    Write-Host 'Open Apple Music as usual, then click Ampify at the bottom of the left sidebar to pick a theme. Ampify loads with Apple Music each time.'
    if ($admin) {
        Write-Host 'This PowerShell runs as administrator: Ampify starts with your next Windows sign-in, or now with ampify apply in a normal terminal.'
    }
    Write-Host 'In a new terminal, ampify help lists the commands. To uninstall: ampify uninstall, or Settings > Apps > Ampify for Apple Music.'
}

# The marks are written as UTF-8 (Windows PowerShell 5.1 would turn them into the console's code page), then the
# session gets its own encoding back.
$script:Encoding = [Console]::OutputEncoding
try {
    if ($script:Unicode) { [Console]::OutputEncoding = New-Object Text.UTF8Encoding $false }
    Install-Ampify
} catch {
    Write-Failed $_.Exception.Message
} finally {
    if ($script:Unicode) { [Console]::OutputEncoding = $script:Encoding }
}
