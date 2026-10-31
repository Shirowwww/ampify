<#
.SYNOPSIS
Gives the Windows runner an audio output: VB-CABLE virtual driver (or Scream as a fallback), default output, Windows
Sonic spatial sound if Windows accepts it.

.DESCRIPTION
GitHub runners have no sound card: Apple Music cannot play anything. This script installs a virtual audio device,
the way Mozilla's cubeb CI does on windows-2025 (VB-CABLE Pack43, the publisher's certificate added to the trusted
publishers, devcon install). devcon.exe comes from the official Scream 4.0 archive (the same Microsoft-signed file as
the one cubeb uses). Both archives are checked by SHA-256 before use.
No sound is recorded: the virtual cable only consumes the stream.

.PARAMETER OutputRoot
Folder where audio.txt (the summary) is written. Nothing secret goes into it.
#>
[CmdletBinding()]
param([string]$OutputRoot = (Join-Path $PSScriptRoot '..\..\..\results-live\audio'))

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\lib\audio.ps1')

$vbcableUrl = 'https://download.vb-audio.com/Download_CABLE/VBCABLE_Driver_Pack43.zip'
$vbcableSha256 = '66fd0a4d9f4896ff41632b7e3d53892c085c4561f53e8ae8d0f0bc10eedd1cdd'
$screamUrl = 'https://github.com/duncanthrax/scream/releases/download/4.0/Scream4.0.zip'
$screamSha256 = 'fa33e25f9a46c61e4e0cd83362c51c3d2a45c6fe4091aad7507e240e40f1a520'
$devconSha256 = '97cff42f8c0fe4fbdf991273159516bf78090625a933c3983ebd6f62284e329a'

New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
$work = Join-Path ([IO.Path]::GetTempPath()) 'ampify-audio'
New-Item -ItemType Directory -Force -Path $work | Out-Null
$summary = New-Object System.Collections.Generic.List[string]
function Note([string]$Text) { Write-Host "  $Text"; $summary.Add($Text) }

function Get-Verified {
    param([string]$Url, [string]$Sha256, [string]$Name)
    $path = Join-Path $work $Name
    if (-not (Test-Path -LiteralPath $path) -or (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash -ne $Sha256) {
        Invoke-WebRequest -Uri $Url -OutFile $path -UseBasicParsing
    }
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash
    if ($hash -ne $Sha256) { throw "unexpected hash for ${Name}: $hash" }
    return $path
}

# Trusts the publisher that signed the driver catalog, so devcon installs without the "untrusted publisher" prompt.
function Approve-CatalogSigner {
    param([string]$Catalog)
    $signature = Get-AuthenticodeSignature -FilePath $Catalog
    if (-not $signature.SignerCertificate) { throw "catalog without a readable signature: $Catalog" }
    $cer = Join-Path $work ([IO.Path]::GetFileNameWithoutExtension($Catalog) + '.cer')
    [IO.File]::WriteAllBytes($cer, $signature.SignerCertificate.Export([Security.Cryptography.X509Certificates.X509ContentType]::Cert))
    & certutil.exe -f -addstore TrustedPublisher $cer | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "certutil failed (code $LASTEXITCODE)" }
    return "$($signature.SignerCertificate.Subject); signature status: $($signature.Status)"
}

function Wait-RenderEndpoint {
    param([int]$TimeoutSeconds = 45)
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        if (@(Get-RenderEndpoints).Count -gt 0) { return $true }
        Start-Sleep -Seconds 3
    } while ((Get-Date) -lt $deadline)
    return $false
}

Write-Host '==> Runner audio output'
$os = Get-CimInstance -ClassName Win32_OperatingSystem
Note "System: $($os.Caption) $($os.Version)"
$testSigning = (& bcdedit.exe /enum '{current}' 2>$null | Select-String -Pattern 'testsigning\s+Yes' -Quiet)
Note "Test mode (testsigning): $(if ($testSigning) { 'on' } else { 'off or unreadable' })"

# Windows Server leaves the audio services stopped.
foreach ($service in 'AudioEndpointBuilder', 'Audiosrv') {
    Set-Service -Name $service -StartupType Automatic
    Start-Service -Name $service
}
Note "Audio services: $((Get-Service AudioEndpointBuilder, Audiosrv | ForEach-Object { "$($_.Name)=$($_.Status)" }) -join ', ')"

$scream = Get-Verified -Url $screamUrl -Sha256 $screamSha256 -Name 'Scream4.0.zip'
$screamDir = Join-Path $work 'scream'
Expand-Archive -LiteralPath $scream -DestinationPath $screamDir -Force
$devcon = Join-Path $screamDir 'Install\helpers\devcon-x64.exe'
if ((Get-FileHash -Algorithm SHA256 -LiteralPath $devcon).Hash -ne $devconSha256) { throw 'devcon-x64.exe: unexpected hash' }

$driver = 'none'
if (@(Get-RenderEndpoints).Count -gt 0) {
    $driver = 'output already present'
} else {
    try {
        $zip = Get-Verified -Url $vbcableUrl -Sha256 $vbcableSha256 -Name 'VBCABLE_Driver_Pack43.zip'
        $dir = Join-Path $work 'vbcable'
        Expand-Archive -LiteralPath $zip -DestinationPath $dir -Force
        Note "VB-CABLE, signer: $(Approve-CatalogSigner (Join-Path $dir 'vbaudio_cable64_win7.cat'))"
        $out = & $devcon install (Join-Path $dir 'vbMmeCable64_win7.inf') VBAudioVACWDM 2>&1 | Out-String
        Note "devcon (VB-CABLE): code $LASTEXITCODE, $($out.Trim() -replace '\s+', ' ')"
        if (Wait-RenderEndpoint) { $driver = 'VB-CABLE Pack43' }
    } catch {
        Note "VB-CABLE: failed: $($_.Exception.Message)"
    }
    if ($driver -eq 'none') {
        try {
            $dir = Join-Path $screamDir 'Install\driver\x64'
            Note "Scream, signer: $(Approve-CatalogSigner (Join-Path $dir 'scream.cat'))"
            $out = & $devcon install (Join-Path $dir 'Scream.inf') '*Scream' 2>&1 | Out-String
            Note "devcon (Scream): code $LASTEXITCODE, $($out.Trim() -replace '\s+', ' ')"
            if (Wait-RenderEndpoint) { $driver = 'Scream 4.0' }
        } catch {
            Note "Scream: failed: $($_.Exception.Message)"
        }
    }
}
Note "Driver: $driver"

$endpoints = @(Get-RenderEndpoints)
Note "Active outputs: $(if ($endpoints.Count) { ($endpoints | ForEach-Object { $_.Name }) -join ', ' } else { 'none' })"
$pnp = @(Get-PnpDevice -Class AudioEndpoint -Status OK -ErrorAction SilentlyContinue)
Note "Get-PnpDevice AudioEndpoint (OK): $($pnp.Count) ($(($pnp | ForEach-Object { $_.FriendlyName }) -join ', '))"

$default = Get-DefaultRenderEndpoint
if (-not $default -and $endpoints.Count -gt 0) {
    $hr = Set-DefaultRenderEndpoint -Id $endpoints[0].Id
    Note ("Default output chosen: {0} (0x{1:X8})" -f $endpoints[0].Name, $hr)
    $default = Get-DefaultRenderEndpoint
}
Note "Default output: $(if ($default) { $default.Name } else { 'none' })"

if ($default) {
    Note "ISpatialAudioClient before: $(Get-SpatialAudioSummary)"
    foreach ($line in Invoke-SpatialSoundConfig -Enable) { Note "WinRT: $line" }
    Note "ISpatialAudioClient after: $(Get-SpatialAudioSummary)"
}

$summary | Set-Content -LiteralPath (Join-Path $OutputRoot 'audio.txt') -Encoding UTF8
if (-not $default) {
    Write-Host '::error::no default audio output after installing the virtual driver: playback cannot be tested'
    exit 1
}
if ($env:GITHUB_OUTPUT) { "audio=$driver" | Add-Content -LiteralPath $env:GITHUB_OUTPUT -Encoding utf8 }
exit 0
