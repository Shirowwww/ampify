<#
.SYNOPSIS
Banc de test Windows : installe Apple Music si besoin, lance les tests de bench/windows/cases et écrit le rapport.

.DESCRIPTION
Chaque fichier de cases/ enregistre un ou plusieurs tests (Register-Case). Un test dont le prérequis manque est sauté,
avec la raison dans le rapport. Le banc ferme Apple Music de force et remet à zéro les journaux du kit : ne pas le
lancer sur un PC où l'on utilise Apple Music. Pour ajouter un test, voir docs/tests/banc-windows.md.

.PARAMETER KitRoot
Dossier qui contient diffusion\ (build de diffusion) et banc\ (build de banc), avec amk_launcher.exe et amk_kernel.dll.

.PARAMETER RepoRoot
Racine du dépôt : la liste des versions testées est lue dans core\version_gate.hpp.

.PARAMETER OutputRoot
Dossier des résultats : results.json, rapport.md, winget.log, et un dossier par test (journaux, captures).

.PARAMETER Only
Identifiants des tests à lancer. Vide : tous.

.PARAMETER SkipInstall
N'essaie pas d'installer Apple Music.

.OUTPUTS
Code de sortie 1 si un test échoue, 0 sinon. Les tests sautés ne font pas échouer le banc.
#>
[CmdletBinding()]
param(
    [string]$KitRoot = (Join-Path $PSScriptRoot '..\..\dist'),
    [string]$RepoRoot = (Join-Path $PSScriptRoot '..\..'),
    [string]$OutputRoot = (Join-Path $PSScriptRoot '..\..\results'),
    [string[]]$Only = @(),
    [switch]$SkipInstall
)

$ErrorActionPreference = 'Stop'

# Une erreur non prévue arrête le banc, avec la ligne fautive dans le journal du job.
trap {
    Write-Host "::error::banc interrompu : $($_.Exception.GetType().FullName) : $($_.Exception.Message)"
    Write-Host $_.InvocationInfo.PositionMessage
    Write-Host $_.Exception.ToString()
    Write-Host $_.ScriptStackTrace
    exit 1
}

# Diagnostic de démarrage : si un chemin de base est vide, la suite échoue plus bas sans dire pourquoi.
Write-Host "Démarrage : PSScriptRoot=[$PSScriptRoot] PSCommandPath=[$PSCommandPath] USERPROFILE=[$env:USERPROFILE]"
if (-not $PSScriptRoot -or -not $env:USERPROFILE) {
    throw 'Chemins de base vides : lancer le banc avec pwsh -File bench/windows/run-bench.ps1'
}

. (Join-Path $PSScriptRoot 'lib\harness.ps1')
Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'cases') -Filter '*.ps1' | Sort-Object Name | ForEach-Object {
    . $_.FullName
}

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
        default { throw "Prérequis inconnu : $Name" }
    }
}

# FAIL si une vérification échoue ; SKIP si rien n'a pu être vérifié ; PASS sinon (INFO ne compte pas).
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

# Préparation : environnement, build, versions, installation d'Apple Music.
Write-Step 'Préparation'
$os = Get-CimInstance -ClassName Win32_OperatingSystem
$preparation = New-Object System.Collections.Generic.List[string]
$preparation.Add("Système : $($os.Caption) $($os.Version)")
$preparation.Add("PowerShell : $($PSVersionTable.PSVersion)")
$preparation.Add("Date : $((Get-Date).ToString('s'))")
$buildInfo = Join-Path $script:BenchKitRoot 'BUILD_INFO.txt'
if (Test-Path -LiteralPath $buildInfo) {
    Get-Content -LiteralPath $buildInfo | ForEach-Object { $preparation.Add($_) }
}

$script:KnownVersions = @(Get-KnownAppVersions -RepoRoot $repo)
$preparation.Add("Versions testées (core/version_gate.hpp) : $($script:KnownVersions -join ', ')")

$installState = 'installation sautée (-SkipInstall)'
if (-not $SkipInstall) {
    $installState = Install-AppleMusic -LogFile (Join-Path $script:BenchOutputRoot 'winget.log')
}
$fullName = Get-AppleMusicFullName
$script:AppleMusicInstalled = [bool]$fullName
if ($fullName) { $script:AppleMusicVersion = Get-VersionFromFullName -FullName $fullName }
$preparation.Add("Apple Music : $installState")
$preparation.Add("Version installée : $(if ($script:AppleMusicVersion) { $script:AppleMusicVersion } else { 'aucune' })")
if (-not $script:AppleMusicInstalled) {
    Write-Host "::warning::Apple Music n'est pas installé sur ce runner : les tests d'application sont sautés ($installState)"
}

$preparation | ForEach-Object { Write-Host $_ }
$preparation | Set-Content -LiteralPath (Join-Path $script:BenchOutputRoot 'preparation.txt') -Encoding UTF8

# Les tests.
$report = @()
foreach ($case in $script:Cases) {
    if ($Only.Count -gt 0 -and $Only -notcontains $case.Id) { continue }

    Write-Host "::group::$($case.Id) : $($case.Title)"
    $ctx = New-CaseContext -Case $case
    Stop-BenchProcesses
    $missing = @($case.Requires | Where-Object { -not (Test-Requirement -Name $_) })
    if ($missing.Count -gt 0) {
        Add-Skip -Ctx $ctx -Name 'prérequis' -Reason ("manque : " + ($missing -join ', ') + " (préparation : $installState)")
    } else {
        try {
            & $case.Action $ctx
        } catch {
            Add-Check -Ctx $ctx -Name 'le test s''est déroulé sans erreur' -Passed $false -Detail $_.Exception.Message
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

# Résultats : JSON pour les machines, rapport Markdown pour les humains.
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $script:BenchOutputRoot 'results.json') -Encoding UTF8

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add('# Banc de test Windows')
$lines.Add('')
$lines.Add('## Préparation')
$lines.Add('')
foreach ($line in $preparation) { $lines.Add("- $(Format-Cell $line)") }
$lines.Add('')
$lines.Add('## Tests')
$lines.Add('')
$lines.Add('| Test | Résultat |')
$lines.Add('|---|---|')
foreach ($case in $report) { $lines.Add("| $($case.Id) : $(Format-Cell $case.Title) | **$($case.Status)** |") }
foreach ($case in $report) {
    $lines.Add('')
    $lines.Add("### $($case.Id) : $(Format-Cell $case.Title) ($($case.Status))")
    $lines.Add('')
    $lines.Add('| Vérification | Résultat | Détail |')
    $lines.Add('|---|---|---|')
    foreach ($check in $case.Checks) {
        $lines.Add("| $(Format-Cell $check.Name) | $($check.Status) | $(Format-Cell $check.Detail) |")
    }
}
$lines | Set-Content -LiteralPath (Join-Path $script:BenchOutputRoot 'rapport.md') -Encoding UTF8

if ($env:GITHUB_STEP_SUMMARY) {
    $lines | Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Encoding UTF8
}

$failed = @($report | Where-Object { $_.Status -eq 'FAIL' })
$skipped = @($report | Where-Object { $_.Status -eq 'SKIP' })
Write-Step "Bilan : $($report.Count) test(s), $($failed.Count) en échec, $($skipped.Count) sauté(s)"
if ($failed.Count -gt 0) {
    $failed | ForEach-Object { Write-Host "::error::banc : $($_.Id) en échec" }
    exit 1
}
exit 0
