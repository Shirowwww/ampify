# Étapes 1 et 2 du test M3, sur le build de banc (accepte toute version) : le noyau se charge dans les deux processus,
# l'app reste saine, et le lancement suspendu est noté. Le verdict de version est celui du test 20.

Register-Case -Id 'injection-banc' -Title 'Build de banc : noyau dans AppleMusic.exe et AMPLibraryAgent.exe' -Requires 'apple-music' -Action {
    param($Ctx)

    $launcher = Join-Path $script:BenchKitRoot 'banc\amk_launcher.exe'
    if (-not (Test-Path -LiteralPath $launcher)) {
        Add-Check -Ctx $Ctx -Name 'build de banc présent' -Passed $false -Detail $launcher
        return
    }

    # Le banc injecte sur la version installée, même hors de la liste de diffusion : on l'écrit dans le rapport.
    Add-Info -Ctx $Ctx -Name 'version injectée (mode banc)' `
        -Detail "$($script:AppleMusicVersion) (liste de diffusion : $($script:KnownVersions -join ', '))"

    Reset-KitState
    $since = Get-Date
    $launcherProcess = Start-Launcher -LauncherPath $launcher

    $launched = Wait-Journal -FileName 'launcher.log' -Text 'Apple Music démarré' -TimeoutSeconds 120
    Add-Check -Ctx $Ctx -Name 'Apple Music démarré par le lanceur' -Passed $launched
    if (-not $launched) {
        Close-AppleMusic | Out-Null
        return
    }

    # Signature : la DLL du lecteur du paquet doit exporter les noms dont le noyau dépend. Sinon le lanceur refuse, même en mode banc.
    $signature = Find-JournalMatch -FileName 'launcher.log' -Pattern 'signature (?:non )?reconnue[^\r\n]*'
    Add-Check -Ctx $Ctx -Name 'signature : exports de AVFoundationCF.dll reconnus' `
        -Passed ($null -ne $signature -and $signature.Value -like 'signature reconnue*') `
        -Detail $(if ($signature) { $signature.Value } else { 'ligne absente du journal' })

    # Lancement suspendu : Windows garde l'app en pause pendant l'injection. Zéro thread repris veut dire que rien n'a été suspendu.
    $suspendRequested = Test-JournalContains -FileName 'launcher.log' -Text 'Lancement suspendu demandé à Windows'
    $suspendUnavailable = Test-JournalContains -FileName 'launcher.log' -Text 'Lancement suspendu indisponible'
    $resumed = Wait-Journal -FileName 'launcher.log' -Text "thread(s) repris après l'injection" -TimeoutSeconds 30
    $threads = Find-JournalMatch -FileName 'launcher.log' -Pattern '(\d+) thread\(s\) repris'
    if ($suspendRequested -and $resumed -and $threads) {
        $count = [int]$threads.Groups[1].Value
        Add-Check -Ctx $Ctx -Name 'lancement suspendu : des threads ont été suspendus puis repris' `
            -Passed ($count -gt 0) -Detail "$count thread(s) repris"
    } elseif ($suspendUnavailable) {
        Add-Info -Ctx $Ctx -Name 'lancement suspendu' -Detail 'indisponible sur ce Windows : injection après le démarrage (moins sûr)'
    } else {
        Add-Check -Ctx $Ctx -Name 'lancement suspendu : ligne du journal présente' -Passed $false `
            -Detail "ni 'demandé à Windows' ni 'indisponible', ou pas de ligne de reprise"
    }

    $kernelInApp = Wait-Journal -FileName 'launcher.log' -Text 'AppleMusic.exe : noyau chargé' -TimeoutSeconds 60
    Add-Check -Ctx $Ctx -Name 'lanceur : noyau chargé dans AppleMusic.exe' -Passed $kernelInApp
    $hostReady = Wait-Journal -FileName 'AppleMusic.log' -Text "Hôte d'extensions prêt" -TimeoutSeconds 60
    Add-Check -Ctx $Ctx -Name 'AppleMusic.log : hôte d''extensions prêt' -Passed $hostReady

    $agentPattern = 'AMPLibraryAgent\.exe \(\d+\) : noyau chargé'
    $agentInjected = Wait-Until -TimeoutSeconds 90 -Condition { $null -ne (Find-JournalMatch -FileName 'launcher.log' -Pattern $agentPattern) }
    Add-Check -Ctx $Ctx -Name 'lanceur : noyau chargé dans AMPLibraryAgent.exe' -Passed $agentInjected `
        -Detail $(if ($agentInjected) { 'agent vu et injecté' } else { 'AMPLibraryAgent.exe non vu dans les 90 s' })
    $agentLogged = Test-JournalContains -FileName 'AMPLibraryAgent.log' -Text 'Noyau chargé dans AMPLibraryAgent.exe'
    Add-Check -Ctx $Ctx -Name 'AMPLibraryAgent.log : noyau chargé' -Passed $agentLogged

    $healthy = Wait-Journal -FileName 'AppleMusic.log' -Text 'Application saine après 20 s' -TimeoutSeconds 60
    Add-Check -Ctx $Ctx -Name 'AppleMusic.log : application saine après 20 s' -Passed $healthy

    $kernelApp = Get-KernelState -ProcessName 'AppleMusic'
    Add-Check -Ctx $Ctx -Name 'amk_kernel.dll présent dans AppleMusic.exe' `
        -Passed ($kernelApp.Processes -gt 0 -and $kernelApp.Loaded -eq $kernelApp.Processes) `
        -Detail "processus : $($kernelApp.Processes), avec le noyau : $($kernelApp.Loaded), illisibles : $($kernelApp.Unreadable)"
    $kernelAgent = Get-KernelState -ProcessName 'AMPLibraryAgent'
    if ($kernelAgent.Processes -gt 0) {
        Add-Check -Ctx $Ctx -Name 'amk_kernel.dll présent dans AMPLibraryAgent.exe' `
            -Passed ($kernelAgent.Loaded -eq $kernelAgent.Processes) `
            -Detail "processus : $($kernelAgent.Processes), avec le noyau : $($kernelAgent.Loaded), illisibles : $($kernelAgent.Unreadable)"
    }

    $notResponding = @(Get-AppProcesses | Where-Object { -not $_.Responding }).Count
    Add-Check -Ctx $Ctx -Name 'fenêtre d''Apple Music qui répond' -Passed ($notResponding -eq 0)
    Save-Screenshot -Ctx $Ctx -Name 'apple-music-avec-noyau' | Out-Null

    $launcherLog = Read-Journal 'launcher.log'
    $unexpected = [bool]($launcherLog -and ($launcherLog.Contains('Erreur inattendue') -or $launcherLog.Contains('échec,')))
    Add-Check -Ctx $Ctx -Name 'lanceur : aucune erreur inattendue ni échec d''injection' -Passed (-not $unexpected)

    $closed = Close-AppleMusic
    $exited = $launcherProcess.WaitForExit(15000)
    Add-Check -Ctx $Ctx -Name 'lanceur : s''arrête quand Apple Music se ferme' -Passed ($closed -and $exited)
    Add-Check -Ctx $Ctx -Name 'journal : Apple Music fermé' `
        -Passed (Test-JournalContains -FileName 'launcher.log' -Text "Apple Music fermé : le lanceur s'arrête")

    $agentStillOpen = @(Get-Process -Name 'AMPLibraryAgent' -ErrorAction SilentlyContinue).Count -gt 0
    Add-Info -Ctx $Ctx -Name 'AMPLibraryAgent.exe après la fermeture d''Apple Music' `
        -Detail $(if ($agentStillOpen) { 'encore ouvert' } else { 'arrêté' })

    $crashes = @(Get-AppCrashEvents -Since $since)
    Add-Check -Ctx $Ctx -Name 'aucun plantage d''Apple Music' -Passed ($crashes.Count -eq 0) `
        -Detail "$($crashes.Count) événement(s) : $(Format-CrashEvents -Events $crashes)"
}
