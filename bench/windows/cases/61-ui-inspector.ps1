# Step 3 of the UI test: the inspector writes the tree of Apple Music's window to logs\inspector.txt, then sets
# theme.inspect back to 0, and `ampify theme names` checks the stable names against its inspector.json. This case opens
# no page: the survey of the General page is still to be done on a real PC.
# Job: MSVC (msvc-build), AMPIFY_XAML=ON and AMPIFY_BENCH=ON build (otherwise the injection refuses the runner's version).
# Needs $script:BenchKitRoot\ui\ with ampify_launcher.exe and ampify_kernel.dll side by side.

Register-Case -Id 'ui-inspector' -Title 'UI: the inspector writes the interface tree, then resets itself' -Requires 'apple-music', 'kit:ui' -Action {
    param($Ctx)

    $launcher = Join-Path $script:BenchKitRoot 'ui\ampify_launcher.exe'

    # Settings: the other keys are kept, the case sets its own. The theme is off so that nothing is applied.
    Reset-KitState
    Set-Settings -Values @{ 'theme.active' = 'none'; 'theme.inspect' = '1' }

    $since = Get-Date
    $launcherProcess = Start-Launcher -LauncherPath $launcher

    $launched = Wait-Journal -FileName 'launcher.log' -Text 'Apple Music started' -TimeoutSeconds 120
    Add-Check -Ctx $Ctx -Name 'Apple Music started by the launcher' -Passed $launched
    if (-not $launched) {
        Close-AppleMusic | Out-Null
        return
    }
    Clear-FirstRun -Ctx $Ctx

    $hooked = Wait-Journal -FileName 'AppleMusic.log' -Text 'Themes: interface thread ready (DispatcherQueue of the main window)' -TimeoutSeconds 90
    Add-Check -Ctx $Ctx -Name 'interface thread ready (DispatcherQueue)' -Passed $hooked

    $dumped = Wait-Journal -FileName 'AppleMusic.log' -Text 'Inspector: tree written' -TimeoutSeconds 90
    Add-Check -Ctx $Ctx -Name 'log: tree written by the inspector' -Passed $dumped

    $dumpPath = Join-Path $script:JournalDir 'inspector.txt'
    $dump = ''
    if (Test-Path -LiteralPath $dumpPath) {
        $dump = Get-Content -LiteralPath $dumpPath -Raw -Encoding UTF8
        Copy-Item -LiteralPath $dumpPath -Destination $Ctx.Dir -Force
    }
    if ($dump) {
        # The artifact is not reachable from every machine: the first lines of the real tree go to the job log.
        Write-Host "----- inspector.txt (first 300 lines)"
        $dump -split "`r?`n" | Select-Object -First 300 | ForEach-Object { Write-Host "  $_" }
    } else {
        Write-JournalTail -FileName 'AppleMusic.log'
    }
    $selectors = ([regex]::Matches($dump, 'selector:')).Count
    Add-Check -Ctx $Ctx -Name 'inspector.txt: elements with their selector' -Passed ($selectors -gt 0) `
        -Detail "$selectors element(s)"
    # Every stable name (.sidebar, .card...) should match something in this tree. This case opens no page, so the
    # names that live in other views (lyrics, queue, full-screen player) are listed as "Found in no tree" here. That is
    # reported as INFO, not FAIL, until the General page survey tells which names belong to the main window: then the
    # check can gate the job.
    $jsonPath = Join-Path $script:JournalDir 'inspector.json'
    $cli = Join-Path $script:BenchKitRoot 'ui\ampify.exe'
    if (-not (Test-Path -LiteralPath $cli)) {
        Add-Skip -Ctx $Ctx -Name 'stable names checked against the tree' -Reason "no ampify.exe in the UI build: $cli"
    } elseif (-not (Test-Path -LiteralPath $jsonPath)) {
        Add-Check -Ctx $Ctx -Name 'inspector.json: written with the tree' -Passed $false -Detail 'logs\inspector.json not written'
    } else {
        Copy-Item -LiteralPath $jsonPath -Destination $Ctx.Dir -Force
        $namesOutput = @(& $cli theme names $jsonPath 2>&1)
        $namesOutput | ForEach-Object { Write-Host "  $_" }
        $vanished = @($namesOutput | Where-Object { "$_" -match 'Found in no tree' })
        $detail = if ($vanished.Count -gt 0) { "$vanished" } else { 'every stable name matches the tree' }
        Add-Info -Ctx $Ctx -Name 'stable names checked against the tree' -Detail $detail
    }
    Add-Info -Ctx $Ctx -Name 'page open during the survey' `
        -Detail 'none: this case does not open the General page, its survey is still to be done'

    # The inspector sets theme.inspect back to 0 once the survey is written.
    $now = @(Get-Content -LiteralPath $script:SettingsFile -Encoding UTF8)
    $reset = @($now | Where-Object { $_ -match '^\s*theme\.inspect\s*=\s*0\s*$' }).Count -gt 0
    Add-Check -Ctx $Ctx -Name 'settings.txt: theme.inspect reset to 0' -Passed $reset

    Save-Screenshot -Ctx $Ctx -Name 'inspector' | Out-Null

    $closed = Close-AppleMusic
    $exited = $launcherProcess.WaitForExit(15000)
    Add-Check -Ctx $Ctx -Name 'launcher: stops when Apple Music closes' -Passed ($closed -and $exited)

    $crashes = @(Get-AppCrashEvents -Since $since)
    Add-Check -Ctx $Ctx -Name 'no Apple Music crash' -Passed ($crashes.Count -eq 0) `
        -Detail "$($crashes.Count) event(s): $(Format-CrashEvents -Events $crashes)"

    Copy-JournalToResults -Ctx $Ctx
}
