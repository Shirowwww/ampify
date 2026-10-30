# Rendu natif des interfaces (M8) dans le vrai Apple Music, avec le build M8 de banc (AMK_XAML=ON, AMK_BENCH=ON) :
#   1. le noyau obtient la fenêtre principale par AMP.Utils (GetAppProperty) et sa DispatcherQueue exécute du travail ;
#   2. l'extension d'exemple react-panel, démarrée à chaud par reload.txt (comme amk extension enable), monte son
#      panneau ; UI Automation trouve ses éléments « AMK_… » dans la fenêtre d'Apple Music ;
#   3. un clic sur « Plus un » par UI Automation met le panneau à jour sur place ;
#   4. le thème minuit s'applique par-dessus ;
#   5. Réglages > Lecture, ouvert par UI Automation si possible, montre la carte « Mods (Apple Music Mod Kit) ».
# Une capture PNG de la fenêtre est prise à chaque étape. Les lignes utiles du journal sont copiées dans le log du job.
# Job : MSVC (bench-msvc), avec le kit MSVC (artefact amk-msvc-kit) dans $script:BenchKitRoot\m8 (amk_launcher.exe, amk_kernel.dll,
# themes\examples, examples\react-panel). Placement des panneaux et des boutons : provisoire, voir
# docs/tests/M8-interface.md.

# UI Automation de .NET (WindowsDesktop), chargé une fois. $false quand il n'est pas disponible.
function Initialize-M8Uia {
    try {
        Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes -ErrorAction Stop
        return $true
    } catch {
        Write-Host "  UI Automation indisponible : $($_.Exception.Message)"
        return $false
    }
}

# L'élément UI Automation de la fenêtre principale d'Apple Music, ou $null.
function Get-M8AppWindow {
    $process = Get-Process -Name 'AppleMusic' -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero } |
        Select-Object -First 1
    if (-not $process) { return $null }
    try {
        return [System.Windows.Automation.AutomationElement]::FromHandle($process.MainWindowHandle)
    } catch {
        return $null
    }
}

# Le premier descendant dont l'AutomationId vaut $Id, ou $null.
function Find-M8ById {
    param($Root, [string]$Id)
    if (-not $Root) { return $null }
    $condition = New-Object System.Windows.Automation.PropertyCondition ([System.Windows.Automation.AutomationElement]::AutomationIdProperty), $Id
    try {
        return $Root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $condition)
    } catch {
        return $null
    }
}

# Le premier descendant dont le nom correspond à $Pattern (expression régulière), parmi les types de contrôle donnés.
function Find-M8ByName {
    param($Root, [string]$Pattern)
    if (-not $Root) { return $null }
    try {
        $all = $Root.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)
    } catch {
        return $null
    }
    foreach ($element in $all) {
        $name = [string]$element.Current.Name
        if ($name -match $Pattern) { return $element }
    }
    return $null
}

# Les AutomationId « AMK_… » de la fenêtre (au plus 40), pour le rapport.
function Get-M8KitIds {
    param($Root)
    if (-not $Root) { return @() }
    try {
        $all = $Root.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)
    } catch {
        return @()
    }
    $ids = @()
    foreach ($element in $all) {
        $id = [string]$element.Current.AutomationId
        if ($id.StartsWith('AMK_')) { $ids += $id }
        if ($ids.Count -ge 40) { break }
    }
    return $ids
}

# Active un élément : Invoke, sinon sélection, sinon Toggle. Renvoie le moyen utilisé, ou '' si rien n'a marché.
function Invoke-M8Element {
    param($Element)
    if (-not $Element) { return '' }
    $patterns = @(
        @{ Pattern = [System.Windows.Automation.InvokePattern]::Pattern; Name = 'Invoke'; Action = { param($p) $p.Invoke() } },
        @{ Pattern = [System.Windows.Automation.SelectionItemPattern]::Pattern; Name = 'Select'; Action = { param($p) $p.Select() } },
        @{ Pattern = [System.Windows.Automation.TogglePattern]::Pattern; Name = 'Toggle'; Action = { param($p) $p.Toggle() } }
    )
    foreach ($entry in $patterns) {
        $object = $null
        try {
            if ($Element.TryGetCurrentPattern($entry.Pattern, [ref]$object)) {
                & $entry.Action $object
                return $entry.Name
            }
        } catch {
        }
    }
    return ''
}

# Met Apple Music au premier plan, pour les captures et les raccourcis clavier.
function Show-M8AppWindow {
    try {
        if (-not ('AmkBench.Foreground' -as [type])) {
            Add-Type -Namespace AmkBench -Name Foreground -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(System.IntPtr window);
[DllImport("user32.dll")] public static extern bool ShowWindow(System.IntPtr window, int command);
'@
        }
        $process = Get-Process -Name 'AppleMusic' -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero } |
            Select-Object -First 1
        if ($process) {
            [AmkBench.Foreground]::ShowWindow($process.MainWindowHandle, 3) | Out-Null  # SW_MAXIMIZE
            [AmkBench.Foreground]::SetForegroundWindow($process.MainWindowHandle) | Out-Null
        }
    } catch {
    }
}

# Les lignes du journal d'AppleMusic.exe qui parlent de l'interface, des réglages, des thèmes ou de react-panel.
function Write-M8JournalLines {
    $content = Read-Journal 'AppleMusic.log'
    Write-Host '----- AppleMusic.log : lignes Interface, Réglages, Thèmes et react-panel'
    if (-not $content) {
        Write-Host '  (absent ou vide)'
        return
    }
    $content -split "`r?`n" | Where-Object { $_ -match '\] (Interface|Réglages|Thèmes|Thème|Hôte|Extension react-panel|\[react-panel\]|Démarrage demandé)' } |
        Select-Object -First 120 | ForEach-Object { Write-Host "  $_" }
}

function Set-M8Settings {
    param([string]$Path, [hashtable]$Values)
    $lines = @()
    if (Test-Path -LiteralPath $Path) { $lines = @(Get-Content -LiteralPath $Path -Encoding UTF8) }
    foreach ($key in $Values.Keys) {
        $pattern = '^\s*' + [regex]::Escape($key) + '\s*='
        $lines = @($lines | Where-Object { $_ -notmatch $pattern })
        $lines += "$key = $($Values[$key])"
    }
    [System.IO.File]::WriteAllLines($Path, [string[]]$lines, [System.Text.UTF8Encoding]::new($false))
}

Register-Case -Id 'm8-interface' -Title 'M8 : rendu natif d''un panneau React, clic, thème et carte Mods dans Réglages > Lecture' -Requires 'apple-music' -Action {
    param($Ctx)

    $m8 = Join-Path $script:BenchKitRoot 'm8'
    $launcher = Join-Path $m8 'amk_launcher.exe'
    if (-not (Test-Path -LiteralPath $launcher)) {
        Add-Skip -Ctx $Ctx -Name 'build M8 présent' -Reason "pas de build M8 dans ce banc (MinGW ne compile pas XAML) : $launcher"
        return
    }
    $example = Join-Path $m8 'examples\react-panel'
    if (-not (Test-Path -LiteralPath $example)) { $example = Join-Path $repo 'examples\react-panel' }
    $minuit = Join-Path $m8 'themes\examples\minuit'

    # Le kit part propre : l'extension est installée mais coupée, pour capturer l'interface avant le montage.
    Reset-KitState
    New-Item -ItemType Directory -Force -Path (Join-Path $script:KitDir 'extensions') | Out-Null
    $extensionDir = Join-Path $script:KitDir 'extensions\react-panel'
    Remove-Item -LiteralPath $extensionDir -Recurse -Force -ErrorAction SilentlyContinue
    Copy-Item -LiteralPath $example -Destination $extensionDir -Recurse -Force
    if (Test-Path -LiteralPath $minuit) {
        New-Item -ItemType Directory -Force -Path (Join-Path $script:KitDir 'themes') | Out-Null
        Remove-Item -LiteralPath (Join-Path $script:KitDir 'themes\minuit') -Recurse -Force -ErrorAction SilentlyContinue
        Copy-Item -LiteralPath $minuit -Destination (Join-Path $script:KitDir 'themes\minuit') -Recurse -Force
    }
    $settings = Join-Path $script:KitDir 'settings.txt'
    $reload = Join-Path $script:KitDir 'reload.txt'
    Remove-Item -LiteralPath $reload -Force -ErrorAction SilentlyContinue
    Set-M8Settings -Path $settings -Values @{
        'extensions.react-panel.enabled' = 'false'
        'theme.active' = 'none'
        'theme.inspect' = '0'
        'expose_api' = '1'
    }

    $uia = Initialize-M8Uia
    $since = Get-Date
    $launcherProcess = Start-Launcher -LauncherPath $launcher
    try {
        $launched = Wait-Journal -FileName 'launcher.log' -Text 'Apple Music démarré' -TimeoutSeconds 120
        Add-Check -Ctx $Ctx -Name 'Apple Music démarré par le lanceur' -Passed $launched
        if (-not $launched) { return }

        # 1. La fenêtre et la DispatcherQueue.
        $window = Wait-Journal -FileName 'AppleMusic.log' -Text 'Interface : fenêtre principale obtenue par AMP.Utils' -TimeoutSeconds 90
        Add-Check -Ctx $Ctx -Name 'journal : fenêtre principale obtenue par AMP.Utils (GetAppProperty)' -Passed $window
        $ran = Wait-Until -TimeoutSeconds 60 -Condition {
            (Test-JournalContains -FileName 'AppleMusic.log' -Text 'Interface : premier travail exécuté par la DispatcherQueue') -or
            (Test-JournalContains -FileName 'AppleMusic.log' -Text 'premier rafraîchissement exécuté par sa DispatcherQueue')
        }
        Add-Check -Ctx $Ctx -Name 'journal : la DispatcherQueue a exécuté du travail' -Passed $ran
        $bound = Wait-Journal -FileName 'AppleMusic.log' -Text 'Interface : fenêtre « MainWindow » liée' -TimeoutSeconds 60
        Add-Check -Ctx $Ctx -Name 'journal : service d''interface lié à « MainWindow »' -Passed $bound
        Start-Sleep -Seconds 5
        Show-M8AppWindow
        Start-Sleep -Seconds 2
        Save-Screenshot -Ctx $Ctx -Name 'm8-avant-montage' | Out-Null

        # 2. Démarrage à chaud de react-panel, comme amk extension enable, puis montage du panneau.
        Set-M8Settings -Path $settings -Values @{ 'extensions.react-panel.enabled' = 'true' }
        [System.IO.File]::AppendAllText($reload, "start react-panel`n", [System.Text.UTF8Encoding]::new($false))
        $mounted = Wait-Journal -FileName 'AppleMusic.log' -Text 'Interface : panneau « react-panel/react-panel » monté' -TimeoutSeconds 60
        Add-Check -Ctx $Ctx -Name 'journal : panneau « react-panel/react-panel » monté' -Passed $mounted
        Start-Sleep -Seconds 3
        Show-M8AppWindow
        Start-Sleep -Seconds 1
        Save-Screenshot -Ctx $Ctx -Name 'm8-panneau-monte' | Out-Null

        $root = $null
        if ($uia) { $root = Get-M8AppWindow }
        if (-not $uia -or -not $root) {
            Add-Skip -Ctx $Ctx -Name 'UI Automation : éléments AMK_ dans la fenêtre' -Reason 'UI Automation ou fenêtre principale indisponible'
        } else {
            $title = Find-M8ById -Root $root -Id 'AMK_react-panel_panel-react-panel.0'
            $ids = @(Get-M8KitIds -Root $root)
            Add-Check -Ctx $Ctx -Name 'UI Automation : AMK_react-panel_panel-react-panel.0 (titre du panneau) trouvé' -Passed ($null -ne $title) `
                -Detail "$(if ($title) { 'nom : ' + $title.Current.Name } else { 'absent' }) ; AMK_ vus : $($ids.Count) ($(($ids | Select-Object -First 8) -join ', '))"

            # 3. Un clic sur « Plus un » : le compteur passe à 1, le panneau est mis à jour sur place.
            $plus = Find-M8ById -Root $root -Id 'AMK_react-panel_panel-react-panel.2.1'
            $how = Invoke-M8Element -Element $plus
            if ($how) {
                $updated = Wait-Journal -FileName 'AppleMusic.log' -Text 'Interface : panneau « react-panel/react-panel » mis à jour sur place' -TimeoutSeconds 20
                Add-Check -Ctx $Ctx -Name 'clic « Plus un » (UI Automation) : panneau mis à jour sur place' -Passed $updated -Detail "par $how"
                Start-Sleep -Seconds 1
                $counter = Find-M8ById -Root $root -Id 'AMK_react-panel_panel-react-panel.2.0'
                $text = if ($counter) { [string]$counter.Current.Name } else { '' }
                Add-Check -Ctx $Ctx -Name 'compteur affiché : « Clics : 1 »' -Passed ($text -eq 'Clics : 1') -Detail "lu : « $text »"
                Save-Screenshot -Ctx $Ctx -Name 'm8-panneau-clic' | Out-Null
            } else {
                Add-Info -Ctx $Ctx -Name 'clic « Plus un »' -Detail 'bouton AMK_react-panel_panel-react-panel.2.1 introuvable ou sans motif Invoke'
            }
        }

        # 4. Le thème minuit par-dessus le panneau.
        if (Test-Path -LiteralPath (Join-Path $script:KitDir 'themes\minuit')) {
            Set-M8Settings -Path $settings -Values @{ 'theme.active' = 'minuit'; 'theme.scheme' = 'Dark' }
            $themed = Wait-Until -TimeoutSeconds 60 -Condition {
                $null -ne (Find-JournalMatch -FileName 'AppleMusic.log' -Pattern 'Thème « minuit » : \d+ valeur\(s\) appliquée\(s\)')
            }
            Add-Check -Ctx $Ctx -Name 'thème minuit appliqué avec le panneau monté' -Passed $themed
            Start-Sleep -Seconds 2
            Show-M8AppWindow
            Save-Screenshot -Ctx $Ctx -Name 'm8-theme-applique' | Out-Null
            Set-M8Settings -Path $settings -Values @{ 'theme.active' = 'none' }
            Wait-Journal -FileName 'AppleMusic.log' -Text 'Thème retiré : ' -TimeoutSeconds 30 | Out-Null
        } else {
            Add-Skip -Ctx $Ctx -Name 'thème minuit' -Reason "thème d'exemple absent du build M8 : $minuit"
        }

        # 5. Réglages > Lecture et la carte Mods. Ctrl+, ouvre les réglages dans beaucoup d'apps ; sinon on cherche une
        # entrée « Settings », « Réglages » ou « Paramètres ». Puis l'entrée « Playback » ou « Lecture ».
        $settingsOpened = ''
        if ($uia -and $root) {
            Show-M8AppWindow
            Start-Sleep -Seconds 1
            $entry = Find-M8ByName -Root $root -Pattern '^(Settings|Réglages|Paramètres)(…|\.\.\.)?$'
            $settingsOpened = Invoke-M8Element -Element $entry
            if (-not $settingsOpened) {
                try {
                    Add-Type -AssemblyName System.Windows.Forms
                    [System.Windows.Forms.SendKeys]::SendWait('^,')
                    $settingsOpened = 'Ctrl+,'
                } catch {
                }
            }
            Start-Sleep -Seconds 3
            $root = Get-M8AppWindow
            $playback = Find-M8ByName -Root $root -Pattern '^(Playback|Lecture)$'
            $playbackHow = Invoke-M8Element -Element $playback
            Add-Info -Ctx $Ctx -Name 'navigation UI Automation vers Réglages > Lecture' `
                -Detail "réglages : $(if ($settingsOpened) { $settingsOpened } else { 'introuvable' }) ; Lecture : $(if ($playbackHow) { $playbackHow } else { 'introuvable' })"
        }
        $card = Wait-Until -TimeoutSeconds 30 -Condition {
            (Test-JournalContains -FileName 'AppleMusic.log' -Text 'Réglages : carte « Mods (Apple Music Mod Kit) » ajoutée') -or
            (Test-JournalContains -FileName 'AppleMusic.log' -Text 'Réglages : carte « Mods (Apple Music Mod Kit) » rafraîchie')
        }
        $pageSeen = Test-JournalContains -FileName 'AppleMusic.log' -Text 'Réglages : page PlaybackSettingsPage repérée'
        if ($card) {
            Add-Check -Ctx $Ctx -Name 'journal : carte Mods ajoutée à Réglages > Lecture' -Passed $true
            Start-Sleep -Seconds 2
            $root = Get-M8AppWindow
            $button = Find-M8ById -Root $root -Id 'AMK_ModsOpenFolder'
            $toggle = Find-M8ById -Root $root -Id 'AMK_ModsToggle_react-panel'
            Add-Check -Ctx $Ctx -Name 'UI Automation : bouton « Ouvrir le dossier du kit » et interrupteur react-panel' `
                -Passed (($null -ne $button) -and ($null -ne $toggle))
        } else {
            # Opening the settings by automation is a bonus: without it, the card is checked on the PC.
            Add-Info -Ctx $Ctx -Name 'carte Mods' -Detail "non vue (page Lecture repérée : $pageSeen) : à vérifier sur le PC"
        }
        Show-M8AppWindow
        Save-Screenshot -Ctx $Ctx -Name 'm8-reglages-mods' | Out-Null
    } finally {
        Write-M8JournalLines
        $closed = Close-AppleMusic
        $exited = $launcherProcess.WaitForExit(15000)
        Add-Check -Ctx $Ctx -Name 'lanceur : s''arrête quand Apple Music se ferme' -Passed ($closed -and $exited)
        $crashes = @(Get-AppCrashEvents -Since $since)
        Add-Check -Ctx $Ctx -Name 'aucun plantage d''Apple Music' -Passed ($crashes.Count -eq 0) `
            -Detail "$($crashes.Count) événement(s) : $(Format-CrashEvents -Events $crashes)"
        Copy-JournalToResults -Ctx $Ctx
        Remove-Item -LiteralPath $extensionDir -Recurse -Force -ErrorAction SilentlyContinue
        Set-M8Settings -Path $settings -Values @{ 'extensions.react-panel.enabled' = 'false'; 'theme.active' = 'none' }
    }
}
