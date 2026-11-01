# Steps 4 and 5 of the UI test: the midnight theme applies in Apple Music, then is removed without a restart when
# theme.active goes back to none. The setting changes in settings.txt, not in the Settings page: the "Ampify theme"
# section of the General page is still to be checked on a real PC.
# Job: MSVC (msvc-build), AMPIFY_XAML=ON and AMPIFY_BENCH=ON build (otherwise the injection refuses the runner's version).
# Needs $script:BenchKitRoot\ui\ with ampify_launcher.exe, ampify_kernel.dll, and themes\midnight.

Register-Case -Id 'ui-theme-midnight' -Title 'UI: the midnight theme applies, then is removed without a restart' -Requires 'apple-music' -Action {
    param($Ctx)

    $launcher = Join-Path $script:BenchKitRoot 'ui\ampify_launcher.exe'
    $example = Join-Path $script:BenchKitRoot 'ui\themes\midnight'
    if (-not (Test-Path -LiteralPath $launcher) -or -not (Test-Path -LiteralPath $example)) {
        Add-Skip -Ctx $Ctx -Name 'UI build and midnight theme present' -Reason ("no UI build in this bench: " + "$launcher / $example")
        return
    }

    # The midnight theme is copied into the user's Ampify folder, as in step 2 of the UI test.
    $themes = Join-Path $script:KitDir 'themes'
    New-Item -ItemType Directory -Force -Path $themes | Out-Null
    $target = Join-Path $themes 'midnight'
    Remove-Item -LiteralPath $target -Recurse -Force -ErrorAction SilentlyContinue
    Copy-Item -LiteralPath $example -Destination $target -Recurse -Force
    # Glass, the complete redesign, is tried in the same launch, after midnight.
    $glassExample = Join-Path $script:BenchKitRoot 'ui\themes\glass'
    if (Test-Path -LiteralPath $glassExample) {
        Remove-Item -LiteralPath (Join-Path $themes 'glass') -Recurse -Force -ErrorAction SilentlyContinue
        Copy-Item -LiteralPath $glassExample -Destination (Join-Path $themes 'glass') -Recurse -Force
    }

    Reset-KitState
    $settings = Join-Path $script:KitDir 'settings.txt'
    $lines = @()
    if (Test-Path -LiteralPath $settings) {
        $lines = @(Get-Content -LiteralPath $settings -Encoding UTF8 | Where-Object { $_ -notmatch '^\s*theme\.(active|scheme|inspect)\s*=' })
    }
    $lines += 'theme.active = midnight'
    $lines += 'theme.scheme = Dark'
    $lines += 'theme.inspect = 0'
    Set-Content -LiteralPath $settings -Value $lines -Encoding UTF8

    $since = Get-Date
    $launcherProcess = Start-Launcher -LauncherPath $launcher

    $launched = Wait-Journal -FileName 'launcher.log' -Text 'Apple Music started' -TimeoutSeconds 120
    Add-Check -Ctx $Ctx -Name 'Apple Music started by the launcher' -Passed $launched
    if (-not $launched) {
        Close-AppleMusic | Out-Null
        return
    }
    # New runner: the MPEG LA licence opens before the main window, which the kernel waits for.
    $firstRun = Close-AppleMusicFirstRun
    Add-Info -Ctx $Ctx -Name 'first launch of Apple Music' -Detail "$firstRun screen(s) closed (licence, welcome)"

    $hooked = Wait-Journal -FileName 'AppleMusic.log' -Text 'Themes: interface thread ready (DispatcherQueue of the main window)' -TimeoutSeconds 90
    Add-Check -Ctx $Ctx -Name 'interface thread ready (DispatcherQueue)' -Passed $hooked

    # 'Theme "midnight": N value(s) applied, ...': N is the number of values written.
    $applyPattern = 'Theme "midnight": (\d+) value\(s\) applied'
    $applied = Wait-Until -TimeoutSeconds 90 -Condition { $null -ne (Find-JournalMatch -FileName 'AppleMusic.log' -Pattern $applyPattern) }
    $count = 0
    if ($applied) {
        $match = Find-JournalMatch -FileName 'AppleMusic.log' -Pattern $applyPattern
        $count = [int]$match.Groups[1].Value
    }
    Add-Check -Ctx $Ctx -Name 'midnight theme: values applied' -Passed ($applied -and $count -gt 0) `
        -Detail "$count value(s)"

    $log = Read-Journal 'AppleMusic.log'
    $refusals = @(([string]$log) -split "`r?`n" | Where-Object { $_.StartsWith('Refused: ') })
    Add-Info -Ctx $Ctx -Name '"Refused" lines of the log' -Detail "$($refusals.Count): $(($refusals | Select-Object -First 3) -join ' | ')"

    Start-Sleep -Seconds 4  # one more tick: the colours reach what is already on screen
    Save-Screenshot -Ctx $Ctx -Name 'midnight-applied' | Out-Null

    if (Test-Path -LiteralPath (Join-Path $themes 'glass')) {
        $switch = @(Get-Content -LiteralPath $settings -Encoding UTF8 | Where-Object { $_ -notmatch '^\s*theme\.(active|scheme)\s*=' })
        $switch += 'theme.active = glass'
        $switch += 'theme.scheme = Dark'
        Set-Content -LiteralPath $settings -Value $switch -Encoding UTF8
        $glassPattern = 'Theme "glass": (\d+) value\(s\) applied'
        $glassApplied = Wait-Until -TimeoutSeconds 60 -Condition { $null -ne (Find-JournalMatch -FileName 'AppleMusic.log' -Pattern $glassPattern) }
        $glassCount = 0
        if ($glassApplied) { $glassCount = [int](Find-JournalMatch -FileName 'AppleMusic.log' -Pattern $glassPattern).Groups[1].Value }
        Add-Check -Ctx $Ctx -Name 'glass theme (redesign): values applied' -Passed ($glassApplied -and $glassCount -gt 0) `
            -Detail "$glassCount value(s)"
        Start-Sleep -Seconds 4
        Save-Screenshot -Ctx $Ctx -Name 'glass-applied' | Out-Null
    }

    # Removal: back to none in settings.txt, without restarting Apple Music.
    $kept = @(Get-Content -LiteralPath $settings -Encoding UTF8 | Where-Object { $_ -notmatch '^\s*theme\.active\s*=' })
    $kept += 'theme.active = none'
    Set-Content -LiteralPath $settings -Value $kept -Encoding UTF8

    $restored = Wait-Journal -FileName 'AppleMusic.log' -Text 'Theme removed: ' -TimeoutSeconds 60
    Add-Check -Ctx $Ctx -Name 'removal without a restart: "Theme removed" in the log' -Passed $restored

    Save-Screenshot -Ctx $Ctx -Name 'midnight-removed' | Out-Null

    $closed = Close-AppleMusic
    $exited = $launcherProcess.WaitForExit(15000)
    Add-Check -Ctx $Ctx -Name 'launcher: stops when Apple Music closes' -Passed ($closed -and $exited)

    $crashes = @(Get-AppCrashEvents -Since $since)
    Add-Check -Ctx $Ctx -Name 'no Apple Music crash' -Passed ($crashes.Count -eq 0) `
        -Detail "$($crashes.Count) event(s): $(Format-CrashEvents -Events $crashes)"

    Copy-JournalToResults -Ctx $Ctx
}
