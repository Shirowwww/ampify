# Real playback with Ampify injected (bench build) and the Atmos leveler on: a stereo track, then a Dolby Atmos track
# (live-common.ps1, $script:LiveTracks), about 45 s each, then the stereo track again for a few seconds so the Atmos
# measurement closes. The checks read the kernel logs; the log lines are printed in the job log.
# Nothing is recorded: the leveler measures in memory and logs numbers. Ampify never moves the volume slider:
# the case reads the slider before and after, and only Apple Music's own UI could have changed it.

Register-Case -Id 'playback-with-kit' -Title 'Stereo then Dolby Atmos playback with Ampify and the leveler' -Requires 'apple-account' -Action {
    param($Ctx)

    $launcher = Join-Path $script:BenchKitRoot 'bench\ampify_launcher.exe'
    if (-not (Test-Path -LiteralPath $launcher)) {
        Add-Check -Ctx $Ctx -Name 'bench build present' -Passed $false -Detail $launcher
        return
    }
    $playSeconds = if ($env:AMPIFY_PLAY_SECONDS) { [int]$env:AMPIFY_PLAY_SECONDS } else { 45 }

    $settings = Join-Path $env:USERPROFILE 'Ampify\settings.txt'
    $previous = if (Test-Path -LiteralPath $settings) { Get-Content -LiteralPath $settings -Raw -Encoding UTF8 } else { $null }
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    $trackPattern = 'Atmos: (stereo track|Dolby Atmos track), variant'
    $countTracks = { @([regex]::Matches([string](Read-Journal 'AMPLibraryAgent.log'), $trackPattern)).Count }

    try {
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $settings) | Out-Null
        $base = if ($previous) { $previous.TrimEnd("`r", "`n") + "`r`n" } else { '' }
        [System.IO.File]::WriteAllText($settings, $base + "extensions.atmos-leveler.enabled = 1`r`n", $utf8)

        Reset-KitState
        $since = Get-Date
        $launcherProcess = Start-Launcher -LauncherPath $launcher
        $launched = Wait-Journal -FileName 'launcher.log' -Text 'Apple Music started' -TimeoutSeconds 120
        Add-Check -Ctx $Ctx -Name 'Apple Music started by the launcher (bench build)' -Passed $launched
        if (-not $launched) { Write-JournalTail -FileName 'launcher.log'; return }

        $main = Wait-AppleMusicReady -TimeoutSeconds 120
        Add-Check -Ctx $Ctx -Name 'main window ready' -Passed ($null -ne $main)
        if (-not $main) { Save-Screenshot -Ctx $Ctx -Name '01-startup' | Out-Null; return }
        Show-AppleMusicWindow | Out-Null
        Start-Sleep -Seconds 5
        $signed = Test-AppleMusicSignedIn
        Add-Check -Ctx $Ctx -Name 'still signed in with Ampify injected' -Passed ($signed -ne 'no') -Detail $signed

        # Apple Music's own setting, changed in its UI like a user would. Nothing in Ampify touches it.
        Add-Info -Ctx $Ctx -Name 'Apple Music Dolby Atmos setting: Always on' -Detail (Set-AppleMusicAtmosAlwaysOn)
        Show-AppleMusicWindow | Out-Null

        $slider = Get-VolumeSlider
        $volumeBefore = if ($slider) { Get-RangeValue $slider } else { $null }
        Add-Info -Ctx $Ctx -Name 'Apple Music volume slider at the start' -Detail $(if ($null -ne $volumeBefore) { "$volumeBefore" } else { 'slider not found' })

        $sequence = @($script:LiveTracks[0], $script:LiveTracks[1], $script:LiveTracks[0])
        for ($step = 0; $step -lt $sequence.Count; $step++) {
            $track = $sequence[$step]
            $last = ($step -eq $sequence.Count - 1)
            $before = & $countTracks
            $how = Start-TrackPlayback -Track $track -Storefront $script:LiveStorefront -Started {
                (& $countTracks) -gt $before -or $null -ne (Find-Element -ControlType 'Button' -NamePattern $script:Patterns.Pause -VisibleOnly)
            }
            $label = if ($last) { "$($track.Kind) (end of the Atmos measurement)" } else { $track.Kind }
            Add-Check -Ctx $Ctx -Name "playback started: $label, `"$($track.Title)`" by $($track.Artist)" -Passed ($null -ne $how) `
                -Detail $(if ($how) { $how } else { 'neither a log line nor a Pause button: no subscription, track unavailable or unknown interface' })
            if (-not $how) {
                Write-VisibleSummary "starting `"$($track.Title)`""
                continue
            }
            if ($last) {
                Start-Sleep -Seconds 15
            } else {
                Start-Sleep -Seconds ([Math]::Max(5, $playSeconds - 10))
                Save-Screenshot -Ctx $Ctx -Name $(if ($track.Kind -eq 'atmos') { '04-playback-atmos' } else { '03-playback-stereo' }) | Out-Null
                Start-Sleep -Seconds 10
            }
        }

        $slider = Get-VolumeSlider
        $volumeAfter = if ($slider) { Get-RangeValue $slider } else { $null }
        if ($null -ne $volumeBefore -and $null -ne $volumeAfter) {
            Add-Check -Ctx $Ctx -Name 'volume slider unchanged while Ampify ran' `
                -Passed ([Math]::Abs($volumeAfter - $volumeBefore) -lt 0.001) -Detail "before $volumeBefore, after $volumeAfter"
        } else {
            Add-Skip -Ctx $Ctx -Name 'volume slider unchanged' -Reason 'slider not found by UI Automation'
        }

        $pause = Find-Element -ControlType 'Button' -NamePattern $script:Patterns.Pause -VisibleOnly
        if ($pause) { Invoke-Click -Element $pause | Out-Null }
        Close-AppleMusic | Out-Null
        $launcherProcess.WaitForExit(15000) | Out-Null

        # --- What the kernel logged in AMPLibraryAgent.exe ---
        $agent = [string](Read-Journal 'AMPLibraryAgent.log')
        $lines = @($agent -split "`r?`n" | Where-Object { $_ -match 'Atmos: |kernel|Kernel' })
        Write-Host "----- AMPLibraryAgent.log: leveler lines ($($lines.Count))"
        $lines | Select-Object -First 200 | ForEach-Object { Write-Host "  $(Protect-UiaText $_)" }
        Write-Host '----- end of the leveler lines'

        $find = { param($pattern) $m = [regex]::Match($agent, $pattern); if ($m.Success) { $m } else { $null } }
        $leveler = & $find 'Atmos: leveler started: (\d+) audio hook\(s\)'
        Add-Check -Ctx $Ctx -Name 'leveler started in AMPLibraryAgent.exe' -Passed ($null -ne $leveler) `
            -Detail $(if ($leveler) { $leveler.Value } else { 'line missing' })

        $stereo = & $find 'Atmos: stereo track, variant (\S+)'
        Add-Check -Ctx $Ctx -Name 'stereo track recognised (variant)' -Passed ($null -ne $stereo) -Detail $(if ($stereo) { $stereo.Groups[1].Value } else { '' })
        $stereoMeter = & $find 'Atmos: stereo measurement at (\d+) Hz'
        Add-Check -Ctx $Ctx -Name 'stereo measurement active' -Passed ($null -ne $stereoMeter) -Detail $(if ($stereoMeter) { $stereoMeter.Value } else { '' })
        $stereoLufs = & $find 'Atmos: stereo track measured \([^)]*\): (-?[\d.]+) LUFS over (\d+) s'
        Add-Check -Ctx $Ctx -Name 'LUFS of the stereo track' -Passed ($null -ne $stereoLufs) `
            -Detail $(if ($stereoLufs) { "$($stereoLufs.Groups[1].Value) LUFS over $($stereoLufs.Groups[2].Value) s" } else { 'line "stereo track measured" missing' })

        $atmos = & $find 'Atmos: Dolby Atmos track, variant (\S+), starting gain (-?[\d.]+) dB'
        $spatial = Get-SpatialAudioState
        if ($atmos) {
            Add-Check -Ctx $Ctx -Name 'Dolby Atmos detected (variant)' -Passed $true -Detail "$($atmos.Groups[1].Value), starting gain $($atmos.Groups[2].Value) dB"
        } elseif ($spatial.Error -or $spatial.MetadataStreamHr -ne 0) {
            Add-Info -Ctx $Ctx -Name 'Dolby Atmos detected' -Detail "no: the output does not offer the metadata stream ($(Get-SpatialAudioSummary)), Apple Music played the stereo version"
        } else {
            Add-Check -Ctx $Ctx -Name 'Dolby Atmos detected (variant)' -Passed $false -Detail 'metadata stream available but no Atmos track logged'
        }
        $metadataStream = & $find 'Atmos: spatial stream activated: ISpatialAudioObjectRenderStreamForMetadata[^\r\n]*'
        $metadataHooks = @($agent -split "`r?`n" | Where-Object { $_ -match 'hook ISpatialAudioObject(RenderStream)?ForMetadata\S* placed in' })
        $gain = & $find 'Atmos: gain (-?[\d.]+) -> (-?[\d.]+) dB'
        $atmosLufs = & $find 'Atmos: (measuring: (-?[\d.]+) LUFS[^\r\n]*|Atmos track measured[^\r\n]*)'
        if ($atmos) {
            Add-Check -Ctx $Ctx -Name 'ForMetadata stream seen' -Passed ($null -ne $metadataStream) -Detail $(if ($metadataStream) { $metadataStream.Value } else { '' })
            Add-Check -Ctx $Ctx -Name 'ForMetadata hooks placed' -Passed ($metadataHooks.Count -gt 0) -Detail "$($metadataHooks.Count) line(s)"
            Add-Check -Ctx $Ctx -Name 'Atmos measurement in LUFS' -Passed ($null -ne $atmosLufs) -Detail $(if ($atmosLufs) { $atmosLufs.Groups[1].Value } else { '' })
            Add-Info -Ctx $Ctx -Name 'gain applied' -Detail $(if ($gain) { $gain.Value } else { "starting gain only ($($atmos.Groups[2].Value) dB), no change logged" })
        }

        # Never a hook in FairPlay or its modules. The refusals ("refused") show the guard working and are fine.
        $all = [string](Read-Journal 'AppleMusic.log') + "`n" + $agent + "`n" + [string](Read-Journal 'launcher.log')
        $forbidden = @($all -split "`r?`n" | Where-Object { $_ -match 'placed in' -and $_ -match 'CoreFP|CoreADI64|FairPlay' })
        Add-Check -Ctx $Ctx -Name 'no hook on CoreFP, CoreADI64 or FairPlay' -Passed ($forbidden.Count -eq 0) -Detail ($forbidden -join ' | ')

        $crashes = @(Get-AppCrashEvents -Since $since)
        Add-Check -Ctx $Ctx -Name 'no Apple Music crash' -Passed ($crashes.Count -eq 0) `
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
