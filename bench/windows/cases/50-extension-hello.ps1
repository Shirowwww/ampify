# Steps 1 to 6 of the M7 test, on the bench build: the hello-extension extension from the user folder starts in
# AppleMusic.exe, not in AMPLibraryAgent.exe, and its launch counter is written to storage.

Register-Case -Id 'extension-hello' -Title 'hello-extension started in AppleMusic.exe, not in AMPLibraryAgent.exe' -Requires 'apple-music' -Action {
    param($Ctx)

    $launcher = Join-Path $script:BenchKitRoot 'bench\ampify_launcher.exe'
    if (-not (Test-Path -LiteralPath $launcher)) {
        Add-Check -Ctx $Ctx -Name 'bench build present' -Passed $false -Detail $launcher
        return
    }

    $example = Join-Path $repo 'examples\hello-extension'
    $extensionDir = Join-Path $script:KitDir 'extensions\hello-extension'
    $settingsPath = Join-Path $script:KitDir 'settings.txt'
    $storagePath = Join-Path $script:KitDir 'storage\hello-extension.json'
    $settingLine = 'extensions.hello-extension.enabled = true'

    # Storage starts empty: the counter must be exactly 1 after this launch.
    Reset-KitState
    Remove-Item -LiteralPath $storagePath -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $extensionDir) | Out-Null
    Copy-Item -LiteralPath $example -Destination $extensionDir -Recurse -Force
    [System.IO.File]::AppendAllText($settingsPath, "$settingLine`r`n", [System.Text.UTF8Encoding]::new($false))

    try {
        $since = Get-Date
        $launcherProcess = Start-Launcher -LauncherPath $launcher

        $launched = Wait-Journal -FileName 'launcher.log' -Text 'Apple Music started' -TimeoutSeconds 120
        Add-Check -Ctx $Ctx -Name 'Apple Music started by the launcher' -Passed $launched
        if (-not $launched) {
            Close-AppleMusic | Out-Null
            return
        }

        $started = Wait-Journal -FileName 'AppleMusic.log' -Text 'Extension hello-extension started' -TimeoutSeconds 60
        Add-Check -Ctx $Ctx -Name 'AppleMusic.log: hello-extension started' -Passed $started

        $retained = Test-JournalContains -FileName 'AppleMusic.log' -Text 'JavaScript elements accepted: 1'
        Add-Check -Ctx $Ctx -Name 'AppleMusic.log: one JavaScript element accepted' -Passed $retained

        $greeted = Wait-Journal -FileName 'AppleMusic.log' -Text '[hello-extension] Hello, launch #1' -TimeoutSeconds 30
        Add-Check -Ctx $Ctx -Name 'AppleMusic.log: message from hello-extension' -Passed $greeted

        # The example declares "processes": ["ui"]: it must write nothing to the agent's log.
        $agentLog = Read-Journal 'AMPLibraryAgent.log'
        $inAgent = [bool]($agentLog -and $agentLog.Contains('hello-extension'))
        Add-Check -Ctx $Ctx -Name 'AMPLibraryAgent.log: no line from hello-extension' -Passed (-not $inAgent)

        $storageOk = (Test-Path -LiteralPath $storagePath) -and ((Get-Content -LiteralPath $storagePath -Raw) -match '"launches"\s*:\s*1\b')
        Add-Check -Ctx $Ctx -Name 'storage\hello-extension.json: counter at 1' -Passed ([bool]$storageOk)
        Save-Screenshot -Ctx $Ctx -Name 'extension-hello' | Out-Null

        $closed = Close-AppleMusic
        $exited = $launcherProcess.WaitForExit(15000)
        Add-Check -Ctx $Ctx -Name 'launcher: stops when Apple Music closes' -Passed ($closed -and $exited)

        $crashes = @(Get-AppCrashEvents -Since $since)
        Add-Check -Ctx $Ctx -Name 'no Apple Music crash during the M7 test' -Passed ($crashes.Count -eq 0) `
            -Detail "$($crashes.Count) event(s): $(Format-CrashEvents -Events $crashes)"
    }
    finally {
        # The bench runs on a throwaway runner, but the user folder is put back as it was found.
        Close-AppleMusic | Out-Null
        Remove-Item -LiteralPath $extensionDir -Recurse -Force -ErrorAction SilentlyContinue
        if (Test-Path -LiteralPath $settingsPath) {
            $kept = @(Get-Content -LiteralPath $settingsPath | Where-Object { $_ -ne $settingLine })
            [System.IO.File]::WriteAllLines($settingsPath, [string[]]$kept, [System.Text.UTF8Encoding]::new($false))
        }
    }
}
