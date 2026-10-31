<#
.SYNOPSIS
Keeps the Apple Music session from one run to the next: encrypted archive of the folders of Apple Music's package.

.DESCRIPTION
-Mode Save: with Apple Music closed, archives LocalState, LocalCache, Settings and RoamingState of the package
AppleInc.AppleMusicWin_nzyj5cx40ttqa, then encrypts the archive (AES-GCM, key derived from AMPIFY_APPLE_PASSWORD by
PBKDF2-SHA256, random salt). Only the encrypted file stays, ready for actions/cache.
-Mode Restore: decrypts and puts these folders back before Apple Music's first launch. If the key does not fit
(password changed) or the archive is damaged, nothing is restored and the sign-in happens normally.
Only Apple Music's package and its own folders are involved. None of the content is ever shown: the log shows only
file counts and sizes per folder. Needs PowerShell 7 (AesGcm).

.PARAMETER Mode
Save ou Restore.

.PARAMETER CacheDir
Cache folder (the one actions/cache saves and restores). Holds session.bin.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('Save', 'Restore')][string]$Mode,
    [string]$CacheDir = (Join-Path ([IO.Path]::GetTempPath()) 'ampify-session')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem

$package = Join-Path $env:LOCALAPPDATA 'Packages\AppleInc.AppleMusicWin_nzyj5cx40ttqa'
$folders = 'LocalState', 'LocalCache', 'Settings', 'RoamingState'
$maxFileBytes = 64MB
$magic = [Text.Encoding]::ASCII.GetBytes('AMPSESS1')
$file = Join-Path $CacheDir 'session.bin'

function Get-Key {
    param([byte[]]$Salt)
    $secret = $env:AMPIFY_APPLE_PASSWORD
    if (-not $secret) { throw 'AMPIFY_APPLE_PASSWORD missing: no key for the session cache' }
    return [Security.Cryptography.Rfc2898DeriveBytes]::Pbkdf2(
        [Text.Encoding]::UTF8.GetBytes("ampify-session-v1:$secret"), $Salt, 200000,
        [Security.Cryptography.HashAlgorithmName]::SHA256, 32)
}

# Number of files and size per package folder, nothing else.
function Write-FolderSizes {
    param([string]$Title)
    Write-Host "  $Title"
    foreach ($folder in $folders) {
        $path = Join-Path $package $folder
        if (-not (Test-Path -LiteralPath $path)) { Write-Host "    ${folder}: missing"; continue }
        $items = @(Get-ChildItem -LiteralPath $path -Recurse -File -Force -ErrorAction SilentlyContinue)
        # Summed by hand: Measure-Object returns no Sum for an empty folder, which strict mode rejects.
        [long]$bytes = 0
        foreach ($item in $items) { $bytes += $item.Length }
        Write-Host ("    {0}: {1} file(s), {2:N1} MB" -f $folder, $items.Count, ($bytes / 1MB))
    }
}

if (-not (Test-Path -LiteralPath $package)) {
    Write-Host "  session cache: Apple Music package missing, nothing to do"
    exit 0
}
if (@(Get-Process -Name 'AppleMusic', 'AMPLibraryAgent' -ErrorAction SilentlyContinue).Count -gt 0) {
    throw "session cache: Apple Music must be closed before $Mode"
}

if ($Mode -eq 'Save') {
    Write-FolderSizes 'package folders saved:'
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
    Write-Host ("  session cache written: {0:N1} MB encrypted, {1} file(s) left out (too big or locked)" -f ((Get-Item -LiteralPath $file).Length / 1MB), $skipped)
    exit 0
}

# Restore
if (-not (Test-Path -LiteralPath $file)) {
    Write-Host '  session cache: none (first run or expired cache), normal sign-in'
    exit 0
}
try {
    $data = [IO.File]::ReadAllBytes($file)
    $header = $magic.Length
    if ($data.Length -lt $header + 44 -or [Text.Encoding]::ASCII.GetString($data, 0, $header) -ne 'AMPSESS1') { throw 'unknown header' }
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
    Write-Host "  session cache unreadable ($($_.Exception.GetType().Name)): ignored, normal sign-in"
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
Write-Host "  session cache restored: $count file(s)"
Write-FolderSizes 'package folders after the restore:'
if ($env:GITHUB_OUTPUT) { 'restored=true' | Add-Content -LiteralPath $env:GITHUB_OUTPUT -Encoding utf8 }
exit 0
