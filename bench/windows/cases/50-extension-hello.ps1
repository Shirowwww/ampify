# Étapes 1 à 6 du test M7, sur le build de banc : l'extension hello-extension du dossier utilisateur démarre dans
# AppleMusic.exe, pas dans AMPLibraryAgent.exe, et son compteur de lancements est écrit dans le stockage.

Register-Case -Id 'extension-hello' -Title 'Extension hello-extension démarrée dans AppleMusic.exe, pas dans AMPLibraryAgent.exe' -Requires 'apple-music' -Action {
    param($Ctx)

    $launcher = Join-Path $script:BenchKitRoot 'banc\amk_launcher.exe'
    if (-not (Test-Path -LiteralPath $launcher)) {
        Add-Check -Ctx $Ctx -Name 'build de banc présent' -Passed $false -Detail $launcher
        return
    }

    $example = Join-Path $repo 'examples\hello-extension'
    $extensionDir = Join-Path $script:KitDir 'extensions\hello-extension'
    $settingsPath = Join-Path $script:KitDir 'settings.txt'
    $storagePath = Join-Path $script:KitDir 'storage\hello-extension.json'
    $settingLine = 'extensions.hello-extension.enabled = true'

    # Le stockage part de zéro : le compteur doit valoir exactement 1 après ce lancement.
    Reset-KitState
    Remove-Item -LiteralPath $storagePath -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $extensionDir) | Out-Null
    Copy-Item -LiteralPath $example -Destination $extensionDir -Recurse -Force
    [System.IO.File]::AppendAllText($settingsPath, "$settingLine`r`n", [System.Text.UTF8Encoding]::new($false))

    try {
        $since = Get-Date
        $launcherProcess = Start-Launcher -LauncherPath $launcher

        $launched = Wait-Journal -FileName 'launcher.log' -Text 'Apple Music démarré' -TimeoutSeconds 120
        Add-Check -Ctx $Ctx -Name 'Apple Music démarré par le lanceur' -Passed $launched
        if (-not $launched) {
            Close-AppleMusic | Out-Null
            return
        }

        $started = Wait-Journal -FileName 'AppleMusic.log' -Text 'Extension hello-extension démarrée' -TimeoutSeconds 60
        Add-Check -Ctx $Ctx -Name 'AppleMusic.log : hello-extension démarrée' -Passed $started

        $retained = Test-JournalContains -FileName 'AppleMusic.log' -Text 'Éléments JavaScript retenus : 1'
        Add-Check -Ctx $Ctx -Name 'AppleMusic.log : un élément JavaScript retenu' -Passed $retained

        $greeted = Wait-Journal -FileName 'AppleMusic.log' -Text '[hello-extension] Bonjour, lancement n° 1' -TimeoutSeconds 30
        Add-Check -Ctx $Ctx -Name 'AppleMusic.log : message de hello-extension' -Passed $greeted

        # L'exemple déclare "processes": ["ui"] : il ne doit rien écrire dans le journal de l'agent.
        $agentLog = Read-Journal 'AMPLibraryAgent.log'
        $inAgent = [bool]($agentLog -and $agentLog.Contains('hello-extension'))
        Add-Check -Ctx $Ctx -Name 'AMPLibraryAgent.log : aucune ligne de hello-extension' -Passed (-not $inAgent)

        $storageOk = (Test-Path -LiteralPath $storagePath) -and ((Get-Content -LiteralPath $storagePath -Raw) -match '"launches"\s*:\s*1\b')
        Add-Check -Ctx $Ctx -Name 'storage\hello-extension.json : compteur à 1' -Passed ([bool]$storageOk)
        Save-Screenshot -Ctx $Ctx -Name 'extension-hello' | Out-Null

        $closed = Close-AppleMusic
        $exited = $launcherProcess.WaitForExit(15000)
        Add-Check -Ctx $Ctx -Name 'lanceur : s''arrête quand Apple Music se ferme' -Passed ($closed -and $exited)

        $crashes = @(Get-AppCrashEvents -Since $since)
        Add-Check -Ctx $Ctx -Name 'aucun plantage d''Apple Music pendant le test M7' -Passed ($crashes.Count -eq 0) `
            -Detail "$($crashes.Count) événement(s) : $(Format-CrashEvents -Events $crashes)"
    }
    finally {
        # Le banc tourne sur un runner jetable, mais on remet le dossier utilisateur comme on l'a trouvé.
        Close-AppleMusic | Out-Null
        Remove-Item -LiteralPath $extensionDir -Recurse -Force -ErrorAction SilentlyContinue
        if (Test-Path -LiteralPath $settingsPath) {
            $kept = @(Get-Content -LiteralPath $settingsPath | Where-Object { $_ -ne $settingLine })
            [System.IO.File]::WriteAllLines($settingsPath, [string[]]$kept, [System.Text.UTF8Encoding]::new($false))
        }
    }
}
