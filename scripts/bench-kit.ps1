# Lays out the kit folder of the Windows bench from MSVC builds. The only place that knows the layout: the CI jobs
# call it too, and scripts/package.sh and bench/windows/run-bench.ps1 read what it writes.
#   release\    ampify_launcher, ampify_kernel, ampify_setup, ampify (shipping build)
#   cli\        ampify.exe
#   bench\, ui\ ampify_launcher, ampify_kernel, ampify (bench build); ui\ also holds themes\ and examples\react-panel
#   BUILD_INFO.txt   the commit, and the words scripts/package.sh checks (written with the release part)
#
#   pwsh -File scripts\build-msvc.ps1 -Bench; pwsh -File scripts\build-msvc.ps1; pwsh -File scripts\bench-kit.ps1
#   pwsh -File bench\windows\run-bench.ps1 -KitRoot build-msvc\kit -Only ui-surfaces -SkipInstall
#
# CI builds each variant in its own job: -Variant release -ReleaseBuild build, then -Variant bench -BenchBuild build,
# both into one -Out folder (the artifacts are merged). The bench build accepts any Apple Music version: never ship it.
param(
    [ValidateSet('all', 'release', 'bench')][string]$Variant = 'all',
    [string]$ReleaseBuild = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build-msvc\release'),
    [string]$BenchBuild = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build-msvc\bench'),
    [string]$Out = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build-msvc\kit')
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

function Copy-Built {
    param([string]$From, [string]$To, [string[]]$Names)
    New-Item -ItemType Directory -Force -Path $To | Out-Null
    foreach ($name in $Names) {
        $found = Get-Item -LiteralPath (Join-Path $From $name) -ErrorAction SilentlyContinue
        if (-not $found) {
            $found = Get-ChildItem -LiteralPath $From -Recurse -Filter $name -ErrorAction SilentlyContinue | Select-Object -First 1
        }
        if (-not $found) { throw "$name missing from $From (build first with scripts\build-msvc.ps1)" }
        Copy-Item -LiteralPath $found.FullName -Destination $To -Force
    }
}

$files = (Get-Content -LiteralPath (Join-Path $root 'facts\release.json') -Raw | ConvertFrom-Json).files
$all = @($files.launcher, $files.kernel, $files.setup, $files.cli)
$bench = @($files.launcher, $files.kernel, $files.cli)

if ($Variant -eq 'all') { Remove-Item -LiteralPath $Out -Recurse -Force -ErrorAction SilentlyContinue }
if ($Variant -ne 'bench') {
    Copy-Built $ReleaseBuild (Join-Path $Out 'release') $all
    Copy-Built $ReleaseBuild (Join-Path $Out 'cli') @($files.cli)
    $commit = if ($env:GITHUB_SHA) { $env:GITHUB_SHA } else { git -C $root rev-parse --short HEAD }
    "Commit: $commit`nrelease: AMPIFY_BENCH=OFF (MSVC, XAML)`nbench and ui: AMPIFY_BENCH=ON (MSVC, XAML). Never ship." |
        Set-Content -LiteralPath (Join-Path $Out 'BUILD_INFO.txt')
}
if ($Variant -ne 'release') {
    Copy-Built $BenchBuild (Join-Path $Out 'bench') $bench
    Copy-Built $BenchBuild (Join-Path $Out 'ui') $bench
    # The interface cases need the example themes and the React panel example (63-ui-surfaces).
    New-Item -ItemType Directory -Force -Path (Join-Path $Out 'ui\examples') | Out-Null
    Copy-Item -LiteralPath (Join-Path $root 'themes') -Destination (Join-Path $Out 'ui') -Recurse
    Copy-Item -LiteralPath (Join-Path $root 'examples\react-panel') -Destination (Join-Path $Out 'ui\examples') -Recurse
}
Write-Host "Bench kit ready: $Out ($Variant)"
