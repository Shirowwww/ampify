# AMK.player sous Windows (win/player_bridge.*) : le noyau de banc doit dire dans AppleMusic.log s'il a activé le lecteur
# d'Apple (AMP.Services.Player), lire un premier état, et décrire la file d'attente (sonde en lecture seule). Sans compte
# Apple sur le runner, rien ne joue : on vérifie l'activation et la lecture d'état, pas la musique. Les lignes du
# lecteur sont copiées dans le journal du job, pour qu'on les lise sans télécharger les résultats.
# Build : banc\ (MinGW ou MSVC, AMK_BENCH=ON). Aucun réglage n'est changé : player.appleCommands reste éteint.

Register-Case -Id 'lecteur-amp' -Title 'Lecteur : AMP.Services.Player activé (ou repli SMTC) et premier état lu' -Requires 'apple-music' -Action {
    param($Ctx)

    $launcher = Join-Path $script:BenchKitRoot 'banc\amk_launcher.exe'
    if (-not (Test-Path -LiteralPath $launcher)) {
        Add-Check -Ctx $Ctx -Name 'build de banc présent' -Passed $false -Detail $launcher
        return
    }

    Reset-KitState
    $since = Get-Date
    $launcherProcess = Start-Launcher -LauncherPath $launcher

    $launched = Wait-Journal -FileName 'launcher.log' -Text 'Apple Music démarré' -TimeoutSeconds 120
    Add-Check -Ctx $Ctx -Name 'Apple Music démarré par le lanceur' -Passed $launched
    if (-not $launched) {
        Write-JournalTail -FileName 'launcher.log'
        Close-AppleMusic | Out-Null
        return
    }

    # Le noyau attend une fenêtre visible, puis essaie toutes les 5 s pendant 2 minutes : une des deux lignes finit
    # par arriver. « pas encore joignable » seule veut dire que les essais continuent.
    $verdictPattern = 'Lecteur : AMP\.Services\.Player (activé \([^\r\n]*|non activé \([^\r\n]*)'
    $gotVerdict = Wait-Until -TimeoutSeconds 200 -Condition {
        $null -ne (Find-JournalMatch -FileName 'AppleMusic.log' -Pattern $verdictPattern)
    }
    $verdict = Find-JournalMatch -FileName 'AppleMusic.log' -Pattern $verdictPattern
    Add-Check -Ctx $Ctx -Name 'AppleMusic.log : verdict sur AMP.Services.Player' -Passed $gotVerdict `
        -Detail $(if ($verdict) { $verdict.Value } else { 'ni « activé » ni « non activé » en 200 s' })

    $activated = [bool]($verdict -and $verdict.Value -like 'Lecteur : AMP.Services.Player activé*')
    Add-Info -Ctx $Ctx -Name 'lecteur d''Apple activé dans AppleMusic.exe' -Detail $(if ($activated) { 'oui' } else { 'non : repli SMTC puis touches' })
    if ($activated) {
        $first = Find-JournalMatch -FileName 'AppleMusic.log' -Pattern 'Lecteur : premier état lu par AMP\.Services\.Player : [^\r\n]*'
        Add-Check -Ctx $Ctx -Name 'premier état lu par le lecteur d''Apple' -Passed ($null -ne $first) `
            -Detail $(if ($first) { $first.Value } else { 'ligne absente' })
        $probe = Wait-Until -TimeoutSeconds 15 -Condition {
            $null -ne (Find-JournalMatch -FileName 'AppleMusic.log' -Pattern "File d'attente \(sonde, lecture seule\) : [^\r\n]*")
        }
        $probeLine = Find-JournalMatch -FileName 'AppleMusic.log' -Pattern "File d'attente \(sonde, lecture seule\) : [^\r\n]*"
        Add-Info -Ctx $Ctx -Name 'sonde de la file d''attente' -Detail $(if ($probeLine) { $probeLine.Value } else { 'ligne absente' })
    }

    # Une source d'état a été choisie (apple, smtc ou aucune) : la boucle du noyau lit bien le lecteur.
    $source = Wait-Until -TimeoutSeconds 30 -Condition {
        $null -ne (Find-JournalMatch -FileName 'AppleMusic.log' -Pattern 'Lecteur : état lu par [^\r\n]*')
    }
    Add-Check -Ctx $Ctx -Name 'AppleMusic.log : la boucle du noyau lit l''état du lecteur' -Passed $source

    $notResponding = @(Get-AppProcesses | Where-Object { -not $_.Responding }).Count
    Add-Check -Ctx $Ctx -Name 'fenêtre d''Apple Music qui répond' -Passed ($notResponding -eq 0)
    Save-Screenshot -Ctx $Ctx -Name 'lecteur-amp' | Out-Null

    # Toutes les lignes du lecteur, des deux processus, dans le journal du job.
    foreach ($file in @('AppleMusic.log', 'AMPLibraryAgent.log')) {
        $content = Read-Journal $file
        Write-Host "----- $file : lignes du lecteur"
        if ($content) {
            $content -split "`r?`n" | Where-Object { $_ -match 'Lecteur :|File d''attente' } | ForEach-Object { Write-Host "  $_" }
        } else {
            Write-Host '  (journal absent)'
        }
    }

    $closed = Close-AppleMusic
    $exited = $launcherProcess.WaitForExit(15000)
    Add-Check -Ctx $Ctx -Name 'lanceur : s''arrête quand Apple Music se ferme' -Passed ($closed -and $exited)

    $crashes = @(Get-AppCrashEvents -Since $since)
    Add-Check -Ctx $Ctx -Name 'aucun plantage d''Apple Music' -Passed ($crashes.Count -eq 0) `
        -Detail "$($crashes.Count) événement(s) : $(Format-CrashEvents -Events $crashes)"

    Copy-JournalToResults -Ctx $Ctx
}
