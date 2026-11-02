# Steps 1 and 2 of the M4 test, without an account: the Atmos leveler turned on must neither stop Apple Music from
# starting, nor crash the app, nor hook CoreFP, CoreADI64 or FairPlay. The leveler start is noted only if the agent
# plays sound, which needs the Apple Music account: without playback, it is a finding. The ±1 LU check stays on a
# real PC (docs/development/testing.md).

Register-Case -Id 'atmos-leveler' -Title 'Atmos leveler on: Apple Music starts, no hook on FairPlay' -Requires 'apple-music' -Action {
    param($Ctx)

    $launcher = Join-Path $script:BenchKitRoot 'bench\ampify_launcher.exe'
    if (-not (Test-Path -LiteralPath $launcher)) {
        Add-Check -Ctx $Ctx -Name 'bench build present' -Passed $false -Detail $launcher
        return
    }

    # The case adds the enabling line to settings.txt, then puts the file back as it was at the end.
    $settings = Join-Path $env:USERPROFILE 'Ampify\settings.txt'
    $previous = if (Test-Path -LiteralPath $settings) { Get-Content -LiteralPath $settings -Raw -Encoding UTF8 } else { $null }
    $utf8 = New-Object System.Text.UTF8Encoding($false)

    try {
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $settings) | Out-Null
        $base = if ($previous) { $previous.TrimEnd("`r", "`n") + "`r`n" } else { '' }
        [System.IO.File]::WriteAllText($settings, $base + "extensions.atmos-leveler.enabled = true`r`n", $utf8)

        Reset-KitState
        $since = Get-Date
        $launcherProcess = Start-Launcher -LauncherPath $launcher

        $launched = Wait-Journal -FileName 'launcher.log' -Text 'Apple Music started' -TimeoutSeconds 120
        Add-Check -Ctx $Ctx -Name 'Apple Music started with the leveler on' -Passed $launched

        if ($launched) {
            $levelerStarted = Wait-Journal -FileName 'AMPLibraryAgent.log' -Text 'leveler started' -TimeoutSeconds 30
            if ($levelerStarted) {
                Add-Info -Ctx $Ctx -Name 'leveler started in AMPLibraryAgent.exe' -Detail 'an audio stream was opened'
            } else {
                Add-Info -Ctx $Ctx -Name 'leveler started' -Detail 'no playback on the runner: the leveler waits for an audio stream'
            }

            # No hook on the protected modules. Refusals ("refused") are normal: they show the guard.
            $agentLog = Read-Journal -FileName 'AMPLibraryAgent.log'
            $hooked = @()
            if ($agentLog) {
                foreach ($line in ($agentLog -split "`r?`n")) {
                    if ($line -notmatch 'placed in') { continue }
                    if ($line -match 'CoreFP|CoreADI64|FairPlay') { $hooked += $line }
                }
            }
            Add-Check -Ctx $Ctx -Name 'no hook on CoreFP, CoreADI64 or FairPlay' -Passed ($hooked.Count -eq 0) `
                -Detail ($hooked -join ' | ')
        }

        $crashes = @(Get-AppCrashEvents -Since $since)
        Add-Check -Ctx $Ctx -Name 'no Apple Music crash' -Passed ($crashes.Count -eq 0) `
            -Detail $(if ($crashes.Count) { Format-CrashEvents -Events $crashes } else { '' })

        Close-AppleMusic | Out-Null
        Add-Check -Ctx $Ctx -Name 'launcher stopped' -Passed ($launcherProcess.WaitForExit(15000))
    } finally {
        if ($null -eq $previous) {
            Remove-Item -LiteralPath $settings -ErrorAction SilentlyContinue
        } else {
            [System.IO.File]::WriteAllText($settings, $previous, $utf8)
        }
    }
}
