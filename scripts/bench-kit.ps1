# Lays out the kit folder of the Windows bench from the local MSVC builds, as the CI job xaml-m8 does:
#   diffusion\  amk_launcher, amk_kernel, amk_setup, AMK-Test (shipping build)
#   cli\        amk.exe
#   banc\, m8\  amk_launcher, amk_kernel (bench build), plus amk_theme_check, themes\examples, examples\react-panel in m8\
#
#   pwsh -File scripts\build-msvc.ps1 -Bench; pwsh -File scripts\build-msvc.ps1; pwsh -File scripts\bench-kit.ps1
#   pwsh -File bench\windows\run-bench.ps1 -KitRoot build-msvc\kit -Only m8-interface -SkipInstall
#
# The bench build accepts any Apple Music version: never ship build-msvc\kit.
param([string]$Out = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build-msvc\kit'))
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$builds = @{ diffusion = Join-Path $root 'build-msvc\diffusion'; banc = Join-Path $root 'build-msvc\banc' }

function Copy-Built {
    param([string]$From, [string]$To, [string[]]$Names)
    New-Item -ItemType Directory -Force -Path $To | Out-Null
    foreach ($name in $Names) {
        $found = Get-ChildItem -LiteralPath $From -Recurse -Filter $name -ErrorAction SilentlyContinue | Select-Object -First 1
        if (-not $found) { throw "$name absent de $From (compiler d'abord avec scripts\build-msvc.ps1)" }
        Copy-Item -LiteralPath $found.FullName -Destination $To -Force
    }
}

Remove-Item -LiteralPath $Out -Recurse -Force -ErrorAction SilentlyContinue
Copy-Built $builds.diffusion (Join-Path $Out 'diffusion') @('amk_launcher.exe', 'amk_kernel.dll', 'amk_setup.exe', 'AMK-Test.exe')
Copy-Built $builds.diffusion (Join-Path $Out 'cli') @('amk.exe')
Copy-Built $builds.banc (Join-Path $Out 'banc') @('amk_launcher.exe', 'amk_kernel.dll')
Copy-Built $builds.banc (Join-Path $Out 'm8') @('amk_launcher.exe', 'amk_kernel.dll', 'amk_theme_check.exe')
New-Item -ItemType Directory -Force -Path (Join-Path $Out 'm8\themes'), (Join-Path $Out 'm8\examples') | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'themes\examples') -Destination (Join-Path $Out 'm8\themes') -Recurse
Copy-Item -LiteralPath (Join-Path $root 'examples\react-panel') -Destination (Join-Path $Out 'm8\examples') -Recurse
$commit = (git -C $root rev-parse --short HEAD)
"Commit : $commit (local)`ndiffusion : AMK_BENCH=OFF`nbanc et m8 : AMK_BENCH=ON. Ne jamais diffuser." |
    Set-Content -LiteralPath (Join-Path $Out 'BUILD_INFO.txt')
Write-Host "Kit du banc prêt : $Out"
