# Apple Music's real element names, for themes and for an "Ampify" entry in its Settings: UI Automation tree of the main
# window and of Settings > Playback in the job log (first 400 lines each), plus a short dump of every Settings page.
# Apple Music runs without Ampify here: the names are Apple's own.

Register-Case -Id 'ui-tree' -Title 'UI Automation tree of the main window and of Settings' -Requires 'apple-music' -Action {
    param($Ctx)

    Start-AppNormally
    $main = Wait-AppleMusicReady -TimeoutSeconds 120
    Add-Check -Ctx $Ctx -Name 'main window ready' -Passed ($null -ne $main)
    if (-not $main) {
        Save-Screenshot -Ctx $Ctx -Name '01-main-window' | Out-Null
        Write-VisibleSummary 'startup'
        return
    }
    Show-AppleMusicWindow | Out-Null
    Start-Sleep -Seconds 5
    Add-Info -Ctx $Ctx -Name 'signed in' -Detail (Test-AppleMusicSignedIn)
    Save-Screenshot -Ctx $Ctx -Name '01-main-window' | Out-Null
    Write-TreeDump -Root (Get-UiaMainWindow) -Title 'main window' -MaxLines 400 -Path (Join-Path $Ctx.Dir 'tree-main-window.txt')

    $settings = Open-AppleMusicSettings
    Add-Check -Ctx $Ctx -Name 'Settings open (Playback page found)' -Passed ($null -ne $settings)
    if (-not $settings) {
        Save-Screenshot -Ctx $Ctx -Name '02-settings' | Out-Null
        Write-VisibleSummary 'opening Settings'
        Close-AppleMusic | Out-Null
        return
    }

    # The Settings pages are the items next to "Playback" (same parent).
    $playback = Find-Element -NamePattern $script:Patterns.Playback -VisibleOnly
    $pages = @()
    if ($playback) {
        $parent = [System.Windows.Automation.TreeWalker]::ControlViewWalker.GetParent($playback)
        if ($parent) {
            $pages = @(Find-Element -Root $parent -ControlType $playback.Current.ControlType.ProgrammaticName.Replace('ControlType.', '') -All -VisibleOnly)
        }
    }
    Add-Info -Ctx $Ctx -Name 'Settings pages' -Detail (($pages | ForEach-Object { $_.Current.Name }) -join ', ')
    $index = 2
    foreach ($page in $pages) {
        $name = [string]$page.Current.Name
        if ($name -match $script:Patterns.Playback) { continue }
        try {
            Invoke-Click -Element $page | Out-Null
            Start-Sleep -Seconds 2
            $slug = ($name -replace '[^A-Za-z0-9]+', '-').Trim('-').ToLowerInvariant()
            Save-Screenshot -Ctx $Ctx -Name ('{0:D2}-settings-{1}' -f $index, $slug) | Out-Null
            Write-TreeDump -Root (Get-UiaWindowOf $page) -Title "Settings > $name" -MaxLines 80
            $index++
        } catch {
            Add-Info -Ctx $Ctx -Name "page `"$name`"" -Detail "not opened: $($_.Exception.Message)"
        }
    }

    $playback = Find-Element -NamePattern $script:Patterns.Playback -VisibleOnly
    if ($playback) { Invoke-Click -Element $playback | Out-Null; Start-Sleep -Seconds 2 }
    $settingsWindow = if ($playback) { Get-UiaWindowOf $playback } else { $null }
    if (-not $settingsWindow) { $settingsWindow = Get-UiaMainWindow }
    Save-Screenshot -Ctx $Ctx -Name '10-settings-playback' | Out-Null
    Write-TreeDump -Root $settingsWindow -Title 'Settings > Playback' -MaxLines 400 -Path (Join-Path $Ctx.Dir 'tree-settings-playback.txt')
    $atmos = Find-Element -NamePattern 'Dolby Atmos' -VisibleOnly
    Add-Info -Ctx $Ctx -Name 'Dolby Atmos setting' -Detail $(if ($atmos) { Format-Element $atmos } else { 'not found on the Playback page' })

    $closed = Close-AppleMusic
    Add-Check -Ctx $Ctx -Name 'Apple Music closed cleanly' -Passed $closed
}
