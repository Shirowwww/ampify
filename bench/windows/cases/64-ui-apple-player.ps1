# Apple Music's own full-screen player, opened by Ampify.ui.applePlayer.open() and laid over the main window
# (src/win/apple_player.cpp). The log says how it went, the captures show it, then Escape is pressed in the player to see
# what Apple Music does with it once it is a window.
# Job: MSVC, UI shard ($script:BenchKitRoot\ui with ampify_launcher.exe and ampify_kernel.dll).

if (-not ('AmpifyBench.AppWindows' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
namespace AmpifyBench {
public static class AppWindows {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [DllImport("user32.dll")] static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] static extern void mouse_event(uint flags, int dx, int dy, uint data, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out RECT rect);
    [DllImport("user32.dll")] static extern int GetSystemMetrics(int index);

    // Moves the pointer as a mouse does. With SetCursorPos and moves of zero pixels, the player saw the press but
    // never followed (bench, 01/11): absolute moves are what a real mouse sends.
    static void MoveTo(int x, int y) {
        int width = GetSystemMetrics(0), height = GetSystemMetrics(1);  // SM_CXSCREEN, SM_CYSCREEN
        mouse_event(0x8001, x * 65535 / (width - 1), y * 65535 / (height - 1), 0, UIntPtr.Zero);  // MOVE | ABSOLUTE
    }

    // Left button down at (x, y), ten steps to (x + dx, y + dy), button up: what a user dragging does.
    public static void Drag(int x, int y, int dx, int dy) {
        SetCursorPos(x, y);
        MoveTo(x, y);
        Thread.Sleep(200);
        mouse_event(0x0002, 0, 0, 0, UIntPtr.Zero);  // MOUSEEVENTF_LEFTDOWN
        Thread.Sleep(300);
        for (int step = 1; step <= 10; step++) {
            MoveTo(x + dx * step / 10, y + dy * step / 10);
            Thread.Sleep(40);
        }
        Thread.Sleep(200);
        mouse_event(0x0004, 0, 0, 0, UIntPtr.Zero);  // MOUSEEVENTF_LEFTUP
        Thread.Sleep(300);
    }
    delegate bool EnumProc(IntPtr window, IntPtr parameter);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc callback, IntPtr parameter);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] static extern IntPtr GetWindowLongPtr(IntPtr window, int index);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);

    // The visible top-level windows of the process: handle, size, place and style.
    public static List<string> Describe(uint process, List<long> handles) {
        var rows = new List<string>();
        IntPtr front = GetForegroundWindow();
        EnumWindows((window, parameter) => {
            uint owner;
            GetWindowThreadProcessId(window, out owner);
            if (owner != process || !IsWindowVisible(window)) return true;
            RECT r;
            GetWindowRect(window, out r);
            handles.Add(window.ToInt64());
            rows.Add(String.Format("{0}: {1}x{2} at {3},{4}, style 0x{5:X8}{6}", window.ToInt64(), r.R - r.L, r.B - r.T,
                r.L, r.T, GetWindowLongPtr(window, -16).ToInt64(), window == front ? " (in front)" : ""));
            return true;
        }, IntPtr.Zero);
        return rows;
    }
}
}
'@
}

# @{ Handles = the window handles; Text = one line per window } for AppleMusic.exe.
function Get-AppleMusicWindowList {
    $handles = New-Object System.Collections.Generic.List[long]
    $rows = New-Object System.Collections.Generic.List[string]
    foreach ($process in @(Get-Process -Name 'AppleMusic' -ErrorAction SilentlyContinue)) {
        foreach ($row in [AmpifyBench.AppWindows]::Describe([uint32]$process.Id, $handles)) { $rows.Add($row) }
    }
    return @{ Handles = @($handles); Text = ($rows -join ' | ') }
}

Register-Case -Id 'ui-apple-player' -Title "UI: Apple Music's full-screen player laid over the main window" -Requires 'apple-music', 'kit:ui' -Action {
    param($Ctx)

    $launcher = Join-Path $script:BenchKitRoot 'ui\ampify_launcher.exe'

    # A one-line extension that asks for the player when it starts; it starts hot, after the first capture.
    Reset-KitState
    $utf8 = [System.Text.UTF8Encoding]::new($false)
    $extensionDir = Join-Path $script:KitDir 'extensions\apple-player-probe'
    Remove-Item -LiteralPath $extensionDir -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $extensionDir | Out-Null
    $manifest = '{"id":"apple-player-probe","name":"Apple player probe","version":"0.0.1","kind":"extension",' +
        '"author":"bench","description":"Opens the full-screen player of Apple Music.","kitApi":"1.0","main":"main.js",' +
        '"permissions":["ui.fullscreen"]}'
    [System.IO.File]::WriteAllText((Join-Path $extensionDir 'manifest.json'), $manifest, $utf8)
    [System.IO.File]::WriteAllText((Join-Path $extensionDir 'main.js'), "Ampify.ui.applePlayer.open();`n", $utf8)
    $settings = Join-Path $script:KitDir 'settings.txt'
    $reload = Join-Path $script:KitDir 'reload.txt'
    Remove-Item -LiteralPath $reload -Force -ErrorAction SilentlyContinue
    Set-Settings -Values @{
        'extensions.apple-player-probe.enabled' = 'false'
        'theme.active' = 'none'
        'theme.inspect' = '0'
    }

    $since = Get-Date
    $launcherProcess = Start-Launcher -LauncherPath $launcher
    try {
        $launched = Wait-Journal -FileName 'launcher.log' -Text $script:LogPhrase.AppleMusicStarted -TimeoutSeconds 120
        Add-Check -Ctx $Ctx -Name 'Apple Music started by the launcher' -Passed $launched
        if (-not $launched) { return }
        Clear-FirstRun -Ctx $Ctx
        $bound = Wait-Journal -FileName 'AppleMusic.log' -Text $script:LogPhrase.MainWindowBound -TimeoutSeconds 90
        Add-Check -Ctx $Ctx -Name 'log: interface service bound to "MainWindow"' -Passed $bound
        Start-Sleep -Seconds 5
        Show-UiAppWindow
        Start-Sleep -Seconds 2
        Save-Screenshot -Ctx $Ctx -Name 'apple-player-before' | Out-Null
        $before = Get-AppleMusicWindowList
        Add-Info -Ctx $Ctx -Name 'windows before' -Detail $before.Text

        Set-Settings -Values @{ 'extensions.apple-player-probe.enabled' = 'true' }
        [System.IO.File]::AppendAllText($reload, "start apple-player-probe`n", $utf8)
        $endPattern = 'Apple player: (a second later|not opened|shown, but|its window|already being|the main window)'
        $ended = Wait-Until -TimeoutSeconds 60 -Condition { $null -ne (Find-JournalMatch -FileName 'AppleMusic.log' -Pattern $endPattern) }
        Add-Check -Ctx $Ctx -Name 'log: the request reached an outcome' -Passed $ended
        $lines = @(([string](Read-Journal 'AppleMusic.log')) -split "`r?`n" | Where-Object { $_ -match '\] Apple player: ' })
        foreach ($line in $lines) { Add-Info -Ctx $Ctx -Name 'log' -Detail $line }
        Add-Check -Ctx $Ctx -Name "log: Apple's player shown" -Passed (@($lines -match 'full-screen player shown').Count -gt 0)
        Add-Check -Ctx $Ctx -Name 'log: a window over the main one, still there a second later' `
            -Passed (@($lines -match 'a second later, shown, overlapped presenter').Count -gt 0)

        Start-Sleep -Seconds 2
        $open = Get-AppleMusicWindowList
        Add-Info -Ctx $Ctx -Name 'windows with the player' -Detail $open.Text
        Save-Screenshot -Ctx $Ctx -Name 'apple-player-open' -AsIs | Out-Null

        # The player is the largest new window (a tooltip or a dialog may have appeared too).
        $new = @($open.Handles | Where-Object { $before.Handles -notcontains $_ })
        if ($new.Count -gt 1) { Add-Info -Ctx $Ctx -Name 'new windows' -Detail "$($new.Count): the largest is taken" }
        $player = $new | Sort-Object -Descending -Property {
            $r = New-Object AmpifyBench.AppWindows+RECT
            [AmpifyBench.AppWindows]::GetWindowRect([IntPtr]$_, [ref]$r) | Out-Null
            ($r.R - $r.L) * ($r.B - $r.T)
        } | Select-Object -First 1
        if ($player) {
            # A drag on the left edge of the player, its background: the window should follow.
            [AmpifyBench.AppWindows]::SetForegroundWindow([IntPtr]$player) | Out-Null
            Start-Sleep -Milliseconds 500
            $start = New-Object AmpifyBench.AppWindows+RECT
            [AmpifyBench.AppWindows]::GetWindowRect([IntPtr]$player, [ref]$start) | Out-Null
            [AmpifyBench.AppWindows]::Drag($start.L + 40, [int](($start.T + $start.B) / 2), 160, 90)
            $moved = New-Object AmpifyBench.AppWindows+RECT
            [AmpifyBench.AppWindows]::GetWindowRect([IntPtr]$player, [ref]$moved) | Out-Null
            Add-Check -Ctx $Ctx -Name 'dragging the background moves the window' `
                -Passed (($moved.L -ne $start.L) -or ($moved.T -ne $start.T)) `
                -Detail "from $($start.L),$($start.T) to $($moved.L),$($moved.T)"
            Save-Screenshot -Ctx $Ctx -Name 'apple-player-dragged' -AsIs | Out-Null

            # Escape in the player closes it through Apple's own close button.
            [AmpifyBench.AppWindows]::SetForegroundWindow([IntPtr]$player) | Out-Null
            Start-Sleep -Milliseconds 500
            $front = [AmpifyBench.AppWindows]::GetForegroundWindow().ToInt64()
            if ($front -ne $player) { Add-Info -Ctx $Ctx -Name 'Escape' -Detail "the player is not in front ($front), the key may go elsewhere" }
            [System.Windows.Forms.SendKeys]::SendWait('{ESC}')
            Start-Sleep -Seconds 2
            $after = Get-AppleMusicWindowList
            Add-Check -Ctx $Ctx -Name 'Escape closes the player' -Passed ($after.Handles -notcontains $player) -Detail $after.Text
            Save-Screenshot -Ctx $Ctx -Name 'apple-player-escape' -AsIs | Out-Null
        } else {
            Add-Info -Ctx $Ctx -Name 'Escape' -Detail 'not tried: no new window of Apple Music'
        }
    } finally {
        $presses = @(([string](Read-Journal 'AppleMusic.log')) -split "`r?`n" | Where-Object { $_ -match '\] Apple player: (press|closed|Escape)' })
        foreach ($line in $presses) { Add-Info -Ctx $Ctx -Name 'log' -Detail $line }
        $closed = Close-AppleMusic
        $exited = $launcherProcess.WaitForExit(15000)
        Add-Check -Ctx $Ctx -Name 'launcher: stops when Apple Music closes' -Passed ($closed -and $exited)
        $crashes = @(Get-AppCrashEvents -Since $since)
        Add-Check -Ctx $Ctx -Name 'no Apple Music crash' -Passed ($crashes.Count -eq 0) `
            -Detail "$($crashes.Count) event(s): $(Format-CrashEvents -Events $crashes)"
        Copy-JournalToResults -Ctx $Ctx
        Remove-Item -LiteralPath $extensionDir -Recurse -Force -ErrorAction SilentlyContinue
        Set-Settings -Values @{ 'extensions.apple-player-probe.enabled' = 'false' }
    }
}
