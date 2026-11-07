# The WinUI 1.8 metadata and the C++/WinRT projection every MSVC build of Ampify needs. One copy, used by
# scripts/build-msvc.ps1 and by .github/actions/msvc-setup: a version bump is made here only.
#
#   pwsh -File scripts\msvc-winui.ps1 -Work <folder for the downloads> -Projection <folder for the headers>
#
# Apple Music 1.1540+ is aligned on Windows App SDK 1.8, so the projection comes from the WinUI 1.8 metadata. Only the
# .winmd files are extracted from the NuGet packages, no DLL is shipped. The SHA-256 of Microsoft.UI.Xaml.winmd is pinned.
# The SDK is the newest one that has cppwinrt.exe: an old one (10.0.17763) ships C++/WinRT 1.0, which does not build
# with C++20, so the projection needs C++/WinRT 2 or later.
param(
    [Parameter(Mandatory)][string]$Work,
    [Parameter(Mandatory)][string]$Projection
)
$ErrorActionPreference = 'Stop'

$expectedXaml = 'A824AB1E380E384C382CEC14AC5DAD9D4C1B91F519D512082F0E7E5D6AC109DE'
$packages = @(
    @{ Id = 'microsoft.windowsappsdk.winui'; Version = '1.8.251222000'; Entries = @('metadata/Microsoft.UI.Xaml.winmd', 'metadata/Microsoft.UI.Text.winmd') },
    @{ Id = 'microsoft.windowsappsdk.interactiveexperiences'; Version = '1.8.251217001'; Entries = @(
        'metadata/10.0.17763.0/Microsoft.UI.winmd', 'metadata/10.0.17763.0/Microsoft.Foundation.winmd', 'metadata/10.0.17763.0/Microsoft.Graphics.winmd') },
    @{ Id = 'microsoft.windowsappsdk.foundation'; Version = '1.8.251220000'; Entries = @('metadata/Microsoft.Windows.ApplicationModel.Resources.winmd') },
    @{ Id = 'microsoft.web.webview2'; Version = '1.0.3179.45'; Entries = @('lib/Microsoft.Web.WebView2.Core.winmd') }
)

function Get-WinUiMetadata {
    param([string]$Folder)
    New-Item -ItemType Directory -Force -Path $Folder | Out-Null
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    foreach ($package in $packages) {
        $archive = Join-Path $Work "$($package.Id).$($package.Version).nupkg"
        $uri = "https://api.nuget.org/v3-flatcontainer/$($package.Id)/$($package.Version)/$($package.Id).$($package.Version).nupkg"
        Invoke-WebRequest -Uri $uri -OutFile $archive -TimeoutSec 300
        Write-Host "$($package.Id) $($package.Version) $((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash)"
        $zip = [IO.Compression.ZipFile]::OpenRead($archive)
        try {
            foreach ($entryPath in $package.Entries) {
                $entry = $zip.GetEntry($entryPath)
                if (-not $entry) { throw "metadata missing from $($package.Id) $($package.Version): $entryPath" }
                [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, (Join-Path $Folder ([IO.Path]::GetFileName($entryPath))), $true)
            }
        } finally {
            $zip.Dispose()
        }
    }
    $actual = (Get-FileHash -LiteralPath (Join-Path $Folder 'Microsoft.UI.Xaml.winmd') -Algorithm SHA256).Hash
    if ($actual -ne $expectedXaml) { throw "Microsoft.UI.Xaml.winmd: unexpected hash $actual (expected $expectedXaml)" }
}

function New-CppWinRtProjection {
    param([string]$Metadata, [string]$Folder)
    $kit = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
    $sdk = Get-ChildItem -LiteralPath (Join-Path $kit 'Include') -Directory |
        Where-Object { Test-Path -LiteralPath (Join-Path $kit "bin\$($_.Name)\x64\cppwinrt.exe") } |
        Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
    if (-not $sdk) { throw 'cppwinrt.exe not found in the Windows SDK' }
    $cppwinrt = Join-Path $kit "bin\$($sdk.Name)\x64\cppwinrt.exe"
    $sdkWinrt = Join-Path $sdk.FullName 'cppwinrt\winrt'
    if (-not (Test-Path -LiteralPath $sdkWinrt)) { throw "cppwinrt\winrt folder not found in the Windows SDK $($sdk.Name)" }

    $arguments = @()
    foreach ($winmd in Get-ChildItem -LiteralPath $Metadata -Filter '*.winmd') { $arguments += @('-in', $winmd.FullName) }
    & $cppwinrt @arguments -ref sdk -out $Folder
    if ($LASTEXITCODE -ne 0) { throw "cppwinrt.exe failed (code $LASTEXITCODE)" }
    # The base headers (winrt/base.h, winrt/Windows.*.h) come from the SDK: put them next to the projection.
    $target = Join-Path $Folder 'winrt'
    New-Item -ItemType Directory -Force -Path $target | Out-Null
    Copy-Item -Path (Join-Path $sdkWinrt '*') -Destination $target -Recurse -Force
    if (-not (Test-Path -LiteralPath (Join-Path $target 'Microsoft.UI.Xaml.h'))) { throw 'winrt\Microsoft.UI.Xaml.h missing' }
}

New-Item -ItemType Directory -Force -Path $Work | Out-Null
$metadata = Join-Path $Work 'winui-metadata'
Get-WinUiMetadata -Folder $metadata
New-CppWinRtProjection -Metadata $metadata -Folder $Projection
