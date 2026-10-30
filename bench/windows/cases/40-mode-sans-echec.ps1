# Étape 4 du test M3 : après deux lancements sans réponse, le lanceur démarre Apple Music sans noyau, puis remet le compteur à zéro.

Register-Case -Id 'mode-sans-echec' -Title 'Mode sans échec : après deux échecs, Apple Music démarre sans noyau' -Requires 'apple-music' -Action {
    param($Ctx)

    $launcher = Join-Path $script:BenchKitRoot 'banc\amk_launcher.exe'
    if (-not (Test-Path -LiteralPath $launcher)) {
        Add-Check -Ctx $Ctx -Name 'build de banc présent' -Passed $false -Detail $launcher
        return
    }

    Reset-KitState
    Set-SafeModeCount -Count 2
    $since = Get-Date
    $launcherProcess = Start-Launcher -LauncherPath $launcher

    $safe = Wait-Journal -FileName 'launcher.log' -Text 'mode sans échec actif : Apple Music démarre sans noyau' -TimeoutSeconds 60
    Add-Check -Ctx $Ctx -Name 'lanceur : mode sans échec activé' -Passed $safe
    $launched = Wait-Journal -FileName 'launcher.log' -Text 'Apple Music démarré' -TimeoutSeconds 120
    Add-Check -Ctx $Ctx -Name 'Apple Music démarré par le lanceur' -Passed $launched
    if (-not $launched) {
        Close-AppleMusic | Out-Null
        return
    }

    $noKernelLaunch = (-not (Test-JournalContains -FileName 'launcher.log' -Text 'noyau injecté')) -and `
        (-not (Test-Path -LiteralPath (Join-Path $script:JournalDir 'AppleMusic.log')))
    Add-Check -Ctx $Ctx -Name 'aucun noyau injecté en mode sans échec' -Passed $noKernelLaunch

    $kernel = Get-KernelState -ProcessName 'AppleMusic'
    Add-Check -Ctx $Ctx -Name 'amk_kernel.dll absent d''AppleMusic.exe' -Passed ($kernel.Loaded -eq 0) `
        -Detail "processus : $($kernel.Processes), avec le noyau : $($kernel.Loaded)"

    $healthy = Wait-Journal -FileName 'launcher.log' -Text 'Application saine sans noyau après 20 s' -TimeoutSeconds 60
    Add-Check -Ctx $Ctx -Name 'application saine : compteur remis à zéro' -Passed ($healthy -and ((Get-SafeModeCount) -eq 0)) `
        -Detail "compteur : $(Get-SafeModeCount)"
    Save-Screenshot -Ctx $Ctx -Name 'apple-music-mode-sans-echec' | Out-Null

    $closed = Close-AppleMusic
    $exited = $launcherProcess.WaitForExit(15000)
    Add-Check -Ctx $Ctx -Name 'lanceur : s''arrête quand Apple Music se ferme' -Passed ($closed -and $exited)
}
