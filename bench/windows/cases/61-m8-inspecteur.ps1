# Étape 3 du test M8 : l'inspecteur écrit l'arbre de la fenêtre d'Apple Music dans journal\inspector.txt, puis remet
# theme.inspect à 0. Aucune page n'est ouverte par ce test : le relevé de la page Général reste à faire sur le PC.
# Job : MSVC (xaml-m8), build AMK_XAML=ON et AMK_BENCH=ON (sinon l'injection refuse la version du runner).
# Attend $script:BenchKitRoot\m8\ avec amk_launcher.exe et amk_kernel.dll côte à côte.

Register-Case -Id 'm8-inspecteur' -Title 'M8 : l''inspecteur écrit l''arbre de l''interface, puis se remet à zéro' -Requires 'apple-music' -Action {
    param($Ctx)

    $launcher = Join-Path $script:BenchKitRoot 'm8\amk_launcher.exe'
    if (-not (Test-Path -LiteralPath $launcher)) {
        Add-Check -Ctx $Ctx -Name 'build M8 présent' -Passed $false -Detail $launcher
        return
    }

    # Réglages : on garde les autres clés, on fixe celles du test. Le thème est éteint pour ne rien appliquer.
    Reset-KitState
    $settings = Join-Path $script:KitDir 'settings.txt'
    $lines = @()
    if (Test-Path -LiteralPath $settings) {
        $lines = @(Get-Content -LiteralPath $settings -Encoding UTF8 | Where-Object { $_ -notmatch '^\s*theme\.(active|inspect)\s*=' })
    }
    $lines += 'theme.active = none'
    $lines += 'theme.inspect = 1'
    Set-Content -LiteralPath $settings -Value $lines -Encoding UTF8

    $since = Get-Date
    $launcherProcess = Start-Launcher -LauncherPath $launcher

    $launched = Wait-Journal -FileName 'launcher.log' -Text 'Apple Music démarré' -TimeoutSeconds 120
    Add-Check -Ctx $Ctx -Name 'Apple Music démarré par le lanceur' -Passed $launched
    if (-not $launched) {
        Close-AppleMusic | Out-Null
        return
    }

    $hooked = Wait-Journal -FileName 'AppleMusic.log' -Text "Thèmes : crochet installé sur le thread d'interface" -TimeoutSeconds 90
    Add-Check -Ctx $Ctx -Name 'crochet installé sur le thread d''interface' -Passed $hooked

    $dumped = Wait-Journal -FileName 'AppleMusic.log' -Text 'Inspecteur : arbre écrit' -TimeoutSeconds 90
    Add-Check -Ctx $Ctx -Name 'journal : arbre écrit par l''inspecteur' -Passed $dumped

    $dumpPath = Join-Path $script:JournalDir 'inspector.txt'
    $dump = ''
    if (Test-Path -LiteralPath $dumpPath) {
        $dump = Get-Content -LiteralPath $dumpPath -Raw -Encoding UTF8
        Copy-Item -LiteralPath $dumpPath -Destination $Ctx.Dir -Force
    }
    $selectors = ([regex]::Matches($dump, 'sélecteur :')).Count
    Add-Check -Ctx $Ctx -Name 'inspector.txt : des éléments avec leur sélecteur' -Passed ($selectors -gt 0) `
        -Detail "$selectors élément(s)"
    Add-Info -Ctx $Ctx -Name 'page ouverte pendant le relevé' `
        -Detail 'aucune : la page Général n''est pas ouverte par ce test, son relevé reste à faire'

    # L'inspecteur remet theme.inspect à 0 une fois le relevé écrit.
    $now = @(Get-Content -LiteralPath $settings -Encoding UTF8)
    $reset = @($now | Where-Object { $_ -match '^\s*theme\.inspect\s*=\s*0\s*$' }).Count -gt 0
    Add-Check -Ctx $Ctx -Name 'settings.txt : theme.inspect remis à 0' -Passed $reset

    Save-Screenshot -Ctx $Ctx -Name 'inspecteur' | Out-Null

    $closed = Close-AppleMusic
    $exited = $launcherProcess.WaitForExit(15000)
    Add-Check -Ctx $Ctx -Name 'lanceur : s''arrête quand Apple Music se ferme' -Passed ($closed -and $exited)

    $crashes = @(Get-AppCrashEvents -Since $since)
    Add-Check -Ctx $Ctx -Name 'aucun plantage d''Apple Music' -Passed ($crashes.Count -eq 0) `
        -Detail "$($crashes.Count) événement(s) : $(Format-CrashEvents -Events $crashes)"

    Copy-JournalToResults -Ctx $Ctx
}
