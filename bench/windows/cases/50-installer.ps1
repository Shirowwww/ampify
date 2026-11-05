# M6: install and uninstall Ampify, without Apple Music (it installs anyway). The uninstaller deletes itself: that is
# the real path on Windows.

# Runs an Ampify program and returns its exit code.
# A program still running after the timeout is stopped and the code is -2, so a stuck installer cannot
# hold the whole bench until the job's own time limit.
function Invoke-KitProgram {
    param([string]$Path, [string[]]$Arguments, [int]$TimeoutSeconds = 300)
    $process = Start-Process -FilePath $Path -ArgumentList $Arguments -PassThru -WindowStyle Hidden
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        $process | Stop-Process -Force -ErrorAction SilentlyContinue
        Write-Host "  $(Split-Path -Leaf $Path) stopped after $TimeoutSeconds s without an answer"
        return -2
    }
    return $process.ExitCode
}

Register-Case -Id 'installer' -Title 'Install, uninstall keeping and purging data, refusal while Apple Music is open' -Action {
    param($Ctx)

    $setup = Join-Path $script:KitRoot 'release\ampify_setup.exe'
    if (-not (Test-Path -LiteralPath $setup)) {
        Add-Check -Ctx $Ctx -Name 'ampify_setup.exe present in release' -Passed $false -Detail $setup
        return
    }

    $installDir = Join-Path $env:LOCALAPPDATA 'Programs\Ampify'
    $installedSetup = Join-Path $installDir 'ampify_setup.exe'
    $shortcut = Join-Path ([Environment]::GetFolderPath('Programs')) 'Apple Music with Ampify.lnk'
    $uninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Ampify'
    $runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
    $launcherFile = Join-Path $installDir 'ampify_launcher.exe'
    $signInEntry = {
        $values = Get-ItemProperty -Path $runKey -ErrorAction SilentlyContinue
        if ($values -and $values.PSObject.Properties['Ampify']) { return $values.Ampify }
        return $null
    }
    $inUserPath = { @(([Environment]::GetEnvironmentVariable('Path', 'User') -split ';') | Where-Object { $_.TrimEnd('\') -eq $installDir }).Count -gt 0 }
    $backgroundRunning = {
        $mutex = $null
        if ([System.Threading.Mutex]::TryOpenExisting('Local\AmpifyBackground', [ref]$mutex)) { $mutex.Dispose(); return $true }
        return $false
    }

    # A clean state: uninstall whatever a previous pass may have left.
    Reset-KitState
    Invoke-KitProgram -Path $setup -Arguments @('/uninstall', '/silent', '/purgedata') | Out-Null

    # 1. Silent install.
    $code = Invoke-KitProgram -Path $setup -Arguments @('/silent')
    Add-Check -Ctx $Ctx -Name 'install: code 0' -Passed ($code -eq 0) -Detail "code $code"
    $files = @('ampify_launcher.exe', 'ampify_kernel.dll', 'ampify_setup.exe') | Where-Object { -not (Test-Path -LiteralPath (Join-Path $installDir $_)) }
    Add-Check -Ctx $Ctx -Name 'the three files are in the install folder' -Passed (@($files).Count -eq 0) `
        -Detail "missing: $(@($files) -join ', ')"
    Add-Check -Ctx $Ctx -Name 'no Start menu shortcut: Apple Music keeps its own icon' -Passed (-not (Test-Path -LiteralPath $shortcut))
    $entryText = & $signInEntry
    # At sign-in the launcher registers itself with Windows for each Apple Music process (--register) and ends.
    Add-Check -Ctx $Ctx -Name 'sign-in entry registers the launcher with Apple Music' `
        -Passed ([bool]$entryText -and $entryText -like "*$launcherFile*--register*") -Detail "$entryText"
    Add-Check -Ctx $Ctx -Name 'install folder in the user Path (ampify in a new terminal)' -Passed (& $inUserPath)
    # Run as administrator (CI runners are), the installer starts the launcher through Explorer, without administrator
    # rights, so a normal session can stop it. A runner may have no Explorer to ask: the log then says so.
    $principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
    if ($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        Add-Check -Ctx $Ctx -Name 'run as administrator: background launcher started through Explorer, not elevated' `
            -Passed (Test-JournalContains -FileName 'install.log' -Text 'Installer run as administrator:')
    } else {
        Add-Check -Ctx $Ctx -Name 'background launcher running after the install' `
            -Passed (Wait-Until -TimeoutSeconds 15 -Condition { & $backgroundRunning })
    }
    $entry = Get-ItemProperty -Path $uninstallKey -ErrorAction SilentlyContinue
    Add-Check -Ctx $Ctx -Name 'uninstall entry (Settings > Installed apps)' `
        -Passed ($entry -and $entry.DisplayName -eq 'Ampify for Apple Music') -Detail "$($entry.DisplayVersion)"

    # 2. Refusal: a process named AppleMusic.exe is open, the uninstall must stop without deleting anything.
    $fakeDir = Join-Path $Ctx.Dir 'fake-apple-music'
    New-Item -ItemType Directory -Force -Path $fakeDir | Out-Null
    $fake = Join-Path $fakeDir 'AppleMusic.exe'
    Copy-Item -LiteralPath (Join-Path $env:SystemRoot 'System32\cmd.exe') -Destination $fake -Force
    $sleeper = Start-Process -FilePath $fake -ArgumentList '/c', 'ping -n 300 127.0.0.1 > nul' -PassThru -WindowStyle Hidden
    Start-Sleep -Seconds 2
    $refused = Invoke-KitProgram -Path $installedSetup -Arguments @('/uninstall', '/silent', '/keepdata')
    Add-Check -Ctx $Ctx -Name 'uninstall refused while Apple Music is open (code 3)' -Passed ($refused -eq 3) -Detail "code $refused"
    Add-Check -Ctx $Ctx -Name 'nothing is deleted after the refusal' -Passed (Test-Path -LiteralPath $launcherFile)
    Stop-Process -Id $sleeper.Id -Force -ErrorAction SilentlyContinue
    Get-Process -Name 'AppleMusic', 'PING' -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2

    # 3. Uninstall keeping the data: everything goes, except the data folder.
    $code = Invoke-KitProgram -Path $installedSetup -Arguments @('/uninstall', '/silent', '/keepdata')
    Add-Check -Ctx $Ctx -Name 'uninstall keeping data: code 0' -Passed ($code -eq 0) -Detail "code $code"
    if ($code -ne 0) { Write-JournalTail -FileName 'uninstall.log' }
    $gone = Wait-Until -TimeoutSeconds 30 -Condition { -not (Test-Path -LiteralPath $installDir) }
    Add-Check -Ctx $Ctx -Name 'install folder deleted, uninstaller included' -Passed $gone
    Add-Check -Ctx $Ctx -Name 'sign-in entry deleted' -Passed (-not (& $signInEntry))
    Add-Check -Ctx $Ctx -Name 'background launcher stopped' -Passed (-not (& $backgroundRunning))
    Add-Check -Ctx $Ctx -Name 'install folder out of the user Path' -Passed (-not (& $inUserPath))
    Add-Check -Ctx $Ctx -Name 'uninstall entry deleted' -Passed (-not (Test-Path -LiteralPath $uninstallKey))
    Add-Check -Ctx $Ctx -Name 'data kept (Ampify folder)' -Passed (Test-Path -LiteralPath $script:KitDir)
    Copy-JournalToResults -Ctx $Ctx

    # 4. Reinstall, then uninstall deleting the data too.
    Invoke-KitProgram -Path $setup -Arguments @('/silent') | Out-Null
    $code = Invoke-KitProgram -Path $installedSetup -Arguments @('/uninstall', '/silent', '/purgedata')
    Add-Check -Ctx $Ctx -Name 'uninstall purging data: code 0' -Passed ($code -eq 0) -Detail "code $code"
    if ($code -ne 0) { Write-JournalTail -FileName 'uninstall.log' }
    $gone = Wait-Until -TimeoutSeconds 30 -Condition { -not (Test-Path -LiteralPath $installDir) }
    Add-Check -Ctx $Ctx -Name 'purge: install folder deleted' -Passed $gone
    Add-Check -Ctx $Ctx -Name 'purge: data folder deleted' -Passed (-not (Test-Path -LiteralPath $script:KitDir))
    Add-Check -Ctx $Ctx -Name 'purge: sign-in and uninstall entries deleted' `
        -Passed ((-not (& $signInEntry)) -and (-not (Test-Path -LiteralPath $uninstallKey)))
}
