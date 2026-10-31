# Builds the kit with MSVC on a Windows PC, XAML included, the same way as the CI job msvc-build.
# Needs Visual Studio 2022 (C++ workload, Windows SDK) and CMake; Ninja comes with Visual Studio.
#
#   pwsh -File scripts\build-msvc.ps1            release build (tested versions only) in build-msvc\release
#   pwsh -File scripts\build-msvc.ps1 -Bench     bench build (any Apple Music version, never ship) in build-msvc\bench
#
# The WinUI 1.8 metadata (NuGet) and the C++/WinRT projection are made once, in build-msvc\winrt-gen; later runs only
# compile what changed (a few seconds to a minute).
param([switch]$Bench)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$out = Join-Path $root 'build-msvc'
New-Item -ItemType Directory -Force -Path $out | Out-Null

# 1. Visual Studio environment (cl.exe, the SDK, ninja) for this process.
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -property installationPath
if (-not $vs) { throw 'Visual Studio not found ("Desktop development with C++" workload)' }
foreach ($line in (cmd /c "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && set")) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2]) }
}

# 2. WinUI 1.8 metadata, as in the CI (same packages, Microsoft.UI.Xaml.winmd pinned), then the projection.
$gen = Join-Path $out 'winrt-gen'
if (-not (Test-Path -LiteralPath (Join-Path $gen 'winrt\Microsoft.UI.Xaml.h'))) {
    $metadata = Join-Path $out 'winui-metadata'
    New-Item -ItemType Directory -Force -Path $metadata | Out-Null
    $packages = @(
        @{ Id = 'microsoft.windowsappsdk.winui'; Version = '1.8.251222000'; Entries = @('metadata/Microsoft.UI.Xaml.winmd', 'metadata/Microsoft.UI.Text.winmd') },
        @{ Id = 'microsoft.windowsappsdk.interactiveexperiences'; Version = '1.8.251217001'; Entries = @(
            'metadata/10.0.17763.0/Microsoft.UI.winmd', 'metadata/10.0.17763.0/Microsoft.Foundation.winmd', 'metadata/10.0.17763.0/Microsoft.Graphics.winmd') },
        @{ Id = 'microsoft.windowsappsdk.foundation'; Version = '1.8.251220000'; Entries = @('metadata/Microsoft.Windows.ApplicationModel.Resources.winmd') },
        @{ Id = 'microsoft.web.webview2'; Version = '1.0.3179.45'; Entries = @('lib/Microsoft.Web.WebView2.Core.winmd') }
    )
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    foreach ($package in $packages) {
        $archive = Join-Path $out "$($package.Id).$($package.Version).nupkg"
        Invoke-WebRequest -Uri "https://api.nuget.org/v3-flatcontainer/$($package.Id)/$($package.Version)/$($package.Id).$($package.Version).nupkg" -OutFile $archive
        $zip = [IO.Compression.ZipFile]::OpenRead($archive)
        try {
            foreach ($entryPath in $package.Entries) {
                $entry = $zip.GetEntry($entryPath)
                if (-not $entry) { throw "metadata missing from $($package.Id): $entryPath" }
                [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, (Join-Path $metadata ([IO.Path]::GetFileName($entryPath))), $true)
            }
        } finally {
            $zip.Dispose()
        }
    }
    $expected = 'A824AB1E380E384C382CEC14AC5DAD9D4C1B91F519D512082F0E7E5D6AC109DE'
    $actual = (Get-FileHash -LiteralPath (Join-Path $metadata 'Microsoft.UI.Xaml.winmd') -Algorithm SHA256).Hash
    if ($actual -ne $expected) { throw "Microsoft.UI.Xaml.winmd: unexpected hash $actual" }

    # The newest SDK: an old one (10.0.17763) ships C++/WinRT 1.0, which does not build with C++20.
    $kit = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
    $sdkVersion = Get-ChildItem -LiteralPath (Join-Path $kit 'Include') -Directory |
        Where-Object { Test-Path -LiteralPath (Join-Path $kit "bin\$($_.Name)\x64\cppwinrt.exe") } |
        Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
    if (-not $sdkVersion) { throw 'cppwinrt not found in the Windows SDK' }
    $cppwinrt = Get-Item -LiteralPath (Join-Path $kit "bin\$($sdkVersion.Name)\x64\cppwinrt.exe")
    $sdkWinrt = Get-Item -LiteralPath (Join-Path $sdkVersion.FullName 'cppwinrt\winrt') -ErrorAction SilentlyContinue
    if (-not $cppwinrt -or -not $sdkWinrt) { throw 'cppwinrt not found in the Windows SDK' }
    $arguments = @()
    foreach ($winmd in Get-ChildItem -LiteralPath $metadata -Filter '*.winmd') { $arguments += @('-in', $winmd.FullName) }
    & $cppwinrt.FullName @arguments -ref sdk -out $gen
    if ($LASTEXITCODE -ne 0) { throw "cppwinrt.exe failed (code $LASTEXITCODE)" }
    Copy-Item -Path (Join-Path $sdkWinrt.FullName '*') -Destination (Join-Path $gen 'winrt') -Recurse -Force
}

# 3. Configure once, then build what changed.
$build = Join-Path $out ($(if ($Bench) { 'bench' } else { 'release' }))
$benchFlag = if ($Bench) { 'ON' } else { 'OFF' }
if (-not (Test-Path -LiteralPath (Join-Path $build 'build.ninja'))) {
    cmake -S $root -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl `
        -DAMPIFY_XAML=ON "-DAMPIFY_BENCH=$benchFlag" -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded "-DAMPIFY_WINAPPSDK_INCLUDE=$gen"
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed' }
}
cmake --build $build
if ($LASTEXITCODE -ne 0) { throw 'build failed' }
Write-Host "Ready: $build (ampify_launcher.exe, ampify_kernel.dll, ampify.exe)"
