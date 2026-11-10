# Apple Music started normally, without the launcher, does not load the kernel.

Register-Case -Id 'normal-launch' -Title 'Apple Music started normally: no kernel' -Requires 'apple-music' -Action {
    param($Ctx)

    Reset-KitState
    $since = Get-Date
    Start-AppNormally

    $started = Wait-Until -TimeoutSeconds 90 -Condition { @(Get-AppProcesses).Count -gt 0 }
    Add-Check -Ctx $Ctx -Name 'Apple Music starts within 90 s' -Passed $started
    if (-not $started) { return }

    Start-Sleep -Seconds 30
    $kernel = Get-KernelState -ProcessName 'AppleMusic'
    Add-Check -Ctx $Ctx -Name 'Apple Music stays open after 30 s' -Passed (@(Get-AppProcesses).Count -gt 0)
    Add-Check -Ctx $Ctx -Name 'ampify_kernel.dll absent from AppleMusic.exe' -Passed ($kernel.Loaded -eq 0) `
        -Detail "processes: $($kernel.Processes), with the kernel: $($kernel.Loaded), unreadable: $($kernel.Unreadable)"
    Add-Check -Ctx $Ctx -Name 'no kernel log (AppleMusic.log absent)' `
        -Passed (-not (Test-Path -LiteralPath (Join-Path $script:JournalDir 'AppleMusic.log')))
    $notResponding = @(Get-AppProcesses | Where-Object { -not $_.Responding }).Count
    Add-Check -Ctx $Ctx -Name 'Apple Music window responds' -Passed ($notResponding -eq 0)
    Save-Screenshot -Ctx $Ctx -Name 'apple-music-without-kernel' | Out-Null

    $crashes = @(Get-AppCrashEvents -Since $since)
    Add-Check -Ctx $Ctx -Name 'no Apple Music crash' -Passed ($crashes.Count -eq 0) `
        -Detail "$($crashes.Count) event(s): $(Format-CrashEvents -Events $crashes)"

    $closed = Close-AppleMusic
    Add-Check -Ctx $Ctx -Name 'Apple Music closes on request' -Passed $closed
}
