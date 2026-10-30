# Apple Music's real element names, for themes and for a "Mods" entry in its Settings: UI Automation tree of the main
# window and of Settings > Playback in the job log (first 400 lines each), plus a short dump of every Settings page.
# Apple Music runs without the kit here: the names are Apple's own.

Register-Case -Id 'interface-arbre' -Title 'Arbre UIA de la fenêtre principale et des Réglages' -Requires 'apple-music' -Action {
    param($Ctx)

    Start-AppNormally
    $main = Wait-AppleMusicReady -TimeoutSeconds 120
    Add-Check -Ctx $Ctx -Name 'fenêtre principale prête' -Passed ($null -ne $main)
    if (-not $main) {
        Save-Screenshot -Ctx $Ctx -Name '01-fenetre-principale' | Out-Null
        Write-VisibleSummary 'démarrage'
        return
    }
    Show-AppleMusicWindow | Out-Null
    Start-Sleep -Seconds 5
    Add-Info -Ctx $Ctx -Name 'connecté' -Detail (Test-AppleMusicSignedIn)
    Save-Screenshot -Ctx $Ctx -Name '01-fenetre-principale' | Out-Null
    Write-TreeDump -Root (Get-UiaMainWindow) -Title 'fenêtre principale' -MaxLines 400 -Path (Join-Path $Ctx.Dir 'arbre-fenetre-principale.txt')

    $settings = Open-AppleMusicSettings
    Add-Check -Ctx $Ctx -Name 'Réglages ouverts (page Lecture trouvée)' -Passed ($null -ne $settings)
    if (-not $settings) {
        Save-Screenshot -Ctx $Ctx -Name '02-reglages' | Out-Null
        Write-VisibleSummary 'ouverture des Réglages'
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
    Add-Info -Ctx $Ctx -Name 'pages des Réglages' -Detail (($pages | ForEach-Object { $_.Current.Name }) -join ', ')
    $index = 2
    foreach ($page in $pages) {
        $name = [string]$page.Current.Name
        if ($name -match $script:Patterns.Playback) { continue }
        try {
            Invoke-Click -Element $page | Out-Null
            Start-Sleep -Seconds 2
            $slug = ($name -replace '[^A-Za-z0-9]+', '-').Trim('-').ToLowerInvariant()
            Save-Screenshot -Ctx $Ctx -Name ('{0:D2}-reglages-{1}' -f $index, $slug) | Out-Null
            Write-TreeDump -Root (Get-UiaWindowOf $page) -Title "Réglages > $name" -MaxLines 80
            $index++
        } catch {
            Add-Info -Ctx $Ctx -Name "page « $name »" -Detail "non ouverte : $($_.Exception.Message)"
        }
    }

    $playback = Find-Element -NamePattern $script:Patterns.Playback -VisibleOnly
    if ($playback) { Invoke-Click -Element $playback | Out-Null; Start-Sleep -Seconds 2 }
    $settingsWindow = if ($playback) { Get-UiaWindowOf $playback } else { $null }
    if (-not $settingsWindow) { $settingsWindow = Get-UiaMainWindow }
    Save-Screenshot -Ctx $Ctx -Name '10-reglages-lecture' | Out-Null
    Write-TreeDump -Root $settingsWindow -Title 'Réglages > Lecture' -MaxLines 400 -Path (Join-Path $Ctx.Dir 'arbre-reglages-lecture.txt')
    $atmos = Find-Element -NamePattern 'Dolby Atmos' -VisibleOnly
    Add-Info -Ctx $Ctx -Name 'réglage Dolby Atmos' -Detail $(if ($atmos) { Format-Element $atmos } else { 'introuvable sur la page Lecture' })

    $closed = Close-AppleMusic
    Add-Check -Ctx $Ctx -Name 'Apple Music fermé proprement' -Passed $closed
}
