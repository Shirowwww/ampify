# Fonctions communes du banc de test Windows. Chargé par run-bench.ps1 : ne se lance pas seul.
# Pour ajouter un test, voir docs/tests/banc-windows.md.

Set-StrictMode -Version Latest

# Identité de l'app et chemins du kit sur ce PC (mêmes valeurs que core/apple_music.hpp et win/win_common.cpp).
$script:PackageFamily = 'AppleInc.AppleMusicWin_nzyj5cx40ttqa'
$script:AppUserModelId = "$($script:PackageFamily)!App"
$script:StoreProductId = '9PFHDD62MXS1'
$script:KitDir = Join-Path $env:USERPROFILE 'AppleMusicModKit'
$script:JournalDir = Join-Path $script:KitDir 'journal'
$script:StateFile = Join-Path $script:KitDir 'state.txt'

# Rempli par run-bench.ps1 avant les tests.
# Noms distincts des paramètres de run-bench.ps1 : le harnais est chargé dans la même portée que le script.
$script:BenchKitRoot = $null
$script:BenchOutputRoot = $null
$script:AppleMusicInstalled = $false
$script:AppleMusicVersion = ''
$script:KnownVersions = @()
$script:Cases = New-Object System.Collections.Generic.List[object]

function Write-Step {
    param([string]$Text)
    Write-Host "==> $Text"
}

# Enregistre un test. Action reçoit le contexte du test ($Ctx) et doit appeler Add-Check.
function Register-Case {
    param(
        [string]$Id,
        [string]$Title,
        [string[]]$Requires = @(),
        [scriptblock]$Action
    )
    $script:Cases.Add([pscustomobject]@{ Id = $Id; Title = $Title; Requires = $Requires; Action = $Action })
}

function New-CaseContext {
    param($Case)
    $dir = Join-Path $script:BenchOutputRoot $Case.Id
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    return [pscustomobject]@{
        Id = $Case.Id
        Title = $Case.Title
        Dir = $dir
        Checks = New-Object System.Collections.Generic.List[object]
    }
}

# PASS, FAIL, INFO (constat sans verdict) ou SKIP (test impossible sur ce runner, avec la raison).
function Add-Check {
    param($Ctx, [string]$Name, [bool]$Passed, [string]$Detail = '')
    $status = if ($Passed) { 'PASS' } else { 'FAIL' }
    Add-Result -Ctx $Ctx -Name $Name -Status $status -Detail $Detail
}

function Add-Info {
    param($Ctx, [string]$Name, [string]$Detail = '')
    Add-Result -Ctx $Ctx -Name $Name -Status 'INFO' -Detail $Detail
}

function Add-Skip {
    param($Ctx, [string]$Name, [string]$Reason)
    Add-Result -Ctx $Ctx -Name $Name -Status 'SKIP' -Detail $Reason
}

function Add-Result {
    param($Ctx, [string]$Name, [string]$Status, [string]$Detail)
    $Ctx.Checks.Add([pscustomobject]@{ Name = $Name; Status = $Status; Detail = $Detail })
    $suffix = if ($Detail) { " - $Detail" } else { '' }
    Write-Host "  [$Status] $Name$suffix"
}

# Attend qu'une condition soit vraie. Renvoie $false si le délai passe.
function Wait-Until {
    param([scriptblock]$Condition, [int]$TimeoutSeconds, [int]$PollSeconds = 2)
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        if (& $Condition) { return $true }
        Start-Sleep -Seconds $PollSeconds
    } while ((Get-Date) -lt $deadline)
    return $false
}

# Contenu d'un journal du kit (launcher.log, AppleMusic.log, AMPLibraryAgent.log). $null s'il n'existe pas.
function Read-Journal {
    param([string]$FileName)
    $path = Join-Path $script:JournalDir $FileName
    if (-not (Test-Path -LiteralPath $path)) { return $null }
    try {
        return Get-Content -LiteralPath $path -Raw -Encoding UTF8
    } catch {
        return $null
    }
}

function Test-JournalContains {
    param([string]$FileName, [string]$Text)
    $content = Read-Journal $FileName
    return [bool]($content -and $content.Contains($Text))
}

function Wait-Journal {
    param([string]$FileName, [string]$Text, [int]$TimeoutSeconds)
    return Wait-Until -TimeoutSeconds $TimeoutSeconds -Condition { Test-JournalContains -FileName $FileName -Text $Text }
}

# Lignes du journal qui correspondent à une expression régulière, pour relire un chiffre (par exemple les threads).
function Find-JournalMatch {
    param([string]$FileName, [string]$Pattern)
    $content = Read-Journal $FileName
    if (-not $content) { return $null }
    $match = [regex]::Match($content, $Pattern)
    if ($match.Success) { return $match }
    return $null
}

function Start-Launcher {
    param([string]$LauncherPath)
    return Start-Process -FilePath $LauncherPath -WorkingDirectory (Split-Path -Parent $LauncherPath) -PassThru
}

# Lance Apple Music comme le ferait l'utilisateur depuis le menu Démarrer : sans le lanceur, donc sans noyau.
function Start-AppNormally {
    Start-Process -FilePath 'explorer.exe' -ArgumentList "shell:AppsFolder\$($script:AppUserModelId)"
}

# La virgule garde le tableau vide tel quel : sans elle, PowerShell renvoie $null et .Count échoue.
function Get-AppProcesses {
    return ,@(Get-Process -Name 'AppleMusic' -ErrorAction SilentlyContinue)
}

# Ferme Apple Music comme un utilisateur (fenêtre fermée), puis de force si elle ne part pas.
# Renvoie $true si elle est partie toute seule.
function Close-AppleMusic {
    foreach ($process in Get-AppProcesses) {
        $process.CloseMainWindow() | Out-Null
    }
    $gone = Wait-Until -TimeoutSeconds 20 -Condition { (Get-AppProcesses).Count -eq 0 }
    if (-not $gone) {
        Get-AppProcesses | Stop-Process -Force -ErrorAction SilentlyContinue
        Wait-Until -TimeoutSeconds 10 -Condition { (Get-AppProcesses).Count -eq 0 } | Out-Null
    }
    return $gone
}

# Arrête tout ce que le banc peut laisser ouvert. Appelé avant et après chaque test.
function Stop-BenchProcesses {
    Get-Process -Name 'amk_launcher', 'AppleMusic', 'AMPLibraryAgent' -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2
}

# Remet le kit à zéro pour un test : journaux et compteur du mode sans échec. Ne touche pas aux réglages.
function Reset-KitState {
    Stop-BenchProcesses
    Remove-Item -LiteralPath $script:JournalDir -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $script:StateFile -Force -ErrorAction SilentlyContinue
}

# Copie les journaux du kit dans le dossier du test, pour les garder dans les résultats.
function Copy-JournalToResults {
    param($Ctx)
    if (-not (Test-Path -LiteralPath $script:JournalDir)) { return }
    $target = Join-Path $Ctx.Dir 'journal'
    New-Item -ItemType Directory -Force -Path $target | Out-Null
    Copy-Item -Path (Join-Path $script:JournalDir '*') -Destination $target -Force -ErrorAction SilentlyContinue
}

function Set-SafeModeCount {
    param([int]$Count)
    New-Item -ItemType Directory -Force -Path $script:KitDir | Out-Null
    Set-Content -LiteralPath $script:StateFile -Value "unhealthy_launches = $Count" -Encoding UTF8
}

function Get-SafeModeCount {
    if (-not (Test-Path -LiteralPath $script:StateFile)) { return $null }
    $match = [regex]::Match((Get-Content -LiteralPath $script:StateFile -Raw), 'unhealthy_launches\s*=\s*(\d+)')
    if ($match.Success) { return [int]$match.Groups[1].Value }
    return $null
}

# Où en est le noyau dans les processus d'Apple Music : combien en ont le module amk_kernel.dll.
function Get-KernelState {
    param([string]$ProcessName = 'AppleMusic')
    $processes = @(Get-Process -Name $ProcessName -ErrorAction SilentlyContinue)
    $loaded = 0
    $unreadable = 0
    foreach ($process in $processes) {
        try {
            if ($process.Modules | Where-Object { $_.ModuleName -ieq 'amk_kernel.dll' }) { $loaded++ }
        } catch {
            $unreadable++
        }
    }
    return [pscustomobject]@{
        Processes = $processes.Count
        Loaded = $loaded
        Unreadable = $unreadable
    }
}

# Plantages d'Apple Music enregistrés par Windows depuis $Since.
function Get-AppCrashEvents {
    param([datetime]$Since)
    $events = Get-WinEvent -FilterHashtable @{ LogName = 'Application'; StartTime = $Since } -ErrorAction SilentlyContinue
    return ,@($events | Where-Object { ($_.Id -in 1000, 1001, 1002) -and ($_.Message -match 'AppleMusic\.exe|AMPLibraryAgent\.exe') })
}

# Résumé lisible des plantages : identifiant de l'événement et début du message, pour savoir ce qui a planté.
function Format-CrashEvents {
    param([object[]]$Events)
    $lines = @($Events | Select-Object -First 2 | ForEach-Object {
        $text = ([string]$_.Message) -replace '\s+', ' '
        "id $($_.Id) : " + $text.Substring(0, [Math]::Min(240, $text.Length))
    })
    return ($lines -join ' | ')
}

# Capture de l'écran principal dans le dossier du test. Renvoie le chemin, ou $null si l'écran n'est pas accessible.
function Save-Screenshot {
    param($Ctx, [string]$Name)
    try {
        Add-Type -AssemblyName System.Windows.Forms, System.Drawing
        $bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
        $bitmap = New-Object System.Drawing.Bitmap $bounds.Width, $bounds.Height
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        $graphics.CopyFromScreen($bounds.Location, [System.Drawing.Point]::Empty, $bounds.Size)
        $path = Join-Path $Ctx.Dir "$Name.png"
        $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
        $graphics.Dispose()
        $bitmap.Dispose()
        return $path
    } catch {
        Write-Host "  capture d'écran impossible : $($_.Exception.Message)"
        return $null
    }
}

# Versions d'Apple Music de la liste de diffusion, lues dans core/version_gate.hpp pour rester alignées sur le code.
function Get-KnownAppVersions {
    param([string]$RepoRoot)
    $header = Get-Content -LiteralPath (Join-Path $RepoRoot 'core\version_gate.hpp') -Raw
    $block = [regex]::Match($header, 'kKnownAppVersions\[\]\s*=\s*\{(?<body>.*?)\};', 'Singleline')
    if (-not $block.Success) { throw 'kKnownAppVersions introuvable dans core/version_gate.hpp' }
    return @([regex]::Matches($block.Groups['body'].Value, '"([0-9.]+)"') | ForEach-Object { $_.Groups[1].Value })
}

# Nom complet du paquet d'Apple Music installé pour l'utilisateur courant, ou '' s'il n'est pas installé.
# Interrogé dans Windows PowerShell 5.1 : Get-AppxPackage passe par un module de compatibilité sous PowerShell 7.
function Get-AppleMusicFullName {
    $query = "(Get-AppxPackage -Name 'AppleInc.AppleMusicWin' | Select-Object -First 1).PackageFullName"
    $name = & powershell.exe -NoProfile -NonInteractive -Command $query 2>$null
    return [string]($name | Select-Object -First 1)
}

# Version dans un nom complet de paquet : AppleInc.AppleMusicWin_1.1540.23042.0_x64__nzyj5cx40ttqa.
function Get-VersionFromFullName {
    param([string]$FullName)
    $parts = $FullName -split '_'
    if ($parts.Count -ne 5) { return '' }
    return $parts[1]
}

# Installe Apple Music avec winget (client officiel de Microsoft, source Microsoft Store).
# Renvoie une phrase qui dit ce qui s'est passé. Le journal complet de winget va dans $LogFile.
function Install-AppleMusic {
    param([string]$LogFile)
    # winget writes progress to stderr: that must not stop the script.
    $ErrorActionPreference = 'Continue'
    if (Get-AppleMusicFullName) { return 'déjà installé sur ce runner' }

    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) { return 'winget absent de ce runner' }

    $wingetArgs = @(
        'install', '--id', $script:StoreProductId, '--source', 'msstore', '--exact', '--silent',
        '--accept-package-agreements', '--accept-source-agreements', '--disable-interactivity'
    )
    $output = & winget @wingetArgs 2>&1 | Out-String
    $exitCode = $LASTEXITCODE
    Set-Content -LiteralPath $LogFile -Value $output -Encoding UTF8

    if (Get-AppleMusicFullName) { return 'installé par winget' }
    return "winget n'a pas installé Apple Music (code de sortie $exitCode, voir winget.log)"
}
