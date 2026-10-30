# M6 : installation et désinstallation du kit, sans Apple Music (le kit s'installe quand même). Le désinstallateur
# se supprime lui-même : c'est le chemin réel sur Windows, qui ne se teste pas sous Wine.

# Lance un exécutable du kit et renvoie son code de sortie. Aussi utilisé par 60-kit-de-test.ps1.
function Invoke-KitProgram {
    param([string]$Path, [string[]]$Arguments)
    $process = Start-Process -FilePath $Path -ArgumentList $Arguments -PassThru -Wait -WindowStyle Hidden
    return $process.ExitCode
}

Register-Case -Id 'installateur' -Title 'Installation, désinstallation gardée et purgée, refus si Apple Music est ouvert' -Action {
    param($Ctx)

    $setup = Join-Path $script:KitRoot 'diffusion\amk_setup.exe'
    if (-not (Test-Path -LiteralPath $setup)) {
        Add-Check -Ctx $Ctx -Name 'amk_setup.exe présent dans diffusion' -Passed $false -Detail $setup
        return
    }

    $installDir = Join-Path $env:LOCALAPPDATA 'Programs\AppleMusicModKit'
    $installedSetup = Join-Path $installDir 'amk_setup.exe'
    $shortcut = Join-Path ([Environment]::GetFolderPath('Programs')) 'Apple Music avec mods.lnk'
    $uninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\AppleMusicModKit'
    $launcherFile = Join-Path $installDir 'amk_launcher.exe'

    # Un état propre : on désinstalle ce qui resterait d'un passage précédent.
    Reset-KitState
    Invoke-KitProgram -Path $setup -Arguments @('/uninstall', '/silent', '/purgedata') | Out-Null

    # 1. Installation silencieuse.
    $code = Invoke-KitProgram -Path $setup -Arguments @('/silent')
    Add-Check -Ctx $Ctx -Name 'installation : code 0' -Passed ($code -eq 0) -Detail "code $code"
    $files = @('amk_launcher.exe', 'amk_kernel.dll', 'amk_setup.exe') | Where-Object { -not (Test-Path -LiteralPath (Join-Path $installDir $_)) }
    Add-Check -Ctx $Ctx -Name 'les trois fichiers sont dans le dossier d''installation' -Passed (@($files).Count -eq 0) `
        -Detail "manquants : $(@($files) -join ', ')"
    Add-Check -Ctx $Ctx -Name 'raccourci du menu Démarrer créé' -Passed (Test-Path -LiteralPath $shortcut)
    $entry = Get-ItemProperty -Path $uninstallKey -ErrorAction SilentlyContinue
    Add-Check -Ctx $Ctx -Name 'entrée de désinstallation (Paramètres > Applications)' `
        -Passed ($entry -and $entry.DisplayName -eq 'Apple Music avec Mod Kit') -Detail "$($entry.DisplayVersion)"

    # 2. Refus : un processus nommé AppleMusic.exe est ouvert, la désinstallation doit s'arrêter sans rien supprimer.
    $fakeDir = Join-Path $Ctx.Dir 'faux-apple-music'
    New-Item -ItemType Directory -Force -Path $fakeDir | Out-Null
    $fake = Join-Path $fakeDir 'AppleMusic.exe'
    Copy-Item -LiteralPath (Join-Path $env:SystemRoot 'System32\cmd.exe') -Destination $fake -Force
    $sleeper = Start-Process -FilePath $fake -ArgumentList '/c', 'ping -n 300 127.0.0.1 > nul' -PassThru -WindowStyle Hidden
    Start-Sleep -Seconds 2
    $refused = Invoke-KitProgram -Path $installedSetup -Arguments @('/uninstall', '/silent', '/keepdata')
    Add-Check -Ctx $Ctx -Name 'désinstallation refusée quand Apple Music est ouvert (code 3)' -Passed ($refused -eq 3) -Detail "code $refused"
    Add-Check -Ctx $Ctx -Name 'rien n''est supprimé après le refus' -Passed (Test-Path -LiteralPath $launcherFile)
    Stop-Process -Id $sleeper.Id -Force -ErrorAction SilentlyContinue
    Get-Process -Name 'AppleMusic', 'PING' -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2

    # 3. Désinstallation en gardant les données : tout part, sauf le dossier de données.
    $code = Invoke-KitProgram -Path $installedSetup -Arguments @('/uninstall', '/silent', '/keepdata')
    Add-Check -Ctx $Ctx -Name 'désinstallation gardée : code 0' -Passed ($code -eq 0) -Detail "code $code"
    $gone = Wait-Until -TimeoutSeconds 30 -Condition { -not (Test-Path -LiteralPath $installDir) }
    Add-Check -Ctx $Ctx -Name 'dossier d''installation supprimé, désinstallateur compris' -Passed $gone
    Add-Check -Ctx $Ctx -Name 'raccourci supprimé' -Passed (-not (Test-Path -LiteralPath $shortcut))
    Add-Check -Ctx $Ctx -Name 'entrée de désinstallation supprimée' -Passed (-not (Test-Path -LiteralPath $uninstallKey))
    Add-Check -Ctx $Ctx -Name 'données gardées (dossier AppleMusicModKit)' -Passed (Test-Path -LiteralPath $script:KitDir)
    Copy-JournalToResults -Ctx $Ctx

    # 4. Réinstallation, puis désinstallation en supprimant aussi les données.
    Invoke-KitProgram -Path $setup -Arguments @('/silent') | Out-Null
    $code = Invoke-KitProgram -Path $installedSetup -Arguments @('/uninstall', '/silent', '/purgedata')
    Add-Check -Ctx $Ctx -Name 'désinstallation purgée : code 0' -Passed ($code -eq 0) -Detail "code $code"
    $gone = Wait-Until -TimeoutSeconds 30 -Condition { -not (Test-Path -LiteralPath $installDir) }
    Add-Check -Ctx $Ctx -Name 'purge : dossier d''installation supprimé' -Passed $gone
    Add-Check -Ctx $Ctx -Name 'purge : dossier de données supprimé' -Passed (-not (Test-Path -LiteralPath $script:KitDir))
    Add-Check -Ctx $Ctx -Name 'purge : raccourci et entrée de désinstallation supprimés' `
        -Passed ((-not (Test-Path -LiteralPath $shortcut)) -and (-not (Test-Path -LiteralPath $uninstallKey)))
}
