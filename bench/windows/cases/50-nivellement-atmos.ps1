# Étape 1 et 2 du test M4, sans compte : le nivellement Atmos activé ne doit ni empêcher Apple Music de démarrer, ni
# planter l'app, ni poser un hook sur CoreFP, CoreADI64 ou FairPlay. Le démarrage du nivellement n'est noté que si
# l'agent joue du son, ce qui demande le compte Apple Music : sans lecture, c'est un constat. Le ±1 LU reste sur le PC
# de l'utilisateur (docs/tests/M4-atmos.md).

Register-Case -Id 'nivellement-atmos' -Title 'Nivellement Atmos activé : Apple Music démarre, aucun hook sur FairPlay' -Requires 'apple-music' -Action {
    param($Ctx)

    $launcher = Join-Path $script:BenchKitRoot 'banc\amk_launcher.exe'
    if (-not (Test-Path -LiteralPath $launcher)) {
        Add-Check -Ctx $Ctx -Name 'build de banc présent' -Passed $false -Detail $launcher
        return
    }

    # Le test ajoute la ligne d'activation à settings.txt, puis remet le fichier tel qu'il était à la fin.
    $settings = Join-Path $env:USERPROFILE 'AppleMusicModKit\settings.txt'
    $previous = if (Test-Path -LiteralPath $settings) { Get-Content -LiteralPath $settings -Raw -Encoding UTF8 } else { $null }
    $utf8 = New-Object System.Text.UTF8Encoding($false)

    try {
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $settings) | Out-Null
        $base = if ($previous) { $previous.TrimEnd("`r", "`n") + "`r`n" } else { '' }
        [System.IO.File]::WriteAllText($settings, $base + "extensions.atmos-leveler.enabled = true`r`n", $utf8)

        Reset-KitState
        $since = Get-Date
        $launcherProcess = Start-Launcher -LauncherPath $launcher

        $launched = Wait-Journal -FileName 'launcher.log' -Text 'Apple Music démarré' -TimeoutSeconds 120
        Add-Check -Ctx $Ctx -Name 'Apple Music démarré avec le nivellement activé' -Passed $launched

        if ($launched) {
            $levelerStarted = Wait-Journal -FileName 'AMPLibraryAgent.log' -Text 'nivellement démarré' -TimeoutSeconds 30
            if ($levelerStarted) {
                Add-Info -Ctx $Ctx -Name 'nivellement démarré dans AMPLibraryAgent.exe' -Detail 'un flux audio a été ouvert'
            } else {
                Add-Info -Ctx $Ctx -Name 'nivellement démarré' -Detail 'pas de lecture sur le runner : le nivellement attend un flux audio'
            }

            # Aucun hook posé sur les modules protégés. Les refus (« refusé ») sont normaux : ils montrent le garde-fou.
            $agentLog = Read-Journal -FileName 'AMPLibraryAgent.log'
            $hooked = @()
            if ($agentLog) {
                foreach ($line in ($agentLog -split "`r?`n")) {
                    if ($line -notmatch 'posé dans') { continue }
                    if ($line -match 'CoreFP|CoreADI64|FairPlay') { $hooked += $line }
                }
            }
            Add-Check -Ctx $Ctx -Name 'aucun hook posé sur CoreFP, CoreADI64 ni FairPlay' -Passed ($hooked.Count -eq 0) `
                -Detail ($hooked -join ' | ')
        }

        $crashes = @(Get-AppCrashEvents -Since $since)
        Add-Check -Ctx $Ctx -Name 'aucun plantage d''Apple Music' -Passed ($crashes.Count -eq 0) `
            -Detail $(if ($crashes.Count) { Format-CrashEvents -Events $crashes } else { '' })

        Close-AppleMusic | Out-Null
        Add-Check -Ctx $Ctx -Name 'lanceur arrêté' -Passed ($launcherProcess.WaitForExit(15000))
    } finally {
        if ($null -eq $previous) {
            Remove-Item -LiteralPath $settings -ErrorAction SilentlyContinue
        } else {
            [System.IO.File]::WriteAllText($settings, $previous, $utf8)
        }
    }
}
