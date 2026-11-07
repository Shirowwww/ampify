# Step 1 of the UI test, without Apple Music: the example themes load without a problem with `ampify theme check`.
# Needs $script:BenchKitRoot\ui\ampify.exe and a copy of
# themes/ in $script:BenchKitRoot\ui\themes.

Register-Case -Id 'ui-examples' -Title 'UI: the example themes are usable (ampify theme check)' -Requires 'kit:ui' -Action {
    param($Ctx)

    $check = Join-Path $script:BenchKitRoot 'ui\ampify.exe'

    $examples = Join-Path $script:BenchKitRoot 'ui\themes'
    foreach ($name in Get-ChildItem -LiteralPath $examples -Directory | Where-Object { Test-Path (Join-Path $_.FullName 'manifest.json') } | ForEach-Object Name) {
        $folder = Join-Path $examples $name
        # The text is written as UTF-8 by the C++ code: the case tests plain words and the exit code.
        $output = (& $check theme check $folder 2>&1 | Out-String)
        $code = $LASTEXITCODE
        Add-Check -Ctx $Ctx -Name "${name}: exit code 0" -Passed ($code -eq 0) -Detail "code $code"
        Add-Check -Ctx $Ctx -Name "${name}: theme usable" -Passed ($output -match 'usable')
        Add-Check -Ctx $Ctx -Name "${name}: no problem reported" -Passed ($output -notmatch 'Problem')
    }
}
