# The runner has an audio output for Apple Music: a default render endpoint, and what ISpatialAudioClient offers on it
# (spatial objects, and the metadata stream Apple Music opens for Dolby Atmos). Set up by live/setup-audio.ps1.

Register-Case -Id 'sortie-audio' -Title 'Sortie audio virtuelle par défaut, son spatial' -Action {
    param($Ctx)

    $endpoints = @(Get-RenderEndpoints)
    Add-Check -Ctx $Ctx -Name 'au moins une sortie audio active' -Passed ($endpoints.Count -gt 0) `
        -Detail (($endpoints | ForEach-Object { $_.Name }) -join ', ')
    $default = Get-DefaultRenderEndpoint
    Add-Check -Ctx $Ctx -Name 'sortie audio par défaut' -Passed ($null -ne $default) `
        -Detail $(if ($default) { $default.Name } else { 'aucune' })
    if (-not $default) { return }

    $state = Get-SpatialAudioState
    Add-Info -Ctx $Ctx -Name 'ISpatialAudioClient' -Detail (Get-SpatialAudioSummary)
    foreach ($line in Invoke-SpatialSoundConfig) { Add-Info -Ctx $Ctx -Name 'son spatial (WinRT)' -Detail $line }
    if (-not $state.Error -and $state.MetadataStreamHr -ne 0) {
        Add-Info -Ctx $Ctx -Name 'Dolby Atmos' -Detail ("pas de flux à métadonnées sur cette sortie : Apple Music jouera l'Atmos " +
            "autrement (objets spatiaux ou stéréo), le journal du noyau le dira")
    }
}
