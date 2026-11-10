# After two launches without an answer, the launcher starts Apple Music without the kernel, then resets the counter.

Register-Case -Id 'safe-mode' -Title 'Safe mode: after two failures, Apple Music starts without the kernel' -Requires 'apple-music', 'kit:bench' -Action {
    param($Ctx)

    $launcher = Join-Path $script:BenchKitRoot 'bench\ampify_launcher.exe'

    Reset-KitState
    Set-SafeModeCount -Count 2
    $since = Get-Date
    $launcherProcess = Start-Launcher -LauncherPath $launcher

    $safe = Wait-Journal -FileName 'launcher.log' -Text 'safe mode active: Apple Music starts without the kernel' -TimeoutSeconds 60
    Add-Check -Ctx $Ctx -Name 'launcher: safe mode turned on' -Passed $safe
    $launched = Wait-Journal -FileName 'launcher.log' -Text $script:LogPhrase.AppleMusicStarted -TimeoutSeconds 120
    Add-Check -Ctx $Ctx -Name 'Apple Music started by the launcher' -Passed $launched
    if (-not $launched) {
        Close-AppleMusic | Out-Null
        return
    }

    $noKernelLaunch = (-not (Test-JournalContains -FileName 'launcher.log' -Text 'kernel injected')) -and `
        (-not (Test-Path -LiteralPath (Join-Path $script:JournalDir 'AppleMusic.log')))
    Add-Check -Ctx $Ctx -Name 'no kernel injected in safe mode' -Passed $noKernelLaunch

    $kernel = Get-KernelState -ProcessName 'AppleMusic'
    Add-Check -Ctx $Ctx -Name 'ampify_kernel.dll absent from AppleMusic.exe' -Passed ($kernel.Loaded -eq 0) `
        -Detail "processes: $($kernel.Processes), with the kernel: $($kernel.Loaded)"

    # Without the kernel, the launcher sees the app healthy and resets the counter (launcher.log).
    $healthy = Wait-Journal -FileName 'launcher.log' -Text 'safe mode count reset' -TimeoutSeconds 60
    Add-Check -Ctx $Ctx -Name 'app healthy: counter reset' -Passed ($healthy -and ((Get-SafeModeCount) -eq 0)) `
        -Detail "counter: $(Get-SafeModeCount)"
    Save-Screenshot -Ctx $Ctx -Name 'apple-music-safe-mode' | Out-Null

    $closed = Close-AppleMusic
    $exited = $launcherProcess.WaitForExit(15000)
    Add-Check -Ctx $Ctx -Name 'launcher: stops when Apple Music closes' -Passed ($closed -and $exited)
}
