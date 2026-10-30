# Real playback with the kit injected (bench build) and the Atmos leveler on: a stereo track, then a Dolby Atmos track
# (live-common.ps1, $script:LiveTracks), about 45 s each, then the stereo track again for a few seconds so the Atmos
# measurement closes. The checks read the kernel journals; the journal lines are printed in the job log.
# Nothing is recorded: the leveler measures in memory and journals numbers. The kit never moves the volume slider:
# the test reads the slider before and after, and only Apple Music's own UI could have changed it.

Register-Case -Id 'lecture-kit' -Title 'Lecture stéréo puis Dolby Atmos avec le kit et le nivellement' -Requires 'apple-account' -Action {
    param($Ctx)

    $launcher = Join-Path $script:BenchKitRoot 'banc\amk_launcher.exe'
    if (-not (Test-Path -LiteralPath $launcher)) {
        Add-Check -Ctx $Ctx -Name 'build de banc présent' -Passed $false -Detail $launcher
        return
    }
    $playSeconds = if ($env:AMK_PLAY_SECONDS) { [int]$env:AMK_PLAY_SECONDS } else { 45 }

    $settings = Join-Path $env:USERPROFILE 'AppleMusicModKit\settings.txt'
    $previous = if (Test-Path -LiteralPath $settings) { Get-Content -LiteralPath $settings -Raw -Encoding UTF8 } else { $null }
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    $trackPattern = 'Atmos : piste (stéréo|Dolby Atmos), variante'
    $countTracks = { @([regex]::Matches([string](Read-Journal 'AMPLibraryAgent.log'), $trackPattern)).Count }

    try {
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $settings) | Out-Null
        $base = if ($previous) { $previous.TrimEnd("`r", "`n") + "`r`n" } else { '' }
        [System.IO.File]::WriteAllText($settings, $base + "extensions.atmos-leveler.enabled = 1`r`n", $utf8)

        Reset-KitState
        $since = Get-Date
        $launcherProcess = Start-Launcher -LauncherPath $launcher
        $launched = Wait-Journal -FileName 'launcher.log' -Text 'Apple Music démarré' -TimeoutSeconds 120
        Add-Check -Ctx $Ctx -Name 'Apple Music démarré par le lanceur (build de banc)' -Passed $launched
        if (-not $launched) { Write-JournalTail -FileName 'launcher.log'; return }

        $main = Wait-AppleMusicReady -TimeoutSeconds 120
        Add-Check -Ctx $Ctx -Name 'fenêtre principale prête' -Passed ($null -ne $main)
        if (-not $main) { Save-Screenshot -Ctx $Ctx -Name '01-demarrage' | Out-Null; return }
        Show-AppleMusicWindow | Out-Null
        Start-Sleep -Seconds 5
        $signed = Test-AppleMusicSignedIn
        Add-Check -Ctx $Ctx -Name 'toujours connecté avec le kit injecté' -Passed ($signed -ne 'non') -Detail $signed

        # Apple Music's own setting, changed in its UI like a user would. Nothing in the kit touches it.
        Add-Info -Ctx $Ctx -Name 'réglage Dolby Atmos d''Apple Music : Toujours activé' -Detail (Set-AppleMusicAtmosAlwaysOn)
        Show-AppleMusicWindow | Out-Null

        $slider = Get-VolumeSlider
        $volumeBefore = if ($slider) { Get-RangeValue $slider } else { $null }
        Add-Info -Ctx $Ctx -Name 'curseur de volume d''Apple Music au départ' -Detail $(if ($null -ne $volumeBefore) { "$volumeBefore" } else { 'curseur introuvable' })

        $sequence = @($script:LiveTracks[0], $script:LiveTracks[1], $script:LiveTracks[0])
        for ($step = 0; $step -lt $sequence.Count; $step++) {
            $track = $sequence[$step]
            $last = ($step -eq $sequence.Count - 1)
            $before = & $countTracks
            $how = Start-TrackPlayback -Track $track -Storefront $script:LiveStorefront -Started {
                (& $countTracks) -gt $before -or $null -ne (Find-Element -ControlType 'Button' -NamePattern $script:Patterns.Pause -VisibleOnly)
            }
            $label = if ($last) { "$($track.Kind) (fin de la mesure Atmos)" } else { $track.Kind }
            Add-Check -Ctx $Ctx -Name "lecture lancée : $label, « $($track.Title) » de $($track.Artist)" -Passed ($null -ne $how) `
                -Detail $(if ($how) { $how } else { 'ni journal ni bouton Pause : abonnement absent, morceau indisponible ou interface inconnue' })
            if (-not $how) {
                Write-VisibleSummary "lancement de « $($track.Title) »"
                continue
            }
            if ($last) {
                Start-Sleep -Seconds 15
            } else {
                Start-Sleep -Seconds ([Math]::Max(5, $playSeconds - 10))
                Save-Screenshot -Ctx $Ctx -Name $(if ($track.Kind -eq 'atmos') { '04-lecture-atmos' } else { '03-lecture-stereo' }) | Out-Null
                Start-Sleep -Seconds 10
            }
        }

        $slider = Get-VolumeSlider
        $volumeAfter = if ($slider) { Get-RangeValue $slider } else { $null }
        if ($null -ne $volumeBefore -and $null -ne $volumeAfter) {
            Add-Check -Ctx $Ctx -Name 'curseur de volume inchangé pendant que le kit tournait' `
                -Passed ([Math]::Abs($volumeAfter - $volumeBefore) -lt 0.001) -Detail "avant $volumeBefore, après $volumeAfter"
        } else {
            Add-Skip -Ctx $Ctx -Name 'curseur de volume inchangé' -Reason 'curseur introuvable par UI Automation'
        }

        $pause = Find-Element -ControlType 'Button' -NamePattern $script:Patterns.Pause -VisibleOnly
        if ($pause) { Invoke-Click -Element $pause | Out-Null }
        Close-AppleMusic | Out-Null
        $launcherProcess.WaitForExit(15000) | Out-Null

        # --- What the kernel journaled in AMPLibraryAgent.exe ---
        $agent = [string](Read-Journal 'AMPLibraryAgent.log')
        $lines = @($agent -split "`r?`n" | Where-Object { $_ -match 'Atmos : |noyau|Noyau' })
        Write-Host "----- AMPLibraryAgent.log : lignes du nivellement ($($lines.Count))"
        $lines | Select-Object -First 200 | ForEach-Object { Write-Host "  $(Protect-UiaText $_)" }
        Write-Host '----- fin des lignes du nivellement'

        $find = { param($pattern) $m = [regex]::Match($agent, $pattern); if ($m.Success) { $m } else { $null } }
        $leveler = & $find 'Atmos : nivellement démarré : (\d+) hook\(s\) audio'
        Add-Check -Ctx $Ctx -Name 'nivellement démarré dans AMPLibraryAgent.exe' -Passed ($null -ne $leveler) `
            -Detail $(if ($leveler) { $leveler.Value } else { 'ligne absente' })

        $stereo = & $find 'Atmos : piste stéréo, variante (\S+)'
        Add-Check -Ctx $Ctx -Name 'piste stéréo reconnue (variante)' -Passed ($null -ne $stereo) -Detail $(if ($stereo) { $stereo.Groups[1].Value } else { '' })
        $stereoMeter = & $find 'Atmos : mesure stéréo à (\d+) Hz'
        Add-Check -Ctx $Ctx -Name 'mesure stéréo active' -Passed ($null -ne $stereoMeter) -Detail $(if ($stereoMeter) { $stereoMeter.Value } else { '' })
        $stereoLufs = & $find 'Atmos : morceau stéréo mesuré \([^)]*\) : (-?[\d.]+) LUFS sur (\d+) s'
        Add-Check -Ctx $Ctx -Name 'LUFS du morceau stéréo' -Passed ($null -ne $stereoLufs) `
            -Detail $(if ($stereoLufs) { "$($stereoLufs.Groups[1].Value) LUFS sur $($stereoLufs.Groups[2].Value) s" } else { 'ligne « morceau stéréo mesuré » absente' })

        $atmos = & $find 'Atmos : piste Dolby Atmos, variante (\S+), gain de départ (-?[\d.]+) dB'
        $spatial = Get-SpatialAudioState
        if ($atmos) {
            Add-Check -Ctx $Ctx -Name 'Dolby Atmos détecté (variante)' -Passed $true -Detail "$($atmos.Groups[1].Value), gain de départ $($atmos.Groups[2].Value) dB"
        } elseif ($spatial.Error -or $spatial.MetadataStreamHr -ne 0) {
            Add-Info -Ctx $Ctx -Name 'Dolby Atmos détecté' -Detail "non : la sortie n'offre pas le flux à métadonnées ($(Get-SpatialAudioSummary)), Apple Music a joué la version stéréo"
        } else {
            Add-Check -Ctx $Ctx -Name 'Dolby Atmos détecté (variante)' -Passed $false -Detail 'flux à métadonnées disponible mais aucune piste Atmos journalisée'
        }
        $metadataStream = & $find 'Atmos : flux spatial activé : ISpatialAudioObjectRenderStreamForMetadata[^\r\n]*'
        $metadataHooks = @($agent -split "`r?`n" | Where-Object { $_ -match 'hook ISpatialAudioObject(RenderStream)?ForMetadata\S* posé dans' })
        $gain = & $find 'Atmos : gain (-?[\d.]+) -> (-?[\d.]+) dB'
        $atmosLufs = & $find 'Atmos : (mesure en cours : (-?[\d.]+) LUFS[^\r\n]*|morceau Atmos mesuré[^\r\n]*)'
        if ($atmos) {
            Add-Check -Ctx $Ctx -Name 'flux ForMetadata vu' -Passed ($null -ne $metadataStream) -Detail $(if ($metadataStream) { $metadataStream.Value } else { '' })
            Add-Check -Ctx $Ctx -Name 'hooks ForMetadata posés' -Passed ($metadataHooks.Count -gt 0) -Detail "$($metadataHooks.Count) ligne(s)"
            Add-Check -Ctx $Ctx -Name 'mesure Atmos en LUFS' -Passed ($null -ne $atmosLufs) -Detail $(if ($atmosLufs) { $atmosLufs.Groups[1].Value } else { '' })
            Add-Info -Ctx $Ctx -Name 'gain appliqué' -Detail $(if ($gain) { $gain.Value } else { "gain de départ seulement ($($atmos.Groups[2].Value) dB), pas de changement journalisé" })
        }

        # Never a hook in FairPlay or its modules. The refusals ("refusé") show the guard working and are fine.
        $all = [string](Read-Journal 'AppleMusic.log') + "`n" + $agent + "`n" + [string](Read-Journal 'launcher.log')
        $forbidden = @($all -split "`r?`n" | Where-Object { $_ -match 'posé dans' -and $_ -match 'CoreFP|CoreADI64|FairPlay' })
        Add-Check -Ctx $Ctx -Name 'aucun hook sur CoreFP, CoreADI64 ni FairPlay' -Passed ($forbidden.Count -eq 0) -Detail ($forbidden -join ' | ')

        $crashes = @(Get-AppCrashEvents -Since $since)
        Add-Check -Ctx $Ctx -Name 'aucun plantage d''Apple Music' -Passed ($crashes.Count -eq 0) `
            -Detail $(if ($crashes.Count) { Format-CrashEvents -Events $crashes } else { '' })
        Write-JournalTail -FileName 'launcher.log' -Lines 25
    } finally {
        if ($null -eq $previous) {
            Remove-Item -LiteralPath $settings -ErrorAction SilentlyContinue
        } else {
            [System.IO.File]::WriteAllText($settings, $previous, $utf8)
        }
    }
}
