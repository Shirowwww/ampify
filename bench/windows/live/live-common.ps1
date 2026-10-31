# Shared helpers of the live bench (signed-in Apple Music, real playback, UI). Loaded after harness.ps1 and uia.ps1.
# Element names are matched in English and French: the runner's Windows is in English, a local PC may be in French.

Set-StrictMode -Version Latest

# Catalog items for the playback test, checked on music.apple.com (fr and us storefronts) on 2026-10-30:
# - stereo: Dave Brubeck, "Take Five" (Dave Brubeck's Greatest Hits), album badges dolbyAtmos=false.
# - Dolby Atmos: Billie Eilish, "bad guy" (WHEN WE ALL FALL ASLEEP, WHERE DO WE GO?), album badges dolbyAtmos=true.
$script:LiveTracks = @(
    [pscustomobject]@{ Kind = 'stereo'; Title = 'Take Five'; Artist = 'Dave Brubeck'; Album = '157427923'; Track = '157427932'; Slug = 'take-five' },
    [pscustomobject]@{ Kind = 'atmos'; Title = 'bad guy'; Artist = 'Billie Eilish'; Album = '1450695723'; Track = '1450695739'; Slug = 'bad-guy' }
)

$script:Patterns = @{
    SignIn = '^(Sign In|Sign in|Se connecter|Connexion|Identifiez-vous)$'
    Ready = '^(Search|Rechercher|Home|Accueil|Listen Now|Écouter|Browse|Explorer|Parcourir|Radio|Library|Bibliothèque)$'
    Onboarding = '^(Continue|Continuer|Agree|Accepter|J.accepte|Get Started|Commencer|OK)$'
    Play = '^(Play|Lire|Lecture)$'
    Pause = '^(Pause)$'
    Settings = '^(Settings|Réglages|Paramètres|Preferences|Préférences)$'
    Playback = '^(Playback|Lecture)$'
    Subscribe = 'Try It Free|Try it free|Essai gratuit|Join Apple Music|Rejoindre Apple Music|S.abonner|Subscribe'
}

# Waits for Apple Music's main window to show its navigation. Returns the main window element or $null.
# Apple Music's first launch shows dialogs before the main window: the codec licence (MPEG LA, buttons Agree /
# Decline / Save As), then possibly a welcome screen. They are accepted like a new user would. Returns $true when a
# dialog was handled, so callers may wait a moment for the next one.
function Close-FirstRunDialogs {
    $licence = Find-Element -NamePattern 'MPEGLA|MPEG LA|mpegla' -VisibleOnly
    $agree = Find-Element -ControlType 'Button' -NamePattern '^(Agree|Accepter|J''accepte|I Agree|Accept)$' -VisibleOnly
    if ($agree -and ($licence -or (Find-Element -ControlType 'Button' -NamePattern '^(Decline|Refuser)$' -VisibleOnly))) {
        Write-Host '  first launch: licence accepted (Agree)'
        Invoke-Click -Element $agree | Out-Null
        Start-Sleep -Seconds 3
        return $true
    }
    $welcome = Find-Element -ControlType 'Button' -NamePattern '^(Start Listening|Commencer l.écoute|Continue|Continuer|Get Started|Commencer|Not Now|Plus tard|Pas maintenant)$' -VisibleOnly
    if ($welcome -and -not (Find-Element -NamePattern $script:Patterns.Ready)) {
        Write-Host "  first launch: welcome screen passed ($(Format-Element $welcome))"
        Invoke-Click -Element $welcome | Out-Null
        Start-Sleep -Seconds 3
        return $true
    }
    return $false
}

function Wait-AppleMusicReady {
    param([int]$TimeoutSeconds = 90)
    $ready = Wait-Until -TimeoutSeconds $TimeoutSeconds -PollSeconds 3 -Condition {
        Close-FirstRunDialogs | Out-Null  # its $true/$false must not join the condition's output
        $null -ne (Find-Element -NamePattern $script:Patterns.Ready)
    }
    if (-not $ready) { return $null }
    return Get-UiaMainWindow
}

# 'yes' (no sign-in button and the app is ready), 'no' (a sign-in button is shown) or 'unknown'.
function Test-AppleMusicSignedIn {
    $signIn = Find-Element -NamePattern $script:Patterns.SignIn -Where {
        param($e) $e.Current.ControlType -in @([System.Windows.Automation.ControlType]::Button, [System.Windows.Automation.ControlType]::Hyperlink,
            [System.Windows.Automation.ControlType]::ListItem, [System.Windows.Automation.ControlType]::MenuItem)
    }
    if ($signIn) { return 'no' }
    if (Find-Element -NamePattern $script:Patterns.Ready) { return 'yes' }
    return 'unknown'
}

# Apple Music's own volume slider, or $null. Ampify never moves it; the case only reads it.
function Get-VolumeSlider {
    return Find-Element -ControlType 'Slider' -NamePattern 'Volume'
}

# Opens a catalog page in the running app (music:// link). The page opens; playback does not start by itself.
function Open-AppleMusicPage {
    param([Parameter(Mandatory)]$Track, [string]$Storefront = 'fr')
    $url = "music://music.apple.com/$Storefront/album/$($Track.Slug)/$($Track.Album)?i=$($Track.Track)"
    Start-Process -FilePath $url
    return $url
}

# Starts a track from its page: double-click on its row, then the page's Play button, then the search box.
# Returns how it started it, or $null.
function Start-TrackPlayback {
    param([Parameter(Mandatory)]$Track, [string]$Storefront = 'fr', [scriptblock]$Started)
    Open-AppleMusicPage -Track $Track -Storefront $Storefront | Out-Null
    Start-Sleep -Seconds 6
    Show-AppleMusicWindow | Out-Null
    $title = [regex]::Escape($Track.Title)

    $row = Find-Element -NamePattern "(^|\W)$title(\W|$)" -TimeoutSeconds 20 -VisibleOnly -Where {
        param($e) $e.Current.ControlType -ne [System.Windows.Automation.ControlType]::Window
    }
    if ($row) {
        Invoke-Click -Element $row -Double | Out-Null
        if (Wait-Until -TimeoutSeconds 20 -Condition $Started) { return "double click on `"$($Track.Title)`"" }
    }

    # The page's own Play button is the one highest on screen (the player bar sits at the bottom).
    $plays = @(Find-Element -NamePattern $script:Patterns.Play -ControlType 'Button' -All -VisibleOnly)
    $top = $plays | Sort-Object { $_.Current.BoundingRectangle.Y } | Select-Object -First 1
    if ($top) {
        Invoke-Click -Element $top | Out-Null
        if (Wait-Until -TimeoutSeconds 20 -Condition $Started) { return 'the page''s Play button' }
    }

    $search = Find-Element -ControlType 'Edit' -NamePattern '^(Search|Rechercher)'
    if ($search) {
        Set-Text -Element $search -Text "$($Track.Artist) $($Track.Title)" | Out-Null
        Send-Key Enter
        Start-Sleep -Seconds 5
        $hit = Find-Element -NamePattern "(^|\W)$title(\W|$)" -TimeoutSeconds 15 -VisibleOnly -Where {
            param($e) $e.Current.ControlType -ne [System.Windows.Automation.ControlType]::Edit
        }
        if ($hit) {
            Invoke-Click -Element $hit -Double | Out-Null
            if (Wait-Until -TimeoutSeconds 20 -Condition $Started) { return 'search, then double click' }
        }
    }
    return $null
}

# Opens Apple Music's Settings: the Settings entry of the sidebar or of the account menu, else Ctrl+, (the shortcut of
# Apple's apps). Returns the element tree root holding the Settings page, or $null.
function Open-AppleMusicSettings {
    Show-AppleMusicWindow | Out-Null
    $entry = Find-Element -NamePattern $script:Patterns.Settings -TimeoutSeconds 5
    if (-not $entry) {
        # The account / "more" menu usually holds Settings.
        $menu = Find-Element -ControlType 'Button' -NamePattern '^(More|Plus|Account|Compte|\.\.\.|…)' -TimeoutSeconds 3
        if ($menu) {
            Invoke-Click -Element $menu | Out-Null
            $entry = Find-Element -NamePattern $script:Patterns.Settings -TimeoutSeconds 5
        }
    }
    if ($entry) {
        Invoke-Click -Element $entry | Out-Null
    } else {
        [AmpifyInput]::SendChord([uint16[]]@(0x11, 0xBC))  # Ctrl + ,
    }
    Start-Sleep -Seconds 3
    if (Find-Element -NamePattern $script:Patterns.Playback -TimeoutSeconds 10) { return Get-UiaMainWindow }
    return $null
}

# Sets Apple Music's own "Dolby Atmos" playback setting to "Always On" through its Settings, as a user would: on a
# device Apple Music does not know as Atmos-capable, "Automatic" plays the stereo version. Best effort; returns a
# sentence for the report.
function Set-AppleMusicAtmosAlwaysOn {
    $settings = Open-AppleMusicSettings
    if (-not $settings) { return 'Settings not found' }
    $playback = Find-Element -NamePattern $script:Patterns.Playback -VisibleOnly
    if ($playback) { Invoke-Click -Element $playback | Out-Null; Start-Sleep -Seconds 2 }
    $combo = Find-Element -ControlType 'ComboBox' -VisibleOnly -Where { param($e) $e.Current.Name -match 'Atmos' -or $e.Current.AutomationId -match 'Atmos' }
    if (-not $combo) {
        Send-Key Escape
        return '"Dolby Atmos" list not found on the Playback page'
    }
    $expand = $null
    if ($combo.TryGetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern, [ref]$expand)) {
        $expand.Expand()
    } else {
        Invoke-Click -Element $combo -Mouse | Out-Null
    }
    Start-Sleep -Seconds 1
    $choice = Find-Element -ControlType 'ListItem' -NamePattern '^(Always On|Always on|Toujours activé|Toujours activée|Toujours)$' -TimeoutSeconds 5
    $result = if ($choice) { "`"$($choice.Current.Name)`" chosen ($(Invoke-Click -Element $choice))" } else { '"Always On" choice not found' }
    Start-Sleep -Seconds 1
    if ($expand) { try { $expand.Collapse() } catch { } }
    $selection = $null
    if ($combo.TryGetCurrentPattern([System.Windows.Automation.SelectionPattern]::Pattern, [ref]$selection)) {
        $current = @($selection.Current.GetSelection() | ForEach-Object { $_.Current.Name }) -join ', '
        $result += "; value shown: $current"
    }
    Send-Key Escape
    Start-Sleep -Seconds 1
    return $result
}
