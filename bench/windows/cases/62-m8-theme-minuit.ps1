# Étapes 4 et 5 du test M8 : le thème minuit s'applique dans Apple Music, puis se retire sans redémarrage quand
# theme.active repasse à none. Le changement de réglage se fait dans settings.txt, pas dans la page Réglages : la
# section « Thème » de la page Général reste à vérifier sur le PC.
# Job : MSVC (xaml-m8), build AMK_XAML=ON et AMK_BENCH=ON (sinon l'injection refuse la version du runner).
# Attend $script:BenchKitRoot\m8\ avec amk_launcher.exe, amk_kernel.dll, et themes\examples\minuit.

Register-Case -Id 'm8-theme-minuit' -Title 'M8 : le thème minuit s''applique, puis se retire sans redémarrage' -Requires 'apple-music' -Action {
    param($Ctx)

    $launcher = Join-Path $script:BenchKitRoot 'm8\amk_launcher.exe'
    $example = Join-Path $script:BenchKitRoot 'm8\themes\examples\minuit'
    if (-not (Test-Path -LiteralPath $launcher) -or -not (Test-Path -LiteralPath $example)) {
        Add-Skip -Ctx $Ctx -Name 'build M8 et thème minuit présents' -Reason ("pas de build M8 dans ce banc : " + "$launcher / $example")
        return
    }

    # Le thème minuit est copié dans le dossier de réglages de l'utilisateur, comme à l'étape 2 du test M8.
    $themes = Join-Path $script:KitDir 'themes'
    New-Item -ItemType Directory -Force -Path $themes | Out-Null
    $target = Join-Path $themes 'minuit'
    Remove-Item -LiteralPath $target -Recurse -Force -ErrorAction SilentlyContinue
    Copy-Item -LiteralPath $example -Destination $target -Recurse -Force
    # Verre, la refonte complète, est essayé dans le même lancement, après minuit.
    $verreExample = Join-Path $script:BenchKitRoot 'm8\themes\examples\verre'
    if (Test-Path -LiteralPath $verreExample) {
        Remove-Item -LiteralPath (Join-Path $themes 'verre') -Recurse -Force -ErrorAction SilentlyContinue
        Copy-Item -LiteralPath $verreExample -Destination (Join-Path $themes 'verre') -Recurse -Force
    }

    Reset-KitState
    $settings = Join-Path $script:KitDir 'settings.txt'
    $lines = @()
    if (Test-Path -LiteralPath $settings) {
        $lines = @(Get-Content -LiteralPath $settings -Encoding UTF8 | Where-Object { $_ -notmatch '^\s*theme\.(active|scheme|inspect)\s*=' })
    }
    $lines += 'theme.active = minuit'
    $lines += 'theme.scheme = Dark'
    $lines += 'theme.inspect = 0'
    Set-Content -LiteralPath $settings -Value $lines -Encoding UTF8

    $since = Get-Date
    $launcherProcess = Start-Launcher -LauncherPath $launcher

    $launched = Wait-Journal -FileName 'launcher.log' -Text 'Apple Music démarré' -TimeoutSeconds 120
    Add-Check -Ctx $Ctx -Name 'Apple Music démarré par le lanceur' -Passed $launched
    if (-not $launched) {
        Close-AppleMusic | Out-Null
        return
    }
    # Runner neuf : la licence MPEG LA s'ouvre avant la fenêtre principale, que le noyau attend.
    $firstRun = Close-AppleMusicFirstRun
    Add-Info -Ctx $Ctx -Name 'premier lancement d''Apple Music' -Detail "$firstRun écran(s) fermé(s) (licence, accueil)"

    $hooked = Wait-Journal -FileName 'AppleMusic.log' -Text "Thèmes : fil d'interface prêt (DispatcherQueue de la fenêtre principale)" -TimeoutSeconds 90
    Add-Check -Ctx $Ctx -Name 'fil d''interface prêt (DispatcherQueue)' -Passed $hooked

    # « Thème « minuit » : N valeur(s) appliquée(s), … » : N est le nombre de valeurs écrites.
    $applyPattern = 'Thème « minuit » : (\d+) valeur\(s\) appliquée\(s\)'
    $applied = Wait-Until -TimeoutSeconds 90 -Condition { $null -ne (Find-JournalMatch -FileName 'AppleMusic.log' -Pattern $applyPattern) }
    $count = 0
    if ($applied) {
        $match = Find-JournalMatch -FileName 'AppleMusic.log' -Pattern $applyPattern
        $count = [int]$match.Groups[1].Value
    }
    Add-Check -Ctx $Ctx -Name 'thème minuit : des valeurs appliquées' -Passed ($applied -and $count -gt 0) `
        -Detail "$count valeur(s)"

    $log = Read-Journal 'AppleMusic.log'
    $refusals = @(([string]$log) -split "`r?`n" | Where-Object { $_.StartsWith('Refusé : ') })
    Add-Info -Ctx $Ctx -Name 'lignes « Refusé » du journal' -Detail "$($refusals.Count) : $(($refusals | Select-Object -First 3) -join ' | ')"

    Start-Sleep -Seconds 4  # one more tick: the colours reach what is already on screen
    Save-Screenshot -Ctx $Ctx -Name 'minuit-applique' | Out-Null

    if (Test-Path -LiteralPath (Join-Path $themes 'verre')) {
        $switch = @(Get-Content -LiteralPath $settings -Encoding UTF8 | Where-Object { $_ -notmatch '^\s*theme\.(active|scheme)\s*=' })
        $switch += 'theme.active = verre'
        $switch += 'theme.scheme = Dark'
        Set-Content -LiteralPath $settings -Value $switch -Encoding UTF8
        $verrePattern = 'Thème « verre » : (\d+) valeur\(s\) appliquée\(s\)'
        $verreApplied = Wait-Until -TimeoutSeconds 60 -Condition { $null -ne (Find-JournalMatch -FileName 'AppleMusic.log' -Pattern $verrePattern) }
        $verreCount = 0
        if ($verreApplied) { $verreCount = [int](Find-JournalMatch -FileName 'AppleMusic.log' -Pattern $verrePattern).Groups[1].Value }
        Add-Check -Ctx $Ctx -Name 'thème verre (refonte) : des valeurs appliquées' -Passed ($verreApplied -and $verreCount -gt 0) `
            -Detail "$verreCount valeur(s)"
        Start-Sleep -Seconds 4
        Save-Screenshot -Ctx $Ctx -Name 'verre-applique' | Out-Null
    }

    # Retrait : on repasse à none dans settings.txt, sans redémarrer Apple Music.
    $kept = @(Get-Content -LiteralPath $settings -Encoding UTF8 | Where-Object { $_ -notmatch '^\s*theme\.active\s*=' })
    $kept += 'theme.active = none'
    Set-Content -LiteralPath $settings -Value $kept -Encoding UTF8

    $restored = Wait-Journal -FileName 'AppleMusic.log' -Text 'Thème retiré : ' -TimeoutSeconds 60
    Add-Check -Ctx $Ctx -Name 'retrait sans redémarrage : « Thème retiré » dans le journal' -Passed $restored

    Save-Screenshot -Ctx $Ctx -Name 'minuit-retire' | Out-Null

    $closed = Close-AppleMusic
    $exited = $launcherProcess.WaitForExit(15000)
    Add-Check -Ctx $Ctx -Name 'lanceur : s''arrête quand Apple Music se ferme' -Passed ($closed -and $exited)

    $crashes = @(Get-AppCrashEvents -Since $since)
    Add-Check -Ctx $Ctx -Name 'aucun plantage d''Apple Music' -Passed ($crashes.Count -eq 0) `
        -Detail "$($crashes.Count) événement(s) : $(Format-CrashEvents -Events $crashes)"

    Copy-JournalToResults -Ctx $Ctx
}
