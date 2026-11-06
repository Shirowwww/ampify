# Lays out the kit folder of the Windows bench from the local MSVC builds, as the CI job msvc-build does:
#   release\    ampify_launcher, ampify_kernel, ampify_setup, ampify (shipping build)
#   cli\        ampify.exe
#   bench\, ui\ ampify_launcher, ampify_kernel (bench build), plus ampify.exe, themes\, examples\react-panel in ui\
#
#   pwsh -File scripts\build-msvc.ps1 -Bench; pwsh -File scripts\build-msvc.ps1; pwsh -File scripts\bench-kit.ps1
#   pwsh -File bench\windows\run-bench.ps1 -KitRoot build-msvc\kit -Only ui-surfaces -SkipInstall
#
# The bench build accepts any Apple Music version: never ship build-msvc\kit.
param([string]$Out = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build-msvc\kit'))
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$builds = @{ release = Join-Path $root 'build-msvc\release'; bench = Join-Path $root 'build-msvc\bench' }

function Copy-Built {
    param([string]$From, [string]$To, [string[]]$Names)
    New-Item -ItemType Directory -Force -Path $To | Out-Null
    foreach ($name in $Names) {
        $found = Get-ChildItem -LiteralPath $From -Recurse -Filter $name -ErrorAction SilentlyContinue | Select-Object -First 1
        if (-not $found) { throw "$name missing from $From (build first with scripts\build-msvc.ps1)" }
        Copy-Item -LiteralPath $found.FullName -Destination $To -Force
    }
}

$files = (Get-Content -LiteralPath (Join-Path $root 'facts\release.json') -Raw | ConvertFrom-Json).files

Remove-Item -LiteralPath $Out -Recurse -Force -ErrorAction SilentlyContinue
Copy-Built $builds.release (Join-Path $Out 'release') @($files.launcher, $files.kernel, $files.setup, $files.cli)
Copy-Built $builds.release (Join-Path $Out 'cli') @($files.cli)
Copy-Built $builds.bench (Join-Path $Out 'bench') @($files.launcher, $files.kernel, $files.cli)
Copy-Built $builds.bench (Join-Path $Out 'ui') @($files.launcher, $files.kernel, $files.cli)
New-Item -ItemType Directory -Force -Path (Join-Path $Out 'ui\examples') | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'themes') -Destination (Join-Path $Out 'ui') -Recurse
Copy-Item -LiteralPath (Join-Path $root 'examples\react-panel') -Destination (Join-Path $Out 'ui\examples') -Recurse
$commit = (git -C $root rev-parse --short HEAD)
"Commit: $commit`nrelease: AMPIFY_BENCH=OFF (MSVC, XAML)`nbench and ui: AMPIFY_BENCH=ON (MSVC, XAML). Never ship." |
    Set-Content -LiteralPath (Join-Path $Out 'BUILD_INFO.txt')
Write-Host "Bench kit ready: $Out"
