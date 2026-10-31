<#
.SYNOPSIS
Windows test bench: installs Apple Music if needed, runs the cases of bench/windows/cases and writes the report.

.DESCRIPTION
Each file of cases/ registers one or more cases (Register-Case). A case whose requirement is missing is skipped,
with the reason in the report. The bench closes Apple Music by force and empties the Ampify logs: do not run it
on a PC where Apple Music is in use. To add a case, see docs/development/bench-and-ci.md.

.PARAMETER KitRoot
Folder that holds release\ (release build) and bench\ (bench build), with ampify_launcher.exe and ampify_kernel.dll.

.PARAMETER RepoRoot
Root of the repository: the list of tested versions is read from core\version_gate.hpp.

.PARAMETER OutputRoot
Results folder: results.json, report.md, winget.log, and one folder per case (logs, screenshots).

.PARAMETER Only
Ids of the cases to run. Empty: all of them.

.PARAMETER SkipInstall
Does not try to install Apple Music.

.PARAMETER CasesDir
Folder of the cases, relative to this script. Default: cases (the normal bench). live\cases: the signed-in bench
(run locally), which needs a signed-in Apple Music and an audio output.

.OUTPUTS
Exit code 1 when a case fails, 0 otherwise. Skipped cases do not fail the bench.
#>
[CmdletBinding()]
param(
    [string]$KitRoot = (Join-Path $PSScriptRoot '..\..\dist'),
    [string]$RepoRoot = (Join-Path $PSScriptRoot '..\..'),
    [string]$OutputRoot = (Join-Path $PSScriptRoot '..\..\results'),
    [string[]]$Only = @(),
    [switch]$SkipInstall,
    [string]$CasesDir = 'cases'
)

$ErrorActionPreference = 'Stop'

# An unexpected error stops the bench, with the faulty line in the job log.
trap {
    Write-Host "::error::bench interrupted: $($_.Exception.GetType().FullName): $($_.Exception.Message)"
    Write-Host $_.InvocationInfo.PositionMessage
    Write-Host $_.Exception.ToString()
    Write-Host $_.ScriptStackTrace
    exit 1
}

# Startup diagnostic: if a base path is empty, the rest fails further down without saying why.
Write-Host "Startup: PSScriptRoot=[$PSScriptRoot] PSCommandPath=[$PSCommandPath] USERPROFILE=[$env:USERPROFILE]"
if (-not $PSScriptRoot -or -not $env:USERPROFILE) {
    throw 'Base paths are empty: start the bench with pwsh -File bench/windows/run-bench.ps1'
}

. (Join-Path $PSScriptRoot 'lib\harness.ps1')
Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot $CasesDir) -Filter '*.ps1' | Sort-Object Name | ForEach-Object {
    . $_.FullName
}

# -Only may arrive as one comma-separated string from the workflow (pwsh -File passes it as is).
$Only = @($Only | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })

New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
$script:BenchKitRoot = (Resolve-Path -LiteralPath $KitRoot).Path
# Cases use either name. Both are absolute, so a case may change folders (Push-Location) and still find the kit.
$script:KitRoot = $script:BenchKitRoot
$script:BenchOutputRoot = (Resolve-Path -LiteralPath $OutputRoot).Path
$repo = (Resolve-Path -LiteralPath $RepoRoot).Path

function Test-Requirement {
    param([string]$Name)
    switch ($Name) {
        'apple-music' { return $script:AppleMusicInstalled }
        # Set by the signed-in bench after bench/windows/live/sign-in.ps1: Apple Music is signed in to the test account.
        'apple-account' { return ($script:AppleMusicInstalled -and $env:AMPIFY_SIGNED_IN -eq 'true') }
        default { throw "Unknown requirement: $Name" }
    }
}

# FAIL when a check fails; SKIP when nothing could be checked; PASS otherwise (INFO does not count).
function Get-CaseStatus {
    param($Ctx)
    $status = 'SKIP'
    foreach ($check in $Ctx.Checks) {
        if ($check.Status -eq 'FAIL') { return 'FAIL' }
        if ($check.Status -eq 'PASS' -or $check.Status -eq 'INFO') { $status = 'PASS' }
    }
    return $status
}

function Format-Cell {
    param([string]$Text)
    return ($Text -replace '\|', '\|' -replace "[\r\n]+", ' ')
}

# Preparation: environment, build, versions, Apple Music install.
Write-Step 'Preparation'
$os = Get-CimInstance -ClassName Win32_OperatingSystem
$preparation = New-Object System.Collections.Generic.List[string]
$preparation.Add("System: $($os.Caption) $($os.Version)")
$preparation.Add("PowerShell: $($PSVersionTable.PSVersion)")
$preparation.Add("Date: $((Get-Date).ToString('s'))")
$buildInfo = Join-Path $script:BenchKitRoot 'BUILD_INFO.txt'
if (Test-Path -LiteralPath $buildInfo) {
    Get-Content -LiteralPath $buildInfo | ForEach-Object { $preparation.Add($_) }
}

$script:KnownVersions = @(Get-KnownAppVersions -RepoRoot $repo)
$preparation.Add("Tested versions (core/version_gate.hpp): $($script:KnownVersions -join ', ')")

$installState = 'install skipped (-SkipInstall)'
if (-not $SkipInstall) {
    $installState = Install-AppleMusic -LogFile (Join-Path $script:BenchOutputRoot 'winget.log')
}
$fullName = Get-AppleMusicFullName
$script:AppleMusicInstalled = [bool]$fullName
if ($fullName) { $script:AppleMusicVersion = Get-VersionFromFullName -FullName $fullName }
$preparation.Add("Apple Music: $installState")
$preparation.Add("Installed version: $(if ($script:AppleMusicVersion) { $script:AppleMusicVersion } else { 'none' })")
if (-not $script:AppleMusicInstalled) {
    Write-Host "::warning::Apple Music is not installed on this runner: the app cases are skipped ($installState)"
}

$preparation | ForEach-Object { Write-Host $_ }
$preparation | Set-Content -LiteralPath (Join-Path $script:BenchOutputRoot 'preparation.txt') -Encoding UTF8

# The cases.
$report = @()
foreach ($case in $script:Cases) {
    if ($Only.Count -gt 0 -and $Only -notcontains $case.Id) { continue }

    Write-Host "::group::$($case.Id): $($case.Title)"
    $ctx = New-CaseContext -Case $case
    Stop-BenchProcesses
    $missing = @($case.Requires | Where-Object { -not (Test-Requirement -Name $_) })
    if ($missing.Count -gt 0) {
        Add-Skip -Ctx $ctx -Name 'requirements' -Reason ("missing: " + ($missing -join ', ') + " (preparation: $installState)")
    } else {
        try {
            & $case.Action $ctx
        } catch {
            Add-Check -Ctx $ctx -Name 'the case ran without error' -Passed $false -Detail $_.Exception.Message
            Write-Host $_.ScriptStackTrace
        }
    }
    Stop-BenchProcesses
    Copy-JournalToResults -Ctx $ctx
    $caseStatus = Get-CaseStatus -Ctx $ctx
    $entry = [pscustomobject]@{
        Id = $case.Id
        Title = $case.Title
        Status = $caseStatus
        Checks = $ctx.Checks.ToArray()
    }
    $report += $entry
    Write-Host '::endgroup::'
}

# Results: JSON for machines, Markdown report for people.
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $script:BenchOutputRoot 'results.json') -Encoding UTF8

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add('# Windows test bench')
$lines.Add('')
$lines.Add('## Preparation')
$lines.Add('')
foreach ($line in $preparation) { $lines.Add("- $(Format-Cell $line)") }
$lines.Add('')
$lines.Add('## Cases')
$lines.Add('')
$lines.Add('| Case | Result |')
$lines.Add('|---|---|')
foreach ($case in $report) { $lines.Add("| $($case.Id): $(Format-Cell $case.Title) | **$($case.Status)** |") }
foreach ($case in $report) {
    $lines.Add('')
    $lines.Add("### $($case.Id): $(Format-Cell $case.Title) ($($case.Status))")
    $lines.Add('')
    $lines.Add('| Check | Result | Detail |')
    $lines.Add('|---|---|---|')
    foreach ($check in $case.Checks) {
        $lines.Add("| $(Format-Cell $check.Name) | $($check.Status) | $(Format-Cell $check.Detail) |")
    }
}
$lines | Set-Content -LiteralPath (Join-Path $script:BenchOutputRoot 'report.md') -Encoding UTF8

if ($env:GITHUB_STEP_SUMMARY) {
    $lines | Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Encoding UTF8
}

$failed = @($report | Where-Object { $_.Status -eq 'FAIL' })
$skipped = @($report | Where-Object { $_.Status -eq 'SKIP' })
Write-Step "Summary: $($report.Count) case(s), $($failed.Count) failed, $($skipped.Count) skipped"
if ($failed.Count -gt 0) {
    $failed | ForEach-Object { Write-Host "::error::bench: $($_.Id) failed" }
    exit 1
}
exit 0
