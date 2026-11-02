# The way people use Ampify: the background launcher runs (as after the install, from Windows sign-in), and Apple Music
# is opened from its own icon. The kernel must load into it without any launcher started by hand, then the launcher
# waits for the next start and stops when asked (ampify restore, the uninstaller).

Register-Case -Id 'background-launch' -Title 'Background launcher: Apple Music opened from its icon gets the kernel' -Requires 'apple-music' -Action {
    param($Ctx)

    $launcher = Join-Path $script:BenchKitRoot 'bench\ampify_launcher.exe'
    if (-not (Test-Path -LiteralPath $launcher)) {
        Add-Check -Ctx $Ctx -Name 'bench build present' -Passed $false -Detail $launcher
        return
    }

    Reset-KitState
    $since = Get-Date
    $background = Start-Process -FilePath $launcher -ArgumentList '--background' -WorkingDirectory (Split-Path -Parent $launcher) -PassThru
    $ready = Wait-Journal -FileName 'background.log' -Text 'Background launcher started' -TimeoutSeconds 30
    Add-Check -Ctx $Ctx -Name 'background launcher started' -Passed $ready

    # A second one must leave at once: one per session.
    $second = Start-Process -FilePath $launcher -ArgumentList '--background' -PassThru -WindowStyle Hidden
    Add-Check -Ctx $Ctx -Name 'a second background launcher leaves at once' -Passed ($second.WaitForExit(10000))

    Start-AppNormally
    $seen = Wait-Journal -FileName 'background.log' -Text 'Apple Music started' -TimeoutSeconds 120
    Add-Check -Ctx $Ctx -Name 'the background launcher saw Apple Music start' -Passed $seen
    $appPattern = 'AppleMusic\.exe \(\d+\): kernel loaded'
    $loaded = Wait-Until -TimeoutSeconds 60 -Condition { $null -ne (Find-JournalMatch -FileName 'background.log' -Pattern $appPattern) }
    Add-Check -Ctx $Ctx -Name 'kernel loaded in AppleMusic.exe opened from its icon' -Passed $loaded
    $hostReady = Wait-Journal -FileName 'AppleMusic.log' -Text 'Extension host ready' -TimeoutSeconds 60
    Add-Check -Ctx $Ctx -Name 'AppleMusic.log: extension host ready' -Passed $hostReady
    $kernelApp = Get-KernelState -ProcessName 'AppleMusic'
    Add-Check -Ctx $Ctx -Name 'ampify_kernel.dll present in AppleMusic.exe' `
        -Passed ($kernelApp.Processes -gt 0 -and $kernelApp.Loaded -eq $kernelApp.Processes) `
        -Detail "processes: $($kernelApp.Processes), with the kernel: $($kernelApp.Loaded)"
    Save-Screenshot -Ctx $Ctx -Name 'apple-music-from-its-icon' | Out-Null

    # Closed, then opened again: the same launcher follows the second session.
    Close-AppleMusic | Out-Null
    $waiting = Wait-Journal -FileName 'background.log' -Text 'Waiting for Apple Music to start again' -TimeoutSeconds 60
    Add-Check -Ctx $Ctx -Name 'after Apple Music closes, the launcher waits for the next start' -Passed $waiting
    Add-Check -Ctx $Ctx -Name 'the background launcher is still running' -Passed (-not $background.HasExited)
    Start-AppNormally
    # Loaded again after the second "Apple Music started": a hand-over during the first session does not count.
    $again = Wait-Until -TimeoutSeconds 120 -Condition {
        $log = [string](Read-Journal 'background.log')
        $starts = [regex]::Matches($log, 'Apple Music started \(process \d+\)')
        $starts.Count -ge 2 -and [regex]::IsMatch($log.Substring($starts[1].Index), $appPattern)
    }
    Add-Check -Ctx $Ctx -Name 'second start: kernel loaded again' -Passed $again
    Close-AppleMusic | Out-Null

    # Asked to stop, the way ampify restore and the uninstaller do.
    $stop = [System.Threading.EventWaitHandle]::OpenExisting('Local\AmpifyBackgroundStop')
    $stop.Set() | Out-Null
    $stop.Dispose()
    Add-Check -Ctx $Ctx -Name 'the background launcher stops when asked' -Passed ($background.WaitForExit(15000))

    $crashes = @(Get-AppCrashEvents -Since $since)
    Add-Check -Ctx $Ctx -Name 'no Apple Music crash' -Passed ($crashes.Count -eq 0) `
        -Detail "$($crashes.Count) event(s): $(Format-CrashEvents -Events $crashes)"
    Copy-JournalToResults -Ctx $Ctx
    Write-JournalTail -FileName 'background.log'
    Write-JournalTail -FileName 'AppleMusic.log'
}
