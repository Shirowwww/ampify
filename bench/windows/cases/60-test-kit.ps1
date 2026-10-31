# M5: the test kit, started in /auto mode like a double-click, writes its report and its archive. If Apple Music is
# installed on the runner, the kit must also start it through the shortcut. ETW traces need administrator rights:
# without them, the case passes with /noetl.

Register-Case -Id 'test-kit' -Title 'M5 test kit: report, archive and launch through the shortcut' -Action {
    param($Ctx)

    $testKit = Join-Path $script:KitRoot 'release\Ampify-Test.exe'
    if (-not (Test-Path -LiteralPath $testKit)) {
        Add-Check -Ctx $Ctx -Name 'Ampify-Test.exe present in release' -Passed $false -Detail $testKit
        return
    }

    # `net session` only succeeds in an elevated process: without it, the kit must not ask for UAC consent.
    $null = & net.exe session 2>$null
    $elevated = ($LASTEXITCODE -eq 0)

    Reset-KitState
    $arguments = @('/auto', '/wait:20')
    if (-not $elevated) { $arguments += '/noetl' }
    # /wait:20 plus the install, the launch (90 s at most) and the collection: 6 minutes is ample.
    $code = Invoke-KitProgram -Path $testKit -Arguments $arguments -TimeoutSeconds 360
    Add-Check -Ctx $Ctx -Name 'test kit: no error (code 0, 2 or 4)' -Passed ($code -in 0, 2, 4) -Detail "code $code"

    $pointer = Join-Path $script:KitDir 'reports\latest.txt'
    $run = if (Test-Path -LiteralPath $pointer) { (Get-Content -LiteralPath $pointer -Raw -Encoding UTF8).Trim() } else { '' }
    if ($code -notin 0, 2, 4) {
        # Show how far the kit got: its own step log, from the newest report folder.
        $latest = Get-ChildItem -LiteralPath (Join-Path $script:KitDir 'reports') -Directory -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if ($latest) {
            Write-Host "----- steps.txt ($($latest.Name))"
            Get-Content -LiteralPath (Join-Path $latest.FullName 'steps.txt') -ErrorAction SilentlyContinue |
                ForEach-Object { Write-Host "  $_" }
        }
    }
    Add-Check -Ctx $Ctx -Name 'latest report pointed to (reports\latest.txt)' -Passed ([bool]$run -and (Test-Path -LiteralPath $run))
    if (-not $run) { return }

    $report = Join-Path $run 'report.txt'
    Add-Check -Ctx $Ctx -Name 'report.txt written' -Passed (Test-Path -LiteralPath $report)
    Add-Check -Ctx $Ctx -Name '.zip archive created next to the folder' -Passed (Test-Path -LiteralPath ($run + '.zip'))
    $text = if (Test-Path -LiteralPath $report) { Get-Content -LiteralPath $report -Raw -Encoding UTF8 } else { '' }
    Add-Check -Ctx $Ctx -Name 'report: Ampify installed' -Passed ($text -match '\[OK\] *Ampify installed')

    if ($script:AppleMusicInstalled) {
        Add-Check -Ctx $Ctx -Name 'Apple Music started by Ampify (Start menu shortcut)' `
            -Passed ($text -match 'Apple Music started by Ampify: yes') -Detail "version $($script:AppleMusicVersion)"
        $known = $script:KnownVersions -contains $script:AppleMusicVersion
        if ($known) {
            Add-Check -Ctx $Ctx -Name 'tested version: the kernel is allowed' -Passed ($text -match 'Tested version, kernel allowed: yes')
        } else {
            Add-Check -Ctx $Ctx -Name 'untested version: the kernel stays inactive, without blocking Apple Music' `
                -Passed ($text -match 'no, the kernel stays inactive') -Detail "version $($script:AppleMusicVersion)"
        }
    } else {
        Add-Skip -Ctx $Ctx -Name 'launch through the shortcut' -Reason 'Apple Music missing from the runner'
    }

    if ($elevated) {
        $trace = Join-Path $run 'trace.etl'
        # The trace is for the owner's PC. On the runner logman may refuse it: that is reported, not failed.
        $traced = (Test-Path -LiteralPath $trace) -and (Get-Item -LiteralPath $trace).Length -gt 0
        $traceLines = @(Get-Content -LiteralPath (Join-Path $run 'steps.txt') -ErrorAction SilentlyContinue |
            Where-Object { $_ -match 'ETW trace|logman' }) -join ' | '
        Add-Info -Ctx $Ctx -Name 'ETW trace' -Detail $(if ($traced) { 'recorded' } else { "not recorded: $traceLines" })
    } else {
        Add-Skip -Ctx $Ctx -Name 'ETW trace' -Reason 'runner not elevated: /noetl'
    }
    Add-Check -Ctx $Ctx -Name 'Windows events read (application-events.txt)' `
        -Passed (Test-Path -LiteralPath (Join-Path $run 'application-events.txt'))

    Copy-Item -LiteralPath $run -Destination (Join-Path $Ctx.Dir 'report') -Recurse -Force -ErrorAction SilentlyContinue
    Copy-Item -LiteralPath ($run + '.zip') -Destination $Ctx.Dir -Force -ErrorAction SilentlyContinue
    Close-AppleMusic | Out-Null
}
