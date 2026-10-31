# The listening pass, in one command: builds the bench kit folder from the local MSVC builds, then runs the live
# case playback-with-kit (bench/windows/live/cases/30-playback-with-kit.ps1): about 45 s of a stereo track (Take Five),
# 45 s of a Dolby Atmos track (bad guy), then the stereo track again, with the kernel and the Atmos leveler. It reads
# Apple Music's volume slider before and after (Ampify never moves it) and checks the logs. Sound comes out: listen.
#
#   pwsh -File scripts\build-msvc.ps1 -Bench; pwsh -File scripts\build-msvc.ps1; pwsh -File scripts\listen-test.ps1
#
# Apple Music must be signed in. The case sets Apple Music's Dolby Atmos setting to "Always on". Results: results\.
param([string]$Storefront = 'fr')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
& pwsh -File (Join-Path $PSScriptRoot 'bench-kit.ps1')
if ($LASTEXITCODE -ne 0) { throw 'cannot lay out the bench kit folder' }
$env:AMPIFY_SIGNED_IN = 'true'
$env:AMPIFY_STOREFRONT = $Storefront
& pwsh -File (Join-Path $root 'bench\windows\run-bench.ps1') -KitRoot (Join-Path $root 'build-msvc\kit') `
    -CasesDir 'live\cases' -Only 'playback-with-kit' -SkipInstall
$code = $LASTEXITCODE
$report = Join-Path $root 'results\report.md'
if (Test-Path -LiteralPath $report) { Get-Content -LiteralPath $report -Encoding UTF8 }
exit $code
