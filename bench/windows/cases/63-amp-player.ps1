# Ampify.player on Windows (win/player_bridge.*): the bench kernel must say in AppleMusic.log whether it activated Apple's
# player (AMP.Services.Player), read a first state, and describe the play queue (read-only probe). Without an Apple
# account on the runner nothing plays: the case checks the activation and the state reading, not the music. The player
# lines are copied into the job log, so they can be read without downloading the results.
# Build: bench\ (MinGW or MSVC, AMPIFY_BENCH=ON). No setting is changed: player.appleCommands stays off.

Register-Case -Id 'amp-player' -Title 'Player: AMP.Services.Player activated (or SMTC fallback) and first state read' -Requires 'apple-music' -Action {
    param($Ctx)

    $launcher = Join-Path $script:BenchKitRoot 'bench\ampify_launcher.exe'
    if (-not (Test-Path -LiteralPath $launcher)) {
        Add-Check -Ctx $Ctx -Name 'bench build present' -Passed $false -Detail $launcher
        return
    }

    Reset-KitState
    $since = Get-Date
    $launcherProcess = Start-Launcher -LauncherPath $launcher

    $launched = Wait-Journal -FileName 'launcher.log' -Text 'Apple Music started' -TimeoutSeconds 120
    Add-Check -Ctx $Ctx -Name 'Apple Music started by the launcher' -Passed $launched
    if (-not $launched) {
        Write-JournalTail -FileName 'launcher.log'
        Close-AppleMusic | Out-Null
        return
    }

    # The kernel waits for a visible window, then tries every 5 s for 2 minutes: one of the two lines ends up
    # arriving. "not reachable yet" alone means the attempts go on.
    $verdictPattern = 'Player: AMP\.Services\.Player (activated \([^\r\n]*|not activated \([^\r\n]*)'
    $gotVerdict = Wait-Until -TimeoutSeconds 200 -Condition {
        $null -ne (Find-JournalMatch -FileName 'AppleMusic.log' -Pattern $verdictPattern)
    }
    $verdict = Find-JournalMatch -FileName 'AppleMusic.log' -Pattern $verdictPattern
    Add-Check -Ctx $Ctx -Name 'AppleMusic.log: verdict on AMP.Services.Player' -Passed $gotVerdict `
        -Detail $(if ($verdict) { $verdict.Value } else { 'neither "activated" nor "not activated" within 200 s' })

    $activated = [bool]($verdict -and $verdict.Value -like 'Player: AMP.Services.Player activated*')
    Add-Info -Ctx $Ctx -Name 'Apple player activated in AppleMusic.exe' -Detail $(if ($activated) { 'yes' } else { 'no: SMTC fallback, then media keys' })
    if ($activated) {
        $first = Find-JournalMatch -FileName 'AppleMusic.log' -Pattern 'Player: first state read by AMP\.Services\.Player: [^\r\n]*'
        Add-Check -Ctx $Ctx -Name 'first state read by Apple''s player' -Passed ($null -ne $first) `
            -Detail $(if ($first) { $first.Value } else { 'line missing' })
        $probe = Wait-Until -TimeoutSeconds 15 -Condition {
            $null -ne (Find-JournalMatch -FileName 'AppleMusic.log' -Pattern "Play queue \(probe, read only\): [^\r\n]*")
        }
        $probeLine = Find-JournalMatch -FileName 'AppleMusic.log' -Pattern "Play queue \(probe, read only\): [^\r\n]*"
        Add-Info -Ctx $Ctx -Name 'play queue probe' -Detail $(if ($probeLine) { $probeLine.Value } else { 'line missing' })
    }

    # A state source was chosen (apple, smtc or none): the kernel loop does read the player.
    $source = Wait-Until -TimeoutSeconds 30 -Condition {
        $null -ne (Find-JournalMatch -FileName 'AppleMusic.log' -Pattern 'Player: state read from [^\r\n]*')
    }
    Add-Check -Ctx $Ctx -Name 'AppleMusic.log: the kernel loop reads the player state' -Passed $source

    $notResponding = @(Get-AppProcesses | Where-Object { -not $_.Responding }).Count
    Add-Check -Ctx $Ctx -Name 'Apple Music window responds' -Passed ($notResponding -eq 0)
    Save-Screenshot -Ctx $Ctx -Name 'amp-player' | Out-Null

    # All the player lines, of both processes, in the job log.
    foreach ($file in @('AppleMusic.log', 'AMPLibraryAgent.log')) {
        $content = Read-Journal $file
        Write-Host "----- ${file}: player lines"
        if ($content) {
            $content -split "`r?`n" | Where-Object { $_ -match 'Player:|Play queue' } | ForEach-Object { Write-Host "  $_" }
        } else {
            Write-Host '  (log missing)'
        }
    }

    $closed = Close-AppleMusic
    $exited = $launcherProcess.WaitForExit(15000)
    Add-Check -Ctx $Ctx -Name 'launcher: stops when Apple Music closes' -Passed ($closed -and $exited)

    $crashes = @(Get-AppCrashEvents -Since $since)
    Add-Check -Ctx $Ctx -Name 'no Apple Music crash' -Passed ($crashes.Count -eq 0) `
        -Detail "$($crashes.Count) event(s): $(Format-CrashEvents -Events $crashes)"

    Copy-JournalToResults -Ctx $Ctx
}
