# Build de diffusion (celui qui sera livré) : le noyau n'est injecté que sur une version testée, et l'app reste saine.
# Le verdict attendu dépend de la version installée sur le runner, lue dans core/version_gate.hpp.

Register-Case -Id 'version-diffusion' -Title 'Build de diffusion : injection seulement sur une version testée' -Requires 'apple-music' -Action {
    param($Ctx)

    $launcher = Join-Path $script:BenchKitRoot 'diffusion\amk_launcher.exe'
    if (-not (Test-Path -LiteralPath $launcher)) {
        Add-Check -Ctx $Ctx -Name 'build de diffusion présent' -Passed $false -Detail $launcher
        return
    }

    $version = $script:AppleMusicVersion
    $tested = $script:KnownVersions -contains $version
    Add-Info -Ctx $Ctx -Name 'version installée' -Detail "$version (testée : $tested)"

    Reset-KitState
    $since = Get-Date
    $launcherProcess = Start-Launcher -LauncherPath $launcher

    $verdict = Wait-Journal -FileName 'launcher.log' -Text "version $version" -TimeoutSeconds 60
    Add-Check -Ctx $Ctx -Name 'lanceur : version lue dans le paquet' -Passed $verdict
    if (-not $verdict) { return }

    if ($tested) {
        Add-Check -Ctx $Ctx -Name 'lanceur : noyau injecté, version testée' `
            -Passed (Test-JournalContains -FileName 'launcher.log' -Text "version $version testée : noyau injecté")

        $launched = Wait-Journal -FileName 'launcher.log' -Text 'Apple Music démarré' -TimeoutSeconds 120
        Add-Check -Ctx $Ctx -Name 'Apple Music démarré par le lanceur' -Passed $launched
        $kernelLoaded = Wait-Journal -FileName 'AppleMusic.log' -Text 'Noyau chargé dans AppleMusic.exe' -TimeoutSeconds 60
        Add-Check -Ctx $Ctx -Name 'AppleMusic.log : noyau chargé dans AppleMusic.exe' -Passed $kernelLoaded
    } else {
        Add-Check -Ctx $Ctx -Name 'lanceur : version refusée, Apple Music démarre sans noyau' `
            -Passed (Test-JournalContains -FileName 'launcher.log' -Text "version $version non testée : Apple Music démarre sans noyau")

        $launched = Wait-Journal -FileName 'launcher.log' -Text 'Apple Music démarré' -TimeoutSeconds 120
        Add-Check -Ctx $Ctx -Name 'Apple Music démarré par le lanceur' -Passed $launched
        Start-Sleep -Seconds 30
        Add-Check -Ctx $Ctx -Name 'pas de journal du noyau (AppleMusic.log absent)' `
            -Passed (-not (Test-Path -LiteralPath (Join-Path $script:JournalDir 'AppleMusic.log')))
    }

    if ($launched) {
        Start-Sleep -Seconds 20
        $kernel = Get-KernelState -ProcessName 'AppleMusic'
        if ($tested) {
            Add-Check -Ctx $Ctx -Name 'amk_kernel.dll présent dans AppleMusic.exe' `
                -Passed ($kernel.Processes -gt 0 -and $kernel.Loaded -eq $kernel.Processes) `
                -Detail "processus : $($kernel.Processes), avec le noyau : $($kernel.Loaded), illisibles : $($kernel.Unreadable)"
        } else {
            Add-Check -Ctx $Ctx -Name 'amk_kernel.dll absent d''AppleMusic.exe' -Passed ($kernel.Loaded -eq 0) `
                -Detail "processus : $($kernel.Processes), avec le noyau : $($kernel.Loaded), illisibles : $($kernel.Unreadable)"
        }
        Save-Screenshot -Ctx $Ctx -Name 'apple-music-diffusion' | Out-Null
    }

    $closed = Close-AppleMusic
    $exited = $launcherProcess.WaitForExit(15000)
    Add-Check -Ctx $Ctx -Name 'lanceur : s''arrête quand Apple Music se ferme' -Passed ($closed -and $exited)
    $crashes = @(Get-AppCrashEvents -Since $since)
    Add-Check -Ctx $Ctx -Name 'aucun plantage d''Apple Music' -Passed ($crashes.Count -eq 0) `
        -Detail "$($crashes.Count) événement(s) : $(Format-CrashEvents -Events $crashes)"
}
