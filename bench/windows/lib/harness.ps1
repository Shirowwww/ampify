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

# Fin d'un journal du kit dans le journal du job : pour lire pourquoi un test échoue sans ouvrir l'artefact.
function Write-JournalTail {
    param([string]$FileName, [int]$Lines = 40)
    Write-Host "----- $FileName (fin du journal)"
    $content = Read-Journal $FileName
    if (-not $content) {
        Write-Host '  (absent ou vide)'
        return
    }
    $content -split "`r?`n" | Select-Object -Last $Lines | ForEach-Object { Write-Host "  $_" }
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
# Démarre AMPLibraryAgent.exe en activant la classe COM que le paquet d'Apple Music déclare (serveur hors processus
# AMP.Core.IAMPMusicLibrary). L'activation se fait dans un pwsh séparé, arrêté au bout de 20 s s'il reste bloqué :
# seul le démarrage du processus compte, l'objet obtenu n'est pas utilisé. Renvoie une ligne pour le rapport.
function Start-AgentByCom {
    $clsid = '68E7097C-F969-4006-AAC3-95115F0ED1C4'
    $script = "try { `$t = [Type]::GetTypeFromCLSID('$clsid'); `$o = [Activator]::CreateInstance(`$t); 'activée' } catch { 'erreur : ' + `$_.Exception.Message }"
    $out = Join-Path ([IO.Path]::GetTempPath()) 'amk-agent-com.txt'
    Remove-Item -LiteralPath $out -ErrorAction SilentlyContinue
    $process = Start-Process -FilePath 'pwsh' -ArgumentList @('-NoProfile', '-Command', $script) -RedirectStandardOutput $out -PassThru -WindowStyle Hidden
    if (-not $process.WaitForExit(20000)) { $process | Stop-Process -Force -ErrorAction SilentlyContinue }
    $result = if (Test-Path -LiteralPath $out) { (Get-Content -LiteralPath $out -Raw).Trim() } else { '' }
    $started = Wait-Until -TimeoutSeconds 15 -Condition { @(Get-Process -Name 'AMPLibraryAgent' -ErrorAction SilentlyContinue).Count -gt 0 }
    return "classe $clsid : $(if ($result) { $result } else { 'pas de réponse' }) ; AMPLibraryAgent.exe $(if ($started) { 'démarré' } else { 'toujours absent' })"
}

function Start-AppNormally {
    Start-Process -FilePath 'explorer.exe' -ArgumentList "shell:AppsFolder\$($script:AppUserModelId)"
}

# Les processus sortent un par un : les appelants les comptent avec @(...), qui donne un tableau même vide ou à un élément.
function Get-AppProcesses {
    Get-Process -Name 'AppleMusic' -ErrorAction SilentlyContinue
}

# Ferme Apple Music comme un utilisateur (fenêtre fermée), puis de force si elle ne part pas.
# Renvoie $true si elle est partie toute seule.
function Close-AppleMusic {
    foreach ($process in Get-AppProcesses) {
        $process.CloseMainWindow() | Out-Null
    }
    $gone = Wait-Until -TimeoutSeconds 20 -Condition { @(Get-AppProcesses).Count -eq 0 }
    if (-not $gone) {
        Get-AppProcesses | Stop-Process -Force -ErrorAction SilentlyContinue
        Wait-Until -TimeoutSeconds 10 -Condition { @(Get-AppProcesses).Count -eq 0 } | Out-Null
    }
    return $gone
}

# Premier lancement d'Apple Music sur un runner neuf : la licence des codecs (MPEG LA, boutons Agree / Decline /
# Save As) s'ouvre avant la fenêtre principale, puis, après une connexion, l'écran « Welcome to Apple Music » (Start
# Listening). Tant qu'ils sont ouverts, le noyau n'a pas de fenêtre XAML, et l'inspecteur comme UI Automation voient
# la licence au lieu de l'app. Les accepte comme un nouvel utilisateur, par UI Automation seule (mêmes règles que
# Close-FirstRunDialogs de live\live-common.ps1, qui demande uia.ps1). Rend la main quand l'app montre sa navigation
# deux fois de suite sans aucun de ces écrans, ou au bout de $TimeoutSeconds. Renvoie le nombre d'écrans fermés,
# -1 si UI Automation est indisponible.
function Close-AppleMusicFirstRun {
    param([int]$TimeoutSeconds = 90)
    try {
        Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes -ErrorAction Stop
    } catch {
        Write-Host "  premier lancement : UI Automation indisponible ($($_.Exception.Message))"
        return -1
    }
    $AE = [System.Windows.Automation.AutomationElement]
    $buttonCondition = New-Object System.Windows.Automation.PropertyCondition ($AE::ControlTypeProperty), ([System.Windows.Automation.ControlType]::Button)
    $agreePattern = '^(Agree|I Agree|Accept|Accepter|J.accepte)$'
    $declinePattern = '^(Decline|Refuser|Save As|Save As\.\.\.|Enregistrer sous.*)$'
    $welcomePattern = '^(Start Listening|Commencer l.écoute)$'
    $laterPattern = '^(Continue|Continuer|Get Started|Commencer|Not Now|Plus tard|Pas maintenant)$'
    $readyPattern = '^(Search|Rechercher|Home|Accueil|Listen Now|Écouter|Browse|Explorer|Parcourir|Radio|Library|Bibliothèque)$'

    # The visible button of $Buttons whose name matches $Pattern, or $null.
    $findButton = {
        param($Buttons, [string]$Pattern)
        foreach ($button in $Buttons) {
            try {
                if (-not $button.Current.IsOffscreen -and ([string]$button.Current.Name -match $Pattern)) { return $button }
            } catch {
            }
        }
        return $null
    }
    $invoke = {
        param($Element)
        $pattern = $null
        try {
            if ($Element.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$pattern)) {
                $pattern.Invoke()
                return $true
            }
        } catch {
        }
        return $false
    }

    $closed = 0
    $calm = 0
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        $ids = @(Get-Process -Name 'AppleMusic' -ErrorAction SilentlyContinue | ForEach-Object { $_.Id })
        $windows = @()
        if ($ids.Count -gt 0) {
            try {
                foreach ($child in $AE::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, [System.Windows.Automation.Condition]::TrueCondition)) {
                    try {
                        if ($ids -contains $child.Current.ProcessId) { $windows += $child }
                    } catch {
                    }
                }
            } catch {
            }
        }
        $buttons = @()
        foreach ($window in $windows) {
            try {
                $buttons += @($window.FindAll([System.Windows.Automation.TreeScope]::Descendants, $buttonCondition))
            } catch {
            }
        }

        $agree = & $findButton $buttons $agreePattern
        $decline = & $findButton $buttons $declinePattern
        $welcome = & $findButton $buttons $welcomePattern
        $dialog = $null
        $what = ''
        if ($agree -and $decline) {
            $dialog = $agree
            $what = 'licence MPEG LA acceptée (Agree)'
        } elseif ($welcome) {
            $dialog = $welcome
            $what = "écran d'accueil passé ($([string]$welcome.Current.Name))"
        }

        $ready = $false
        if (-not $dialog -and $windows.Count -gt 0) {
            foreach ($window in $windows) {
                try {
                    foreach ($element in $window.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)) {
                        try {
                            if ([string]$element.Current.Name -match $readyPattern) {
                                $ready = $true
                                break
                            }
                        } catch {
                        }
                    }
                } catch {
                }
                if ($ready) { break }
            }
            # A generic « Continue » or « Not Now » only counts on a screen without the app's navigation.
            if (-not $ready) {
                $later = & $findButton $buttons $laterPattern
                if ($later) {
                    $dialog = $later
                    $what = "écran d'accueil passé ($([string]$later.Current.Name))"
                }
            }
        }

        if ($dialog) {
            $calm = 0
            if (& $invoke $dialog) {
                Write-Host "  premier lancement : $what"
                $closed++
            }
            Start-Sleep -Seconds 3
            continue
        }
        if ($ready) {
            $calm++
            if ($calm -ge 2) { return $closed }
        } else {
            $calm = 0
        }
        Start-Sleep -Seconds 2
    }
    Write-Host "  premier lancement : navigation d'Apple Music non vue après $TimeoutSeconds s ($closed écran(s) fermé(s))"
    return $closed
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

# Plantages d'Apple Music enregistrés par Windows depuis $Since, en objets simples (Id, Heure, Message).
# Les appelants l'enveloppent dans @(...) : une virgule ici rangerait le tableau dans un second tableau, et le compte serait faux.
function Get-AppCrashEvents {
    param([datetime]$Since)
    $records = @(Get-WinEvent -FilterHashtable @{ LogName = 'Application'; StartTime = $Since } -ErrorAction SilentlyContinue)
    $crashes = @()
    foreach ($record in $records) {
        if ($record.Id -notin 1000, 1001, 1002) { continue }
        $message = [string]$record.Message
        if ($message -notmatch 'AppleMusic\.exe|AMPLibraryAgent\.exe') { continue }
        $crashes += [pscustomobject]@{ Id = $record.Id; Time = $record.TimeCreated; Message = $message }
    }
    return $crashes
}

# Résumé lisible des plantages : identifiant de l'événement et début du message, pour savoir ce qui a planté.
function Format-CrashEvents {
    param([object[]]$Events)
    $lines = @()
    foreach ($crash in $Events) {
        if ($lines.Count -ge 2) { break }
        $text = [string]$crash.Message -replace '\s+', ' '
        $lines += "id $($crash.Id) : " + $text.Substring(0, [Math]::Min(240, $text.Length))
    }
    return ($lines -join ' | ')
}

# Capture de l'écran principal dans le dossier du test. Renvoie le chemin, ou $null si l'écran n'est pas accessible.
# Win32 calls for the captures: bring Apple Music's window forward and draw it whole, even when another window
# (the runner's console) sits on top. PrintWindow with PW_RENDERFULLCONTENT (2) also draws WinUI's composition content.
if (-not ('AmkWindowCapture' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class AmkWindowCapture {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
    [DllImport("kernel32.dll")] public static extern IntPtr GetConsoleWindow();
}
'@
}

# Main window handle of Apple Music, or zero when it has none yet.
function Get-AppleMusicWindow {
    $process = Get-Process -Name 'AppleMusic' -ErrorAction SilentlyContinue |
        Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero } | Select-Object -First 1
    if ($process) { return $process.MainWindowHandle }
    return [IntPtr]::Zero
}

# Two captures per call: <Name>.png is the screen with Apple Music brought forward and maximized (consoles are
# minimized first), <Name>-fenetre.png is Apple Music's own window drawn by PrintWindow. Returns the screen capture.
function Save-Screenshot {
    param($Ctx, [string]$Name)
    try {
        Add-Type -AssemblyName System.Windows.Forms, System.Drawing
        $window = Get-AppleMusicWindow
        if ($window -ne [IntPtr]::Zero) {
            Get-Process -Name 'WindowsTerminal', 'conhost', 'OpenConsole', 'pwsh', 'powershell' -ErrorAction SilentlyContinue |
                Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero } |
                ForEach-Object { [AmkWindowCapture]::ShowWindow($_.MainWindowHandle, 6) | Out-Null }  # SW_MINIMIZE
            [AmkWindowCapture]::ShowWindow($window, 3) | Out-Null  # SW_MAXIMIZE
            [AmkWindowCapture]::SetForegroundWindow($window) | Out-Null
            Start-Sleep -Milliseconds 1500  # let the window repaint after maximize
        }

        $bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
        $bitmap = New-Object System.Drawing.Bitmap $bounds.Width, $bounds.Height
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        $graphics.CopyFromScreen($bounds.Location, [System.Drawing.Point]::Empty, $bounds.Size)
        $path = Join-Path $Ctx.Dir "$Name.png"
        $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
        $graphics.Dispose()
        $bitmap.Dispose()

        if ($window -ne [IntPtr]::Zero) {
            $rect = New-Object AmkWindowCapture+RECT
            if ([AmkWindowCapture]::GetWindowRect($window, [ref]$rect)) {
                $width = [Math]::Max(1, $rect.Right - $rect.Left)
                $height = [Math]::Max(1, $rect.Bottom - $rect.Top)
                $own = New-Object System.Drawing.Bitmap $width, $height
                $ownGraphics = [System.Drawing.Graphics]::FromImage($own)
                $hdc = $ownGraphics.GetHdc()
                [AmkWindowCapture]::PrintWindow($window, $hdc, 2) | Out-Null
                $ownGraphics.ReleaseHdc($hdc)
                $own.Save((Join-Path $Ctx.Dir "$Name-fenetre.png"), [System.Drawing.Imaging.ImageFormat]::Png)
                $ownGraphics.Dispose()
                $own.Dispose()
            }
        } else {
            Write-Host "  capture : aucune fenêtre d'Apple Music, seul l'écran est capturé"
        }
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
