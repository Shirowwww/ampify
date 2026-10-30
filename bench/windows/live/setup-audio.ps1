<#
.SYNOPSIS
Donne une sortie audio au runner Windows : pilote virtuel VB-CABLE (ou Scream en secours), sortie par défaut, son
spatial Windows Sonic si Windows l'accepte.

.DESCRIPTION
Les runners GitHub n'ont aucune carte son : Apple Music ne peut rien jouer. Ce script installe un périphérique audio
virtuel, comme le fait le CI de Mozilla cubeb sur windows-2025 (VB-CABLE Pack43, certificat de l'éditeur ajouté aux
éditeurs approuvés, devcon install). devcon.exe est pris dans l'archive officielle de Scream 4.0 (même fichier signé
par Microsoft que celui de cubeb). Les deux archives sont vérifiées par SHA-256 avant usage.
Aucun son n'est enregistré : le câble virtuel ne fait que consommer le flux.

.PARAMETER OutputRoot
Dossier où écrire audio.txt (le résumé). Rien de secret n'y est écrit.
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
$work = Join-Path ([IO.Path]::GetTempPath()) 'amk-audio'
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
    if ($hash -ne $Sha256) { throw "empreinte inattendue pour $Name : $hash" }
    return $path
}

# Trusts the publisher that signed the driver catalog, so devcon installs without the "untrusted publisher" prompt.
function Approve-CatalogSigner {
    param([string]$Catalog)
    $signature = Get-AuthenticodeSignature -FilePath $Catalog
    if (-not $signature.SignerCertificate) { throw "catalogue sans signature lisible : $Catalog" }
    $cer = Join-Path $work ([IO.Path]::GetFileNameWithoutExtension($Catalog) + '.cer')
    [IO.File]::WriteAllBytes($cer, $signature.SignerCertificate.Export([Security.Cryptography.X509Certificates.X509ContentType]::Cert))
    & certutil.exe -f -addstore TrustedPublisher $cer | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "certutil a échoué (code $LASTEXITCODE)" }
    return "$($signature.SignerCertificate.Subject) ; état de la signature : $($signature.Status)"
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

Write-Host '==> Sortie audio du runner'
$os = Get-CimInstance -ClassName Win32_OperatingSystem
Note "Système : $($os.Caption) $($os.Version)"
$testSigning = (& bcdedit.exe /enum '{current}' 2>$null | Select-String -Pattern 'testsigning\s+Yes' -Quiet)
Note "Mode test (testsigning) : $(if ($testSigning) { 'activé' } else { 'désactivé ou illisible' })"

# Windows Server leaves the audio services stopped.
foreach ($service in 'AudioEndpointBuilder', 'Audiosrv') {
    Set-Service -Name $service -StartupType Automatic
    Start-Service -Name $service
}
Note "Services audio : $((Get-Service AudioEndpointBuilder, Audiosrv | ForEach-Object { "$($_.Name)=$($_.Status)" }) -join ', ')"

$scream = Get-Verified -Url $screamUrl -Sha256 $screamSha256 -Name 'Scream4.0.zip'
$screamDir = Join-Path $work 'scream'
Expand-Archive -LiteralPath $scream -DestinationPath $screamDir -Force
$devcon = Join-Path $screamDir 'Install\helpers\devcon-x64.exe'
if ((Get-FileHash -Algorithm SHA256 -LiteralPath $devcon).Hash -ne $devconSha256) { throw 'devcon-x64.exe : empreinte inattendue' }

$driver = 'aucun'
if (@(Get-RenderEndpoints).Count -gt 0) {
    $driver = 'sortie déjà présente'
} else {
    try {
        $zip = Get-Verified -Url $vbcableUrl -Sha256 $vbcableSha256 -Name 'VBCABLE_Driver_Pack43.zip'
        $dir = Join-Path $work 'vbcable'
        Expand-Archive -LiteralPath $zip -DestinationPath $dir -Force
        Note "VB-CABLE, signataire : $(Approve-CatalogSigner (Join-Path $dir 'vbaudio_cable64_win7.cat'))"
        $out = & $devcon install (Join-Path $dir 'vbMmeCable64_win7.inf') VBAudioVACWDM 2>&1 | Out-String
        Note "devcon (VB-CABLE) : code $LASTEXITCODE, $($out.Trim() -replace '\s+', ' ')"
        if (Wait-RenderEndpoint) { $driver = 'VB-CABLE Pack43' }
    } catch {
        Note "VB-CABLE : échec : $($_.Exception.Message)"
    }
    if ($driver -eq 'aucun') {
        try {
            $dir = Join-Path $screamDir 'Install\driver\x64'
            Note "Scream, signataire : $(Approve-CatalogSigner (Join-Path $dir 'scream.cat'))"
            $out = & $devcon install (Join-Path $dir 'Scream.inf') '*Scream' 2>&1 | Out-String
            Note "devcon (Scream) : code $LASTEXITCODE, $($out.Trim() -replace '\s+', ' ')"
            if (Wait-RenderEndpoint) { $driver = 'Scream 4.0' }
        } catch {
            Note "Scream : échec : $($_.Exception.Message)"
        }
    }
}
Note "Pilote : $driver"

$endpoints = @(Get-RenderEndpoints)
Note "Sorties actives : $(if ($endpoints.Count) { ($endpoints | ForEach-Object { $_.Name }) -join ', ' } else { 'aucune' })"
$pnp = @(Get-PnpDevice -Class AudioEndpoint -Status OK -ErrorAction SilentlyContinue)
Note "Get-PnpDevice AudioEndpoint (OK) : $($pnp.Count) ($(($pnp | ForEach-Object { $_.FriendlyName }) -join ', '))"

$default = Get-DefaultRenderEndpoint
if (-not $default -and $endpoints.Count -gt 0) {
    $hr = Set-DefaultRenderEndpoint -Id $endpoints[0].Id
    Note ("Sortie par défaut choisie : {0} (0x{1:X8})" -f $endpoints[0].Name, $hr)
    $default = Get-DefaultRenderEndpoint
}
Note "Sortie par défaut : $(if ($default) { $default.Name } else { 'aucune' })"

if ($default) {
    Note "ISpatialAudioClient avant : $(Get-SpatialAudioSummary)"
    foreach ($line in Invoke-SpatialSoundConfig -Enable) { Note "WinRT : $line" }
    Note "ISpatialAudioClient après : $(Get-SpatialAudioSummary)"
}

$summary | Set-Content -LiteralPath (Join-Path $OutputRoot 'audio.txt') -Encoding UTF8
if (-not $default) {
    Write-Host '::error::aucune sortie audio par défaut après installation du pilote virtuel : la lecture ne peut pas être testée'
    exit 1
}
if ($env:GITHUB_OUTPUT) { "audio=$driver" | Add-Content -LiteralPath $env:GITHUB_OUTPUT -Encoding utf8 }
exit 0
