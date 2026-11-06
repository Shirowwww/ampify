# The way people use Ampify: the launcher ran once (as after the install, or at Windows sign-in) and Apple Music is
# opened from its own icon. The launcher has Windows start it with each process of Apple Music (package debug
# settings) and ends: nothing of Ampify waits in the background. The kernel must load into Apple Music opened from its
# icon, again at the next start, and no longer once Ampify is turned off (ampify restore, the uninstaller).

# The package setting Windows reads, removed the way ampify restore does; by hand when the command line is absent.
function Remove-AttachSetting {
    param([string]$FullName)
    $ampify = Join-Path $script:KitRoot 'cli\ampify.exe'
    if (Test-Path -LiteralPath $ampify) {
        & $ampify restore | Out-Null
    }
    if ($FullName) {
        Remove-Item -LiteralPath "HKCU:\Software\Classes\PackagesDebug\$FullName" -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Register-Case -Id 'background-launch' -Title 'Launcher started by Windows: Apple Music opened from its icon gets the kernel' -Requires 'apple-music' -Action {
    param($Ctx)

    $launcher = Join-Path $script:BenchKitRoot 'bench\ampify_launcher.exe'
    if (-not (Test-Path -LiteralPath $launcher)) {
        Add-Check -Ctx $Ctx -Name 'bench build present' -Passed $false -Detail $launcher
        return
    }
    $fullName = (Get-AppxPackage -Name $script:AppleFacts.package.name -ErrorAction SilentlyContinue | Select-Object -First 1).PackageFullName

    Reset-KitState
    $since = Get-Date
    $register = Start-Process -FilePath $launcher -ArgumentList '--background' -WorkingDirectory (Split-Path -Parent $launcher) -PassThru
    $registered = Wait-Journal -FileName 'background.log' -Text 'Windows starts the launcher with each process of' -TimeoutSeconds 30
    Add-Check -Ctx $Ctx -Name 'Windows starts the launcher with Apple Music (package debug setting)' -Passed $registered
    Add-Check -Ctx $Ctx -Name 'the launcher ends: nothing stays in the background' -Passed ($register.WaitForExit(30000))
    $key = if ($fullName) { "HKCU:\Software\Classes\PackagesDebug\$fullName" } else { $null }
    Add-Check -Ctx $Ctx -Name 'the setting is in the registry' -Passed ([bool]($key -and (Test-Path -LiteralPath $key))) -Detail $fullName

    $appPattern = 'AppleMusic\.exe \((\d+)\): kernel loaded'
    Start-AppNormally
    $held = Wait-Until -TimeoutSeconds 120 -Condition {
        $null -ne (Find-JournalMatch -FileName 'attach.log' -Pattern 'AppleMusic\.exe \(\d+\) started, held by Windows')
    }
    Add-Check -Ctx $Ctx -Name 'Windows handed Apple Music to the launcher' -Passed $held
    $loaded = Wait-Until -TimeoutSeconds 60 -Condition { $null -ne (Find-JournalMatch -FileName 'attach.log' -Pattern $appPattern) }
    Add-Check -Ctx $Ctx -Name 'kernel loaded in AppleMusic.exe opened from its icon' -Passed $loaded
    $hostReady = Wait-Journal -FileName 'AppleMusic.log' -Text 'Extension host ready' -TimeoutSeconds 60
    Add-Check -Ctx $Ctx -Name 'AppleMusic.log: extension host ready' -Passed $hostReady
    $agent = Wait-Until -TimeoutSeconds 90 -Condition {
        $null -ne (Find-JournalMatch -FileName 'attach.log' -Pattern 'AMPLibraryAgent\.exe \(\d+\): kernel loaded')
    }
    # Windows 11 hands the engine over too (seen on 05/11/2026); the runner's Windows Server starts it outside the
    # package's debug setting, so its absence is reported, not failed.
    $kernelAgent = Get-KernelState -ProcessName 'AMPLibraryAgent'
    $agentDetail = "processes: $($kernelAgent.Processes), with the kernel: $($kernelAgent.Loaded)"
    if ($agent) {
        Add-Check -Ctx $Ctx -Name 'kernel loaded in AMPLibraryAgent.exe once it settled' -Passed $true -Detail $agentDetail
    } else {
        Add-Info -Ctx $Ctx -Name 'AMPLibraryAgent.exe not handed over by this Windows' -Detail $agentDetail
    }
    $kernelApp = Get-KernelState -ProcessName 'AppleMusic'
    Add-Check -Ctx $Ctx -Name 'ampify_kernel.dll present in AppleMusic.exe' `
        -Passed ($kernelApp.Processes -gt 0 -and $kernelApp.Loaded -eq $kernelApp.Processes) `
        -Detail "processes: $($kernelApp.Processes), with the kernel: $($kernelApp.Loaded)"
    Save-Screenshot -Ctx $Ctx -Name 'apple-music-from-its-icon' | Out-Null

    # Closed: nothing of Ampify runs. Opened again: Windows hands the new process over too.
    $firstMatch = Find-JournalMatch -FileName 'attach.log' -Pattern $appPattern
    $firstProcess = if ($firstMatch) { $firstMatch.Groups[1].Value } else { '' }
    Close-AppleMusic | Out-Null
    $gone = Wait-Until -TimeoutSeconds 30 -Condition { @(Get-Process -Name 'ampify_launcher' -ErrorAction SilentlyContinue).Count -eq 0 }
    Add-Check -Ctx $Ctx -Name 'Apple Music closed: no launcher left running' -Passed $gone
    Start-AppNormally
    # A process of the second start: Windows may hand over two processes of the first one (a music: link, a relaunch).
    $again = Wait-Until -TimeoutSeconds 120 -Condition {
        $found = [regex]::Matches([string](Read-Journal 'attach.log'), $appPattern)
        @($found | Where-Object { $_.Groups[1].Value -ne $firstProcess }).Count -gt 0
    }
    Add-Check -Ctx $Ctx -Name 'second start: kernel loaded again' -Passed $again
    Close-AppleMusic | Out-Null

    # Turned off, the way ampify restore and the uninstaller do: Apple Music then starts without the kernel.
    Remove-AttachSetting -FullName $fullName
    Add-Check -Ctx $Ctx -Name 'turned off: the package setting is gone' -Passed ([bool]($key -and -not (Test-Path -LiteralPath $key)))
    Start-AppNormally
    $open = Wait-Until -TimeoutSeconds 90 -Condition { @(Get-Process -Name 'AppleMusic' -ErrorAction SilentlyContinue).Count -gt 0 }
    Start-Sleep -Seconds 10
    $kernelAfter = Get-KernelState -ProcessName 'AppleMusic'
    $clean = $open -and $kernelAfter.Processes -gt 0 -and $kernelAfter.Loaded -eq 0 -and $kernelAfter.Unreadable -eq 0
    Add-Check -Ctx $Ctx -Name 'turned off: Apple Music starts without the kernel' -Passed $clean `
        -Detail "processes: $($kernelAfter.Processes), with the kernel: $($kernelAfter.Loaded), unreadable: $($kernelAfter.Unreadable)"
    Close-AppleMusic | Out-Null

    $crashes = @(Get-AppCrashEvents -Since $since)
    Add-Check -Ctx $Ctx -Name 'no Apple Music crash' -Passed ($crashes.Count -eq 0) `
        -Detail "$($crashes.Count) event(s): $(Format-CrashEvents -Events $crashes)"
    Copy-JournalToResults -Ctx $Ctx
    Write-JournalTail -FileName 'background.log'
    Write-JournalTail -FileName 'attach.log'
    Write-JournalTail -FileName 'AppleMusic.log'
    Remove-AttachSetting -FullName $fullName  # whatever failed above, the next case starts Apple Music as it is
}
