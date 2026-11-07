# Four fast UI checks of the recent features, without Apple Music: each one asks the UI build's ampify.exe, in a folder of
# its own (--kit-dir), whether a setting or an element takes the value it should. The look itself is checked on a real PC.
# Needs $script:BenchKitRoot\ui\ with ampify.exe and themes\ (the copy the bench job makes, with its snippets folder).

function New-UiTestKit {
    $dir = Join-Path ([IO.Path]::GetTempPath()) ('ampify-ui-kit-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    return $dir
}

Register-Case -Id 'ui-hover-sidebar' -Title 'UI: the hover sidebar setting is known, off by default, and checked' -Requires 'kit:ui' -Action {
    param($Ctx)

    $cli = Join-Path $script:BenchKitRoot 'ui\ampify.exe'

    $kit = New-UiTestKit
    try {
        $fresh = @(& $cli --kit-dir $kit config apple.sidebar 2>&1)
        Add-Check -Ctx $Ctx -Name 'off by default: the sidebar opens as Apple Music shows it' -Passed ("$($fresh | Select-Object -First 1)" -eq 'open') `
            -Detail "$($fresh -join ' | ')"

        $set = @(& $cli --kit-dir $kit config apple.sidebar hover 2>&1)
        $setCode = $LASTEXITCODE
        Add-Check -Ctx $Ctx -Name 'apple.sidebar takes hover' -Passed ($setCode -eq 0) -Detail "$($set -join ' | ')"

        $read = @(& $cli --kit-dir $kit config apple.sidebar 2>&1)
        Add-Check -Ctx $Ctx -Name 'apple.sidebar reads back hover' -Passed ("$($read | Select-Object -First 1)" -eq 'hover') -Detail "$($read -join ' | ')"

        $refused = @(& $cli --kit-dir $kit config apple.sidebar folded 2>&1)
        $refusedCode = $LASTEXITCODE
        Add-Check -Ctx $Ctx -Name 'an unknown value is refused' -Passed ($refusedCode -ne 0) -Detail "$($refused -join ' | ')"
    } finally {
        Remove-Item -LiteralPath $kit -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Register-Case -Id 'ui-bar-position' -Title 'UI: Glass takes the playback bar at the top or at the bottom' -Requires 'kit:ui' -Action {
    param($Ctx)

    $cli = Join-Path $script:BenchKitRoot 'ui\ampify.exe'
    $glass = Join-Path $script:BenchKitRoot 'ui\themes\glass'

    $kit = New-UiTestKit
    try {
        New-Item -ItemType Directory -Force -Path (Join-Path $kit 'themes') | Out-Null
        Copy-Item -LiteralPath $glass -Destination (Join-Path $kit 'themes\glass') -Recurse

        foreach ($position in @('Top', 'Bottom')) {
            $out = @(& $cli --kit-dir $kit settings theme glass bar-position $position 2>&1)
            $code = $LASTEXITCODE
            $read = (@(& $cli --kit-dir $kit settings theme glass 2>&1) | Out-String)
            Add-Check -Ctx $Ctx -Name "bar-position takes $position" -Passed ($code -eq 0 -and $read -match "bar-position = $position") `
                -Detail "$($out -join ' | ')"
        }

        $refused = @(& $cli --kit-dir $kit settings theme glass bar-position Sideways 2>&1)
        $refusedCode = $LASTEXITCODE
        Add-Check -Ctx $Ctx -Name 'a position off the list is refused' -Passed ($refusedCode -ne 0) -Detail "$($refused -join ' | ')"
    } finally {
        Remove-Item -LiteralPath $kit -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Register-Case -Id 'ui-compact-sidebar' -Title 'UI: the Compact sidebar snippet is a valid element' -Requires 'kit:ui' -Action {
    param($Ctx)

    $cli = Join-Path $script:BenchKitRoot 'ui\ampify.exe'
    $snippet = Join-Path $script:BenchKitRoot 'ui\themes\snippets\compact-sidebar'

    $out = @(& $cli validate $snippet 2>&1)
    $code = $LASTEXITCODE
    Add-Check -Ctx $Ctx -Name 'compact-sidebar: manifest valid' -Passed ($code -eq 0) -Detail "$($out -join ' | ')"
    $css = Join-Path $snippet 'user.css'
    $named = (Test-Path -LiteralPath $css) -and ((Get-Content -LiteralPath $css -Raw -Encoding UTF8) -match '\.sidebar-entry')
    Add-Check -Ctx $Ctx -Name 'compact-sidebar: names the Ampify entries (.sidebar-entry)' -Passed $named
}

Register-Case -Id 'ui-current-lyrics' -Title 'UI: Lyrics spotlight styles the line being sung (:current)' -Requires 'kit:ui' -Action {
    param($Ctx)

    $cli = Join-Path $script:BenchKitRoot 'ui\ampify.exe'
    $snippet = Join-Path $script:BenchKitRoot 'ui\themes\snippets\lyrics-spotlight'

    $out = @(& $cli validate $snippet 2>&1)
    $code = $LASTEXITCODE
    Add-Check -Ctx $Ctx -Name 'lyrics-spotlight: manifest valid' -Passed ($code -eq 0) -Detail "$($out -join ' | ')"
    $css = Join-Path $snippet 'user.css'
    $current = (Test-Path -LiteralPath $css) -and ((Get-Content -LiteralPath $css -Raw -Encoding UTF8) -match '\.lyrics-line-content:current')
    Add-Check -Ctx $Ctx -Name 'lyrics-spotlight: styles the line being sung with :current' -Passed $current
}
