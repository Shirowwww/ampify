# Release build (the one that ships): the kernel is injected only on a tested version, and the app stays healthy.
# The expected verdict depends on the version installed on the runner, read from facts/apple.json.

Register-Case -Id 'release-version' -Title 'Release build: injection only on a tested version' -Requires 'apple-music', 'kit:release' -Action {
    param($Ctx)

    $launcher = Join-Path $script:BenchKitRoot 'release\ampify_launcher.exe'

    $version = $script:AppleMusicVersion
    $tested = $script:KnownVersions -contains $version
    Add-Info -Ctx $Ctx -Name 'installed version' -Detail "$version (tested: $tested)"

    Reset-KitState
    $since = Get-Date
    $launcherProcess = Start-Launcher -LauncherPath $launcher

    $verdict = Wait-Journal -FileName 'launcher.log' -Text "version $version" -TimeoutSeconds 60
    Add-Check -Ctx $Ctx -Name 'launcher: version read from the package' -Passed $verdict
    if (-not $verdict) { return }

    if ($tested) {
        Add-Check -Ctx $Ctx -Name 'launcher: kernel injected, tested version' `
            -Passed (Test-JournalContains -FileName 'launcher.log' -Text "version $version tested: kernel injected")

        $launched = Wait-Journal -FileName 'launcher.log' -Text $script:LogPhrase.AppleMusicStarted -TimeoutSeconds 120
        Add-Check -Ctx $Ctx -Name 'Apple Music started by the launcher' -Passed $launched
        $kernelLoaded = Wait-Journal -FileName 'AppleMusic.log' -Text 'Kernel loaded in AppleMusic.exe' -TimeoutSeconds 60
        Add-Check -Ctx $Ctx -Name 'AppleMusic.log: kernel loaded in AppleMusic.exe' -Passed $kernelLoaded
    } else {
        Add-Check -Ctx $Ctx -Name 'launcher: version refused, Apple Music starts without the kernel' `
            -Passed (Test-JournalContains -FileName 'launcher.log' -Text "version $version not tested: Apple Music starts without the kernel")

        $launched = Wait-Journal -FileName 'launcher.log' -Text $script:LogPhrase.AppleMusicStarted -TimeoutSeconds 120
        Add-Check -Ctx $Ctx -Name 'Apple Music started by the launcher' -Passed $launched
        Start-Sleep -Seconds 30
        Add-Check -Ctx $Ctx -Name 'no kernel log (AppleMusic.log absent)' `
            -Passed (-not (Test-Path -LiteralPath (Join-Path $script:JournalDir 'AppleMusic.log')))
    }

    if ($launched) {
        Start-Sleep -Seconds 20
        $kernel = Get-KernelState -ProcessName 'AppleMusic'
        if ($tested) {
            Add-Check -Ctx $Ctx -Name 'ampify_kernel.dll present in AppleMusic.exe' `
                -Passed ($kernel.Processes -gt 0 -and $kernel.Loaded -eq $kernel.Processes) `
                -Detail "processes: $($kernel.Processes), with the kernel: $($kernel.Loaded), unreadable: $($kernel.Unreadable)"
        } else {
            Add-Check -Ctx $Ctx -Name 'ampify_kernel.dll absent from AppleMusic.exe' -Passed ($kernel.Loaded -eq 0) `
                -Detail "processes: $($kernel.Processes), with the kernel: $($kernel.Loaded), unreadable: $($kernel.Unreadable)"
        }
        Save-Screenshot -Ctx $Ctx -Name 'apple-music-release' | Out-Null
    }

    $closed = Close-AppleMusic
    $exited = $launcherProcess.WaitForExit(15000)
    Add-Check -Ctx $Ctx -Name 'launcher: stops when Apple Music closes' -Passed ($closed -and $exited)
    $crashes = @(Get-AppCrashEvents -Since $since)
    Add-Check -Ctx $Ctx -Name 'no Apple Music crash' -Passed ($crashes.Count -eq 0) `
        -Detail "$($crashes.Count) event(s): $(Format-CrashEvents -Events $crashes)"
}
