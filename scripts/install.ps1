# Installs or updates Ampify from the latest GitHub release, in one line from PowerShell:
#
#   irm https://raw.githubusercontent.com/Shirowwww/ampify/main/scripts/install.ps1 | iex
#
# Downloads ampify-<version>-windows-x64.zip and SHA256SUMS.txt from the release, checks the zip against its SHA-256,
# then runs the installer it contains (ampify_setup.exe /silent). Nothing needs administrator rights: Ampify installs
# for the current user. Works in Windows PowerShell 5.1 and PowerShell 7.
#
# To install a given version instead of the latest: $env:AMPIFY_VERSION = '1.3.3' before the line above.

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'  # the progress bar of Invoke-WebRequest slows downloads a lot in 5.1

function Write-Step([string]$Text) { Write-Host "  $Text" }

function Install-Ampify {
    if ($env:OS -ne 'Windows_NT') { throw 'Ampify runs on Windows only (Apple Music for Windows).' }
    # Windows PowerShell 5.1 may still default to TLS 1.0, which GitHub refuses.
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

    $repository = 'Shirowwww/ampify'
    $tag = if ($env:AMPIFY_VERSION) { "tags/v$($env:AMPIFY_VERSION.TrimStart('v'))" } else { 'latest' }
    Write-Host 'Ampify: themes, extensions and a Marketplace for Apple Music'
    Write-Step "Looking for the release ($tag)..."
    $release = Invoke-RestMethod -Uri "https://api.github.com/repos/$repository/releases/$tag" -Headers @{ 'User-Agent' = 'ampify-install' }
    $zipAsset = $release.assets | Where-Object { $_.name -match '^ampify-[0-9.]+-windows-x64\.zip$' } | Select-Object -First 1
    $sumsAsset = $release.assets | Where-Object { $_.name -eq 'SHA256SUMS.txt' } | Select-Object -First 1
    if (-not $zipAsset -or -not $sumsAsset) { throw "release $($release.tag_name) has no Ampify package" }

    $work = Join-Path ([IO.Path]::GetTempPath()) ("ampify-install-" + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $work | Out-Null
    try {
        $zip = Join-Path $work $zipAsset.name
        Write-Step "Downloading $($zipAsset.name)..."
        Invoke-WebRequest -Uri $zipAsset.browser_download_url -OutFile $zip -UseBasicParsing
        $sums = (Invoke-WebRequest -Uri $sumsAsset.browser_download_url -UseBasicParsing).Content
        if ($sums -is [byte[]]) { $sums = [Text.Encoding]::UTF8.GetString($sums) }

        $line = ($sums -split "`n") | Where-Object { $_ -match "^([0-9a-fA-F]{64})\s+\*?$([regex]::Escape($zipAsset.name))\s*$" } | Select-Object -First 1
        if (-not $line) { throw "SHA256SUMS.txt does not list $($zipAsset.name)" }
        $expected = ([regex]::Match($line, '^[0-9a-fA-F]{64}')).Value.ToUpperInvariant()
        $actual = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToUpperInvariant()
        if ($actual -ne $expected) { throw "the download does not match its SHA-256 (expected $expected, got $actual)" }
        Write-Step 'SHA-256 checked.'

        $files = Join-Path $work 'package'
        Expand-Archive -LiteralPath $zip -DestinationPath $files
        $setup = Join-Path $files 'ampify_setup.exe'
        if (-not (Test-Path -LiteralPath $setup)) { throw 'the package has no ampify_setup.exe' }

        Write-Step 'Installing...'
        # Not Start-Process -Wait: it waits for every process the installer starts, and the background launcher it
        # starts runs until sign-out.
        $process = Start-Process -FilePath $setup -ArgumentList '/silent' -WorkingDirectory $files -PassThru
        $process.WaitForExit()
        switch ($process.ExitCode) {
            0 { }
            3 { throw 'Apple Music is open: close it, wait a few seconds, then run the same line again.' }
            default { throw "the installer stopped with code $($process.ExitCode) (details in $env:USERPROFILE\Ampify\logs\install.log)" }
        }
    } finally {
        Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    }

    $installed = Join-Path $env:LOCALAPPDATA 'Programs\Ampify'
    Write-Host ''
    Write-Host "Ampify $($release.tag_name.TrimStart('v')) is installed in $installed."
    Write-Host 'Open Apple Music as usual: Ampify loads with it each time it starts.'
    $principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
    if ($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        Write-Host 'This PowerShell runs as administrator: Ampify starts with your next Windows sign-in, or now with ampify apply in a normal terminal.'
    }
    Write-Host 'In a new terminal, ampify help lists the commands. To uninstall: Settings > Apps > Ampify for Apple Music.'
}

try {
    Install-Ampify
} catch {
    Write-Host "Ampify was not installed: $($_.Exception.Message)" -ForegroundColor Red
}
