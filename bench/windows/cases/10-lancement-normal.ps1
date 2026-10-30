# Étape 3 du test M3 : Apple Music lancé normalement, sans le lanceur, ne charge pas le noyau.

Register-Case -Id 'lancement-normal' -Title 'Apple Music lancé normalement : aucun noyau' -Requires 'apple-music' -Action {
    param($Ctx)

    Reset-KitState
    $since = Get-Date
    Start-AppNormally

    $started = Wait-Until -TimeoutSeconds 90 -Condition { (Get-AppProcesses).Count -gt 0 }
    Add-Check -Ctx $Ctx -Name 'Apple Music démarre dans les 90 s' -Passed $started
    if (-not $started) { return }

    Start-Sleep -Seconds 30
    $kernel = Get-KernelState -ProcessName 'AppleMusic'
    Add-Check -Ctx $Ctx -Name 'Apple Music reste ouvert après 30 s' -Passed ((Get-AppProcesses).Count -gt 0)
    Add-Check -Ctx $Ctx -Name 'amk_kernel.dll absent d''AppleMusic.exe' -Passed ($kernel.Loaded -eq 0) `
        -Detail "processus : $($kernel.Processes), avec le noyau : $($kernel.Loaded), illisibles : $($kernel.Unreadable)"
    Add-Check -Ctx $Ctx -Name 'pas de journal du noyau (AppleMusic.log absent)' `
        -Passed (-not (Test-Path -LiteralPath (Join-Path $script:JournalDir 'AppleMusic.log')))
    $notResponding = @(Get-AppProcesses | Where-Object { -not $_.Responding }).Count
    Add-Check -Ctx $Ctx -Name 'fenêtre d''Apple Music qui répond' -Passed ($notResponding -eq 0)
    Save-Screenshot -Ctx $Ctx -Name 'apple-music-sans-noyau' | Out-Null

    $crashes = @(Get-AppCrashEvents -Since $since)
    Add-Check -Ctx $Ctx -Name 'aucun plantage d''Apple Music' -Passed ($crashes.Count -eq 0) `
        -Detail "$($crashes.Count) événement(s) : $(Format-CrashEvents -Events $crashes)"

    $closed = Close-AppleMusic
    Add-Check -Ctx $Ctx -Name 'Apple Music se ferme sur demande' -Passed $closed
}
