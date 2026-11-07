# Steps 1 and 2 of the M3 test, on the bench build (accepts any version): the kernel loads in both processes,
# the app stays healthy, and the suspended launch is noted. The version verdict is the one of case 20.

Register-Case -Id 'bench-injection' -Title 'Bench build: kernel in AppleMusic.exe and AMPLibraryAgent.exe' -Requires 'apple-music', 'kit:bench' -Action {
    param($Ctx)

    $launcher = Join-Path $script:BenchKitRoot 'bench\ampify_launcher.exe'

    # The bench injects on the installed version, even outside the release list: it goes into the report.
    Add-Info -Ctx $Ctx -Name 'injected version (bench mode)' `
        -Detail "$($script:AppleMusicVersion) (release list: $($script:KnownVersions -join ', '))"

    Reset-KitState
    $since = Get-Date
    $launcherProcess = Start-Launcher -LauncherPath $launcher

    $launched = Wait-Journal -FileName 'launcher.log' -Text $script:LogPhrase.AppleMusicStarted -TimeoutSeconds 120
    Add-Check -Ctx $Ctx -Name 'Apple Music started by the launcher' -Passed $launched
    if (-not $launched) {
        Write-JournalTail -FileName 'launcher.log'
        Write-JournalTail -FileName 'AppleMusic.log'
        Close-AppleMusic | Out-Null
        return
    }

    # Signature: the package's player DLL must export the names the kernel depends on. Otherwise the launcher refuses, even in bench mode.
    $signature = Find-JournalMatch -FileName 'launcher.log' -Pattern 'signature (?:not )?recognised[^\r\n]*'
    Add-Check -Ctx $Ctx -Name 'signature: AVFoundationCF.dll exports recognised' `
        -Passed ($null -ne $signature -and $signature.Value -like 'signature recognised*') `
        -Detail $(if ($signature) { $signature.Value } else { 'line missing from the log' })

    # Suspended launch: the launcher pauses Apple Music's threads during the injection, then resumes them.
    # Zero paused threads means nothing was suspended.
    $paused = Find-JournalMatch -FileName 'launcher.log' -Pattern '(\d+) thread\(s\) paused'
    $unavailable = Test-JournalContains -FileName 'launcher.log' -Text 'no pause'
    $resumed = Wait-Journal -FileName 'launcher.log' -Text 'thread(s) resumed after injection' -TimeoutSeconds 30
    $threads = Find-JournalMatch -FileName 'launcher.log' -Pattern '(\d+) thread\(s\) resumed'
    if ($paused -and $resumed -and $threads) {
        $count = [int]$paused.Groups[1].Value
        Add-Check -Ctx $Ctx -Name 'suspended launch: threads were paused, then resumed' `
            -Passed ($count -gt 0) -Detail "$count paused, $($threads.Groups[1].Value) resumed"
    } elseif ($unavailable) {
        Add-Info -Ctx $Ctx -Name 'suspended launch' -Detail 'not available on this Windows: injection after the start (less safe)'
    } else {
        Add-Check -Ctx $Ctx -Name 'suspended launch: log line present' -Passed $false `
            -Detail "neither 'paused' nor 'no pause', or no resume line"
    }

    # The process name carries its id: "AppleMusic.exe (1234): kernel loaded".
    $appPattern = 'AppleMusic\.exe \(\d+\): kernel loaded'
    $kernelInApp = Wait-Until -TimeoutSeconds 60 -Condition { $null -ne (Find-JournalMatch -FileName 'launcher.log' -Pattern $appPattern) }
    Add-Check -Ctx $Ctx -Name 'launcher: kernel loaded in AppleMusic.exe' -Passed $kernelInApp
    $hostReady = Wait-Journal -FileName 'AppleMusic.log' -Text $script:LogPhrase.ExtensionHostReady -TimeoutSeconds 60
    Add-Check -Ctx $Ctx -Name 'AppleMusic.log: extension host ready' -Passed $hostReady

    $agentPattern = 'AMPLibraryAgent\.exe \(\d+\): kernel loaded'
    $agentInjected = Wait-Until -TimeoutSeconds 30 -Condition { $null -ne (Find-JournalMatch -FileName 'launcher.log' -Pattern $agentPattern) }
    if (-not $agentInjected -and @(Get-Process -Name 'AMPLibraryAgent' -ErrorAction SilentlyContinue).Count -eq 0) {
        # Without an Apple account, Apple Music does not start its engine on the runner. The bench starts it the way the
        # app would: by activating the COM class the package declares (AMP.Core.IAMPMusicLibrary, knowledge/manifest-com.txt).
        # The launcher follows the package and must inject it as usual. Nothing is called in the engine.
        $activation = Start-AgentByCom
        Add-Info -Ctx $Ctx -Name 'AMPLibraryAgent.exe started by the bench (COM)' -Detail $activation
        $agentInjected = Wait-Until -TimeoutSeconds 60 -Condition { $null -ne (Find-JournalMatch -FileName 'launcher.log' -Pattern $agentPattern) }
    }
    Add-Check -Ctx $Ctx -Name 'launcher: kernel loaded in AMPLibraryAgent.exe' -Passed $agentInjected `
        -Detail $(if ($agentInjected) { 'agent seen and injected' } else { 'AMPLibraryAgent.exe not seen within 90 s' })
    $agentLogged = Wait-Journal -FileName 'AMPLibraryAgent.log' -Text 'Kernel loaded in AMPLibraryAgent.exe' -TimeoutSeconds 15
    Add-Check -Ctx $Ctx -Name 'AMPLibraryAgent.log: kernel loaded' -Passed $agentLogged
    $agentRunning = @(Get-Process -Name 'AMPLibraryAgent' -ErrorAction SilentlyContinue).Count
    Add-Info -Ctx $Ctx -Name 'AMPLibraryAgent.exe open at check time' -Detail "$agentRunning process(es)"

    $healthy = Wait-Until -TimeoutSeconds 60 -Condition {
        $null -ne (Find-JournalMatch -FileName 'AppleMusic.log' -Pattern 'Healthy after \d+ s: safe mode count reset')
    }
    Add-Check -Ctx $Ctx -Name 'AppleMusic.log: healthy, safe mode count reset' -Passed $healthy

    $kernelApp = Get-KernelState -ProcessName 'AppleMusic'
    Add-Check -Ctx $Ctx -Name 'ampify_kernel.dll present in AppleMusic.exe' `
        -Passed ($kernelApp.Processes -gt 0 -and $kernelApp.Loaded -eq $kernelApp.Processes) `
        -Detail "processes: $($kernelApp.Processes), with the kernel: $($kernelApp.Loaded), unreadable: $($kernelApp.Unreadable)"
    $kernelAgent = Get-KernelState -ProcessName 'AMPLibraryAgent'
    if ($kernelAgent.Processes -gt 0) {
        Add-Check -Ctx $Ctx -Name 'ampify_kernel.dll present in AMPLibraryAgent.exe' `
            -Passed ($kernelAgent.Loaded -eq $kernelAgent.Processes) `
            -Detail "processes: $($kernelAgent.Processes), with the kernel: $($kernelAgent.Loaded), unreadable: $($kernelAgent.Unreadable)"
    }

    $notResponding = @(Get-AppProcesses | Where-Object { -not $_.Responding }).Count
    Add-Check -Ctx $Ctx -Name 'Apple Music window responds' -Passed ($notResponding -eq 0)
    Save-Screenshot -Ctx $Ctx -Name 'apple-music-with-kernel' | Out-Null

    $launcherLog = Read-Journal 'launcher.log'
    $unexpected = [bool]($launcherLog -and ($launcherLog.Contains('Unexpected error') -or $launcherLog.Contains('failed,')))
    Add-Check -Ctx $Ctx -Name 'launcher: no unexpected error or injection failure' -Passed (-not $unexpected)

    $closed = Close-AppleMusic
    $exited = $launcherProcess.WaitForExit(15000)
    Add-Check -Ctx $Ctx -Name 'launcher: stops when Apple Music closes' -Passed ($closed -and $exited)
    Add-Check -Ctx $Ctx -Name 'log: Apple Music closed' `
        -Passed (Test-JournalContains -FileName 'launcher.log' -Text 'Apple Music closed: the launcher stops')

    $agentStillOpen = @(Get-Process -Name 'AMPLibraryAgent' -ErrorAction SilentlyContinue).Count -gt 0
    Add-Info -Ctx $Ctx -Name 'AMPLibraryAgent.exe after Apple Music closed' `
        -Detail $(if ($agentStillOpen) { 'still open' } else { 'stopped' })

    $crashes = @(Get-AppCrashEvents -Since $since)
    Add-Check -Ctx $Ctx -Name 'no Apple Music crash' -Passed ($crashes.Count -eq 0) `
        -Detail "$($crashes.Count) event(s): $(Format-CrashEvents -Events $crashes)"

    Write-JournalTail -FileName 'launcher.log'
    Write-JournalTail -FileName 'AppleMusic.log'
    Write-JournalTail -FileName 'AMPLibraryAgent.log'
}
