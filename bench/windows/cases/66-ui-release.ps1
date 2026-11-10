# Checks of the 1.3.0 features that need no Apple Music: the lyrics lines setting of the full-screen player, and the
# ready-made layouts, now plain CSS files built into Ampify, under every shipped theme. Each asks the UI build's
# ampify.exe, in a folder of its own (--kit-dir). Needs $script:BenchKitRoot\ui\ with ampify.exe and themes\.

function New-ReleaseTestKit {
    $dir = Join-Path ([IO.Path]::GetTempPath()) ('ampify-release-kit-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    return $dir
}

Register-Case -Id 'ui-lyrics-window' -Title 'UI: the lines of lyrics around the one sung take each choice' -Requires 'kit:ui' -Action {
    param($Ctx)

    $cli = Join-Path $script:BenchKitRoot 'ui\ampify.exe'
    $kit = New-ReleaseTestKit
    try {
        foreach ($key in @('apple.lyricsBefore', 'apple.lyricsAfter')) {
            # Unset, the setting keeps Apple's own view (every line).
            $fresh = (@(& $cli --kit-dir $kit config $key 2>&1) | Out-String).Trim()
            Add-Check -Ctx $Ctx -Name "$key is unset by default" -Passed ($fresh -match 'not set') -Detail $fresh

            foreach ($lines in @('0', '3', '5', 'all')) {
                $out = @(& $cli --kit-dir $kit config $key $lines 2>&1)
                $code = $LASTEXITCODE
                $read = (@(& $cli --kit-dir $kit config $key 2>&1) | Out-String).Trim()
                Add-Check -Ctx $Ctx -Name "$key takes $lines" -Passed ($code -eq 0 -and $read -eq $lines) -Detail "$($out -join ' | ')"
            }

            $refused = @(& $cli --kit-dir $kit config $key 6 2>&1)
            $refusedCode = $LASTEXITCODE
            Add-Check -Ctx $Ctx -Name "$key refuses a number off the list" -Passed ($refusedCode -ne 0) -Detail "$($refused -join ' | ')"
        }
    } finally {
        Remove-Item -LiteralPath $kit -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Register-Case -Id 'ui-builtin-layouts' -Title 'UI: every shipped theme loads with the layouts built into Ampify' -Requires 'kit:ui' -Action {
    param($Ctx)

    $cli = Join-Path $script:BenchKitRoot 'ui\ampify.exe'
    $themes = Join-Path $script:BenchKitRoot 'ui\themes'
    $kit = New-ReleaseTestKit
    try {
        $folders = @(Get-ChildItem -LiteralPath $themes -Directory | Where-Object { $_.Name -ne 'snippets' })
        Add-Check -Ctx $Ctx -Name 'the kit ships its themes' -Passed ($folders.Count -gt 0) -Detail "$($folders.Count) themes"
        foreach ($folder in $folders) {
            $out = @(& $cli --kit-dir $kit theme check $folder.FullName 2>&1)
            $code = $LASTEXITCODE
            $text = $out -join "`n"
            # An @import of a layout the kernel does not know is reported as a problem and the theme stays usable:
            # the result line alone would not show a layout gone missing from the build.
            $usable = $code -eq 0 -and $text -match 'Result: usable'
            $missing = $text -match 'ampify:[a-z-]+.*(unknown|not found)'
            Add-Check -Ctx $Ctx -Name "$($folder.Name) is usable with its layouts" -Passed ($usable -and -not $missing) `
                -Detail (($out | Select-Object -Last 3) -join ' | ')
        }
    } finally {
        Remove-Item -LiteralPath $kit -Recurse -Force -ErrorAction SilentlyContinue
    }
}
