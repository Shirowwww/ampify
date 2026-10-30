<#
.SYNOPSIS
Garde la session Apple Music d'un run à l'autre : archive chiffrée des dossiers du paquet d'Apple Music.

.DESCRIPTION
-Mode Save : Apple Music fermé, archive LocalState, LocalCache, Settings et RoamingState du paquet
AppleInc.AppleMusicWin_nzyj5cx40ttqa, puis chiffre l'archive (AES-GCM, clé dérivée de AMK_APPLE_PASSWORD par
PBKDF2-SHA256, sel aléatoire). Seul le fichier chiffré reste, prêt pour actions/cache.
-Mode Restore : déchiffre et remet ces dossiers avant le premier lancement d'Apple Music. Si la clé ne va pas
(mot de passe changé) ou si l'archive est abîmée, rien n'est restauré et la connexion se fait normalement.
Seuls le paquet d'Apple Music et ses propres dossiers sont concernés. Rien du contenu n'est jamais affiché : le
journal ne montre que des nombres de fichiers et des tailles par dossier. Exige PowerShell 7 (AesGcm).

.PARAMETER Mode
Save ou Restore.

.PARAMETER CacheDir
Dossier du cache (celui que actions/cache sauve et restaure). Contient session.bin.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('Save', 'Restore')][string]$Mode,
    [string]$CacheDir = (Join-Path ([IO.Path]::GetTempPath()) 'amk-session')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem

$package = Join-Path $env:LOCALAPPDATA 'Packages\AppleInc.AppleMusicWin_nzyj5cx40ttqa'
$folders = 'LocalState', 'LocalCache', 'Settings', 'RoamingState'
$maxFileBytes = 64MB
$magic = [Text.Encoding]::ASCII.GetBytes('AMKSESS1')
$file = Join-Path $CacheDir 'session.bin'

function Get-Key {
    param([byte[]]$Salt)
    $secret = $env:AMK_APPLE_PASSWORD
    if (-not $secret) { throw 'AMK_APPLE_PASSWORD absent : pas de clé pour le cache de session' }
    return [Security.Cryptography.Rfc2898DeriveBytes]::Pbkdf2(
        [Text.Encoding]::UTF8.GetBytes("amk-session-v1:$secret"), $Salt, 200000,
        [Security.Cryptography.HashAlgorithmName]::SHA256, 32)
}

# Number of files and size per package folder, nothing else.
function Write-FolderSizes {
    param([string]$Title)
    Write-Host "  $Title"
    foreach ($folder in $folders) {
        $path = Join-Path $package $folder
        if (-not (Test-Path -LiteralPath $path)) { Write-Host "    $folder : absent"; continue }
        $items = @(Get-ChildItem -LiteralPath $path -Recurse -File -Force -ErrorAction SilentlyContinue)
        # Summed by hand: Measure-Object returns no Sum for an empty folder, which strict mode rejects.
        [long]$bytes = 0
        foreach ($item in $items) { $bytes += $item.Length }
        Write-Host ("    {0} : {1} fichier(s), {2:N1} Mo" -f $folder, $items.Count, ($bytes / 1MB))
    }
}

if (-not (Test-Path -LiteralPath $package)) {
    Write-Host "  cache de session : paquet d'Apple Music absent, rien à faire"
    exit 0
}
if (@(Get-Process -Name 'AppleMusic', 'AMPLibraryAgent' -ErrorAction SilentlyContinue).Count -gt 0) {
    throw "cache de session : Apple Music doit être fermé avant $Mode"
}

if ($Mode -eq 'Save') {
    Write-FolderSizes 'dossiers du paquet sauvegardés :'
    New-Item -ItemType Directory -Force -Path $CacheDir | Out-Null
    $stream = New-Object IO.MemoryStream
    $zip = New-Object IO.Compression.ZipArchive($stream, [IO.Compression.ZipArchiveMode]::Create, $true)
    $skipped = 0
    foreach ($folder in $folders) {
        $root = Join-Path $package $folder
        if (-not (Test-Path -LiteralPath $root)) { continue }
        foreach ($item in Get-ChildItem -LiteralPath $root -Recurse -File -Force -ErrorAction SilentlyContinue) {
            if ($item.Length -gt $maxFileBytes) { $skipped++; continue }
            $relative = $folder + '/' + $item.FullName.Substring($root.Length + 1).Replace('\', '/')
            try {
                [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $item.FullName, $relative, [IO.Compression.CompressionLevel]::Optimal) | Out-Null
            } catch {
                $skipped++  # file locked by a service of the package
            }
        }
    }
    $zip.Dispose()
    $plain = $stream.ToArray()
    $stream.Dispose()

    $salt = [Security.Cryptography.RandomNumberGenerator]::GetBytes(16)
    $nonce = [Security.Cryptography.RandomNumberGenerator]::GetBytes(12)
    $cipher = New-Object byte[] $plain.Length
    $tag = New-Object byte[] 16
    $aes = [Security.Cryptography.AesGcm]::new((Get-Key $salt), 16)
    $aes.Encrypt($nonce, $plain, $cipher, $tag, $null)
    $aes.Dispose()
    [Array]::Clear($plain, 0, $plain.Length)

    $out = [IO.File]::Create($file)
    foreach ($part in $magic, $salt, $nonce, $tag, $cipher) { $out.Write($part, 0, $part.Length) }
    $out.Dispose()
    Write-Host ("  cache de session écrit : {0:N1} Mo chiffrés, {1} fichier(s) laissé(s) de côté (trop gros ou verrouillés)" -f ((Get-Item -LiteralPath $file).Length / 1MB), $skipped)
    exit 0
}

# Restore
if (-not (Test-Path -LiteralPath $file)) {
    Write-Host '  cache de session : aucun (premier run ou cache expiré), connexion normale'
    exit 0
}
try {
    $data = [IO.File]::ReadAllBytes($file)
    $header = $magic.Length
    if ($data.Length -lt $header + 44 -or [Text.Encoding]::ASCII.GetString($data, 0, $header) -ne 'AMKSESS1') { throw 'en-tête inconnu' }
    $salt = $data[$header..($header + 15)]
    $nonce = $data[($header + 16)..($header + 27)]
    $tag = $data[($header + 28)..($header + 43)]
    $cipher = New-Object byte[] ($data.Length - $header - 44)
    [Array]::Copy($data, $header + 44, $cipher, 0, $cipher.Length)
    $plain = New-Object byte[] $cipher.Length
    $aes = [Security.Cryptography.AesGcm]::new((Get-Key ([byte[]]$salt)), 16)
    $aes.Decrypt([byte[]]$nonce, $cipher, [byte[]]$tag, $plain, $null)
    $aes.Dispose()
} catch {
    Write-Host "  cache de session illisible ($($_.Exception.GetType().Name)) : ignoré, connexion normale"
    exit 0
}

foreach ($folder in $folders) {
    Remove-Item -LiteralPath (Join-Path $package $folder) -Recurse -Force -ErrorAction SilentlyContinue
}
$stream = New-Object IO.MemoryStream(, $plain)
$zip = New-Object IO.Compression.ZipArchive($stream, [IO.Compression.ZipArchiveMode]::Read)
$count = 0
$packageFull = [IO.Path]::GetFullPath($package) + [IO.Path]::DirectorySeparatorChar
foreach ($entry in $zip.Entries) {
    $target = [IO.Path]::GetFullPath((Join-Path $package $entry.FullName))
    $top = $entry.FullName.Split('/')[0]
    if (-not $target.StartsWith($packageFull, [StringComparison]::OrdinalIgnoreCase) -or $folders -notcontains $top) { continue }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
    [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $target, $true)
    $count++
}
$zip.Dispose()
$stream.Dispose()
[Array]::Clear($plain, 0, $plain.Length)
Write-Host "  cache de session restauré : $count fichier(s)"
Write-FolderSizes 'dossiers du paquet après restauration :'
if ($env:GITHUB_OUTPUT) { 'restored=true' | Add-Content -LiteralPath $env:GITHUB_OUTPUT -Encoding utf8 }
exit 0
