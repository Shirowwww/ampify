# Step 1 of the UI test, without Apple Music: the example themes load without a problem with ampify_theme_check.
# Job: MSVC (msvc-build), not the MinGW bench. Needs $script:BenchKitRoot\ui\ampify_theme_check.exe and a copy of
# themes/examples in $script:BenchKitRoot\ui\themes\examples.

Register-Case -Id 'ui-examples' -Title 'UI: the example themes are usable (ampify_theme_check)' -Action {
    param($Ctx)

    $check = Join-Path $script:BenchKitRoot 'ui\ampify_theme_check.exe'
    if (-not (Test-Path -LiteralPath $check)) {
        Add-Skip -Ctx $Ctx -Name 'ampify_theme_check.exe present' -Reason "no UI build in this bench (MinGW does not compile XAML): $check"
        return
    }

    $examples = Join-Path $script:BenchKitRoot 'ui\themes\examples'
    foreach ($name in 'midnight', 'large-text', 'rounded-buttons', 'hide-chrome', 'glass', 'artwork') {
        $folder = Join-Path $examples $name
        # The text is written as UTF-8 by the C++ code: the case tests plain words and the exit code.
        $output = (& $check $folder 2>&1 | Out-String)
        $code = $LASTEXITCODE
        Add-Check -Ctx $Ctx -Name "${name}: exit code 0" -Passed ($code -eq 0) -Detail "code $code"
        Add-Check -Ctx $Ctx -Name "${name}: theme usable" -Passed ($output -match 'usable')
        Add-Check -Ctx $Ctx -Name "${name}: no problem reported" -Passed ($output -notmatch 'Problem')
    }
}
