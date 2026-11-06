# The runner has an audio output for Apple Music: a default render endpoint, and what ISpatialAudioClient offers on it
# (spatial objects, and the metadata stream Apple Music opens for Dolby Atmos). A virtual device (VB-CABLE, Scream)
# gives a machine without a sound card that output.

Register-Case -Id 'audio-output' -Title 'Default virtual audio output, spatial sound' -Action {
    param($Ctx)

    $endpoints = @([AmpifyAudio.Endpoints]::ActiveRender())
    Add-Check -Ctx $Ctx -Name 'at least one active audio output' -Passed ($endpoints.Count -gt 0) `
        -Detail (($endpoints | ForEach-Object { $_.Name }) -join ', ')
    $default = [AmpifyAudio.Endpoints]::DefaultRender()
    Add-Check -Ctx $Ctx -Name 'default audio output' -Passed ($null -ne $default) `
        -Detail $(if ($default) { $default.Name } else { 'none' })
    if (-not $default) { return }

    $state = [AmpifyAudio.Endpoints]::Spatial()
    Add-Info -Ctx $Ctx -Name 'ISpatialAudioClient' -Detail (Get-SpatialAudioSummary)
    foreach ($line in Invoke-SpatialSoundConfig) { Add-Info -Ctx $Ctx -Name 'spatial sound (WinRT)' -Detail $line }
    if (-not $state.Error -and $state.MetadataStreamHr -ne 0) {
        Add-Info -Ctx $Ctx -Name 'Dolby Atmos' -Detail ("no metadata stream on this output: Apple Music will play Atmos " +
            "another way (spatial objects or stereo), the kernel log will say which")
    }
}
