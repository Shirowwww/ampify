# UI Automation helpers for the live bench (bench/windows/live): find Apple Music's elements, click them, type into
# them like a user, and dump the tree into the job log. Loaded after harness.ps1 (it reuses Get-AppleMusicWindow and
# the AmpifyWindowCapture type). Works in Windows PowerShell 5.1 and PowerShell 7.
# Secrets: every text that reaches the log goes through Protect-UiaText, which hides the registered values.

Set-StrictMode -Version Latest

Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, WindowsBase, System.Windows.Forms, System.Drawing

# Keyboard and mouse input through SendInput. Text goes as Unicode key events, so any character of a password is
# typed as is, without SendKeys escaping.
if (-not ('AmpifyInput' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Threading;

public static class AmpifyInput {
    [StructLayout(LayoutKind.Sequential)]
    struct MOUSEINPUT { public int dx; public int dy; public uint mouseData; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Sequential)]
    struct KEYBDINPUT { public ushort wVk; public ushort wScan; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Explicit)]
    struct InputUnion { [FieldOffset(0)] public MOUSEINPUT mi; [FieldOffset(0)] public KEYBDINPUT ki; }
    [StructLayout(LayoutKind.Sequential)]
    struct INPUT { public uint type; public InputUnion u; }

    const uint INPUT_MOUSE = 0, INPUT_KEYBOARD = 1;
    const uint KEYEVENTF_KEYUP = 0x0002, KEYEVENTF_UNICODE = 0x0004;
    const uint MOUSEEVENTF_LEFTDOWN = 0x0002, MOUSEEVENTF_LEFTUP = 0x0004;

    [DllImport("user32.dll", SetLastError = true)] static extern uint SendInput(uint count, INPUT[] inputs, int size);
    [DllImport("user32.dll")] static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] static extern bool BringWindowToTop(IntPtr hWnd);
    [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr hWnd, int cmd);
    [DllImport("user32.dll")] static extern bool IsIconic(IntPtr hWnd);
    [DllImport("user32.dll")] static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);

    static uint Send(INPUT[] inputs) { return SendInput((uint)inputs.Length, inputs, Marshal.SizeOf(typeof(INPUT))); }

    static INPUT Key(ushort vk, ushort scan, uint flags) {
        INPUT input = new INPUT();
        input.type = INPUT_KEYBOARD;
        input.u.ki.wVk = vk;
        input.u.ki.wScan = scan;
        input.u.ki.dwFlags = flags;
        return input;
    }

    // Types the text one character at a time. Returns how many characters went through.
    public static int SendText(string text, int delayMs) {
        int sent = 0;
        foreach (char c in text) {
            INPUT[] pair = { Key(0, c, KEYEVENTF_UNICODE), Key(0, c, KEYEVENTF_UNICODE | KEYEVENTF_KEYUP) };
            if (Send(pair) == 2) sent++;
            Thread.Sleep(delayMs);
        }
        return sent;
    }

    // Presses the keys in order and releases them in reverse order (a chord such as Ctrl+A).
    public static void SendChord(ushort[] keys) {
        INPUT[] inputs = new INPUT[keys.Length * 2];
        for (int i = 0; i < keys.Length; i++) {
            inputs[i] = Key(keys[i], 0, 0);
            inputs[keys.Length * 2 - 1 - i] = Key(keys[i], 0, KEYEVENTF_KEYUP);
        }
        Send(inputs);
    }

    public static void Click(int x, int y, bool twice) {
        SetCursorPos(x, y);
        Thread.Sleep(80);
        int times = twice ? 2 : 1;
        for (int i = 0; i < times; i++) {
            INPUT down = new INPUT(); down.type = INPUT_MOUSE; down.u.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
            INPUT up = new INPUT(); up.type = INPUT_MOUSE; up.u.mi.dwFlags = MOUSEEVENTF_LEFTUP;
            Send(new INPUT[] { down, up });
            Thread.Sleep(90);
        }
    }

    // Brings the window forward. Windows only lets the process that has the last input do it: a short Alt press
    // gives this process that right first.
    public static bool Foreground(IntPtr hWnd) {
        if (hWnd == IntPtr.Zero) return false;
        if (IsIconic(hWnd)) ShowWindow(hWnd, 9);  // SW_RESTORE
        keybd_event(0x12, 0, 0, UIntPtr.Zero);
        keybd_event(0x12, 0, 2, UIntPtr.Zero);
        BringWindowToTop(hWnd);
        SetForegroundWindow(hWnd);
        Thread.Sleep(300);
        return GetForegroundWindow() == hWnd;
    }
}
'@
}

$script:UiaSecrets = New-Object System.Collections.Generic.List[string]

# Values that must never reach the log, a file or an artifact. Protect-UiaText replaces them with ***.
function Register-UiaSecret {
    param([string]$Value)
    if ($Value -and $Value.Length -ge 3 -and -not $script:UiaSecrets.Contains($Value)) { $script:UiaSecrets.Add($Value) }
}

function Protect-UiaText {
    param([string]$Text)
    if (-not $Text) { return $Text }
    foreach ($secret in $script:UiaSecrets) {
        $Text = [regex]::Replace($Text, [regex]::Escape($secret), '***', 'IgnoreCase')
    }
    return $Text
}

# The account and the code come from the workflow's environment: they are registered as soon as this file loads.
foreach ($uiaSecretName in 'AMPIFY_APPLE_ID', 'AMPIFY_APPLE_PASSWORD', 'AMPIFY_CODE_2FA') {
    $uiaSecretValue = [Environment]::GetEnvironmentVariable($uiaSecretName)
    if (-not $uiaSecretValue) { continue }
    Register-UiaSecret $uiaSecretValue
    if ($uiaSecretName -eq 'AMPIFY_APPLE_ID' -and $uiaSecretValue.Contains('@')) {
        $uiaSecretLocal = $uiaSecretValue.Substring(0, $uiaSecretValue.IndexOf('@'))
        if ($uiaSecretLocal.Length -ge 4) { Register-UiaSecret $uiaSecretLocal }
    }
}
Remove-Variable -Name uiaSecretName, uiaSecretValue, uiaSecretLocal -ErrorAction SilentlyContinue

$script:UiaVirtualKeys = @{
    Enter = 0x0D; Tab = 0x09; Escape = 0x1B; Space = 0x20; Back = 0x08; Delete = 0x2E
    Left = 0x25; Up = 0x26; Right = 0x27; Down = 0x28; Home = 0x24; End = 0x23
    Control = 0x11; Shift = 0x10; Alt = 0x12; A = 0x41
}

# Presses one key or a chord: Send-Key Enter, Send-Key Control, A.
function Send-Key {
    param([Parameter(Mandatory)][string[]]$Keys)
    $codes = [uint16[]]@($Keys | ForEach-Object {
        if (-not $script:UiaVirtualKeys.ContainsKey($_)) { throw "unknown key: $_" }
        [uint16]$script:UiaVirtualKeys[$_]
    })
    [AmpifyInput]::SendChord($codes)
    Start-Sleep -Milliseconds 150
}

# Top-level UI Automation windows of a process (Apple Music by default): its main window, its dialogs, its sign-in sheet.
function Get-UiaAppWindows {
    param([string]$ProcessName = 'AppleMusic')
    $ids = @(Get-Process -Name $ProcessName -ErrorAction SilentlyContinue | ForEach-Object { $_.Id })
    if ($ids.Count -eq 0) { return }
    $root = [System.Windows.Automation.AutomationElement]::RootElement
    $children = $root.FindAll([System.Windows.Automation.TreeScope]::Children, [System.Windows.Automation.Condition]::TrueCondition)
    foreach ($child in $children) {
        try {
            if ($ids -contains $child.Current.ProcessId) { $child }
        } catch {
            continue  # the window closed while we looked
        }
    }
}

# Apple Music's main window as a UI Automation element, or $null.
function Get-UiaMainWindow {
    $handle = Get-AppleMusicWindow
    if ($handle -ne [IntPtr]::Zero) {
        try { return [System.Windows.Automation.AutomationElement]::FromHandle($handle) } catch { }
    }
    return (@(Get-UiaAppWindows) | Select-Object -First 1)
}

# Top-level window that holds an element (Settings may open in its own window), or $null.
function Get-UiaWindowOf {
    param([Parameter(Mandatory)]$Element)
    $root = [System.Windows.Automation.AutomationElement]::RootElement
    $walker = [System.Windows.Automation.TreeWalker]::ControlViewWalker
    $current = $Element
    try {
        while ($true) {
            $parent = $walker.GetParent($current)
            if ($null -eq $parent -or $parent -eq $root) { return $current }
            $current = $parent
        }
    } catch {
        return $null
    }
}

# Minimizes the runner's consoles and brings Apple Music forward, maximized: typing and mouse clicks go to the window
# in front. Returns $true when Apple Music is the foreground window.
function Show-AppleMusicWindow {
    $window = Get-AppleMusicWindow
    if ($window -eq [IntPtr]::Zero) { return $false }
    Get-Process -Name 'WindowsTerminal', 'conhost', 'OpenConsole', 'pwsh', 'powershell' -ErrorAction SilentlyContinue |
        Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero -and $_.MainWindowHandle -ne $window } |
        ForEach-Object { [AmpifyWindowCapture]::ShowWindow($_.MainWindowHandle, 6) | Out-Null }  # SW_MINIMIZE
    [AmpifyWindowCapture]::ShowWindow($window, 3) | Out-Null  # SW_MAXIMIZE
    return [AmpifyInput]::Foreground($window)
}

function Get-UiaControlType {
    param([string]$Name)
    $field = [System.Windows.Automation.ControlType].GetField($Name)
    if (-not $field) { throw "unknown control type: $Name" }
    return $field.GetValue($null)
}

# Finds elements by exact name, name pattern (regex, case-insensitive), AutomationId (exact or pattern) and control
# type, under -Root (an element or a list; default: every Apple Music window). Waits up to -TimeoutSeconds for a match.
# Returns the first match, or all of them with -All. Returns $null when nothing matches in time.
function Find-Element {
    param(
        $Root = $null,
        [string]$Name,
        [string]$NamePattern,
        [string]$AutomationId,
        [string]$AutomationIdPattern,
        [string]$ControlType,
        [int]$TimeoutSeconds = 0,
        [switch]$All,
        [switch]$VisibleOnly,
        [scriptblock]$Where
    )
    $AE = [System.Windows.Automation.AutomationElement]
    $conditions = @()
    $PC = [System.Windows.Automation.PropertyCondition]
    if ($Name) { $conditions += $PC::new($AE::NameProperty, $Name) }
    if ($AutomationId) { $conditions += $PC::new($AE::AutomationIdProperty, $AutomationId) }
    if ($ControlType) { $conditions += $PC::new($AE::ControlTypeProperty, (Get-UiaControlType $ControlType)) }
    if ($conditions.Count -eq 0) {
        $condition = [System.Windows.Automation.Condition]::TrueCondition
    } elseif ($conditions.Count -eq 1) {
        $condition = $conditions[0]
    } else {
        $condition = [System.Windows.Automation.AndCondition]::new([System.Windows.Automation.Condition[]]$conditions)
    }

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ($true) {
        $roots = if ($null -ne $Root) { @($Root) } else { @(Get-UiaAppWindows) }
        $found = New-Object System.Collections.Generic.List[object]
        foreach ($top in $roots) {
            if ($null -eq $top) { continue }
            try {
                $candidates = $top.FindAll([System.Windows.Automation.TreeScope]::Subtree, $condition)
            } catch {
                continue
            }
            foreach ($element in $candidates) {
                try {
                    $current = $element.Current
                    if ($NamePattern -and $current.Name -notmatch $NamePattern) { continue }
                    if ($AutomationIdPattern -and $current.AutomationId -notmatch $AutomationIdPattern) { continue }
                    if ($VisibleOnly -and $current.IsOffscreen) { continue }
                    if ($Where -and -not (& $Where $element)) { continue }
                } catch {
                    continue  # stale element
                }
                if (-not $All) { return $element }
                $found.Add($element)
            }
        }
        if ($All -and $found.Count -gt 0) { return $found.ToArray() }
        if ((Get-Date) -ge $deadline) { break }
        Start-Sleep -Milliseconds 500
    }
    if ($All) { return @() }
    return $null
}

# Same search, with a timeout that defaults to 30 s. -Required throws when nothing appears.
function Wait-ForElement {
    param(
        $Root = $null,
        [string]$Name,
        [string]$NamePattern,
        [string]$AutomationId,
        [string]$AutomationIdPattern,
        [string]$ControlType,
        [int]$TimeoutSeconds = 30,
        [switch]$VisibleOnly,
        [switch]$Required,
        [string]$Description = ''
    )
    $search = @{}
    foreach ($key in 'Root', 'Name', 'NamePattern', 'AutomationId', 'AutomationIdPattern', 'ControlType') {
        $value = Get-Variable -Name $key -ValueOnly
        if ($null -ne $value -and "$value" -ne '') { $search[$key] = $value }
    }
    $element = Find-Element @search -TimeoutSeconds $TimeoutSeconds -VisibleOnly:$VisibleOnly
    if (-not $element -and $Required) {
        $what = if ($Description) { $Description } else { ($search.Keys | Where-Object { $_ -ne 'Root' } | ForEach-Object { "$_=$($search[$_])" }) -join ', ' }
        throw "element not found after $TimeoutSeconds s: $what"
    }
    return $element
}

# One line describing an element, for the log (masked).
function Format-Element {
    param($Element)
    try {
        $c = $Element.Current
        $type = $c.ControlType.ProgrammaticName -replace '^ControlType\.', ''
        $name = [string]$c.Name
        if ($name.Length -gt 120) { $name = $name.Substring(0, 117) + '...' }
        $flags = @()
        if ($c.IsOffscreen) { $flags += 'offscreen' }
        if (-not $c.IsEnabled) { $flags += 'disabled' }
        if ($c.IsPassword) { $flags += 'password' }
        $text = "[$type] `"$name`""
        if ($c.AutomationId) { $text += " id=$($c.AutomationId)" }
        if ($c.ClassName) { $text += " class=$($c.ClassName)" }
        if ($flags.Count) { $text += " (" + ($flags -join ', ') + ")" }
        return (Protect-UiaText $text)
    } catch {
        return '[element gone]'
    }
}

# Clicks an element: through its pattern (Invoke, Toggle, SelectionItem, ExpandCollapse) when it has one, otherwise
# with the mouse at its clickable point. -Mouse forces the mouse, -Double double-clicks (always with the mouse).
# Returns how it clicked.
function Invoke-Click {
    param([Parameter(Mandatory)]$Element, [switch]$Mouse, [switch]$Double)
    if (-not $Mouse -and -not $Double) {
        $pattern = $null
        if ($Element.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$pattern)) {
            $pattern.Invoke(); Start-Sleep -Milliseconds 400; return 'invoke'
        }
        if ($Element.TryGetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern, [ref]$pattern)) {
            $pattern.Toggle(); Start-Sleep -Milliseconds 400; return 'toggle'
        }
        if ($Element.TryGetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern, [ref]$pattern)) {
            $pattern.Select(); Start-Sleep -Milliseconds 400; return 'select'
        }
        if ($Element.TryGetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern, [ref]$pattern)) {
            $pattern.Expand(); Start-Sleep -Milliseconds 400; return 'expand'
        }
    }
    Show-AppleMusicWindow | Out-Null
    $point = New-Object System.Windows.Point(0, 0)
    if (-not $Element.TryGetClickablePoint([ref]$point)) {
        $rect = $Element.Current.BoundingRectangle
        if ($rect.IsEmpty -or $rect.Width -le 0) { throw "element without a position on screen: $(Format-Element $Element)" }
        $point = New-Object System.Windows.Point(($rect.X + $rect.Width / 2), ($rect.Y + $rect.Height / 2))
    }
    [AmpifyInput]::Click([int]$point.X, [int]$point.Y, [bool]$Double)
    Start-Sleep -Milliseconds 400
    if ($Double) { return 'double click' }
    return 'mouse click'
}

# Types text into an element like a user: focus, Ctrl+A and Delete to clear it, then Unicode key events. -Secret
# changes nothing to the typing; the text is never logged anyway. -UseValuePattern sets the value directly instead,
# which web pages may not notice. Returns how it typed.
function Set-Text {
    param([Parameter(Mandatory)]$Element, [Parameter(Mandatory)][string]$Text, [switch]$Secret, [switch]$NoClear, [switch]$UseValuePattern)
    if ($UseValuePattern) {
        $value = $null
        if ($Element.TryGetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern, [ref]$value) -and -not $value.Current.IsReadOnly) {
            $value.SetValue($Text)
            return 'ValuePattern'
        }
    }
    Show-AppleMusicWindow | Out-Null
    try { $Element.SetFocus() } catch { Invoke-Click -Element $Element -Mouse | Out-Null }
    Start-Sleep -Milliseconds 200
    if (-not $NoClear) {
        Send-Key Control, A
        Send-Key Delete
    }
    $sent = [AmpifyInput]::SendText($Text, 25)
    if ($sent -ne $Text.Length) { throw "incomplete typing: $sent character(s) out of $($Text.Length)" }
    return 'keyboard'
}

# The tree under an element, one line per element, indented by depth (Control view by default, -Raw for every
# element). Returns at most -MaxLines lines, masked.
function Get-TreeDump {
    param([Parameter(Mandatory)]$Root, [int]$MaxDepth = 30, [int]$MaxLines = 400, [switch]$Raw)
    $walker = if ($Raw) { [System.Windows.Automation.TreeWalker]::RawViewWalker } else { [System.Windows.Automation.TreeWalker]::ControlViewWalker }
    $lines = New-Object System.Collections.Generic.List[string]
    $stack = New-Object System.Collections.Generic.Stack[object]
    $stack.Push(@($Root, 0))
    while ($stack.Count -gt 0 -and $lines.Count -lt $MaxLines) {
        $item = $stack.Pop()
        $element = $item[0]
        $depth = [int]$item[1]
        $lines.Add(('  ' * $depth) + (Format-Element $element))
        if ($depth -ge $MaxDepth) { continue }
        # Children are pushed in reverse so they come out in screen order.
        $children = New-Object System.Collections.Generic.List[object]
        try {
            $child = $walker.GetFirstChild($element)
            while ($null -ne $child -and $children.Count -lt 500) {
                $children.Add($child)
                $child = $walker.GetNextSibling($child)
            }
        } catch {
            continue
        }
        for ($i = $children.Count - 1; $i -ge 0; $i--) { $stack.Push(@($children[$i], ($depth + 1))) }
    }
    if ($stack.Count -gt 0) { $lines.Add("... (cut at $MaxLines lines)") }
    return $lines.ToArray()
}

# Prints a tree dump into the job log between two markers, and keeps it in a file when -Path is given.
function Write-TreeDump {
    param([Parameter(Mandatory)]$Root, [Parameter(Mandatory)][string]$Title, [int]$MaxLines = 400, [string]$Path = '', [switch]$Raw)
    $lines = @(Get-TreeDump -Root $Root -MaxLines $MaxLines -Raw:$Raw)
    Write-Host "----- UIA tree: $Title ($($lines.Count) lines)"
    foreach ($line in $lines) { Write-Host "  $line" }
    Write-Host "----- end of the tree: $Title"
    if ($Path) { $lines | Set-Content -LiteralPath $Path -Encoding UTF8 }
}

# Value of a slider (RangeValuePattern), or $null.
function Get-RangeValue {
    param([Parameter(Mandatory)]$Element)
    $range = $null
    if ($Element.TryGetCurrentPattern([System.Windows.Automation.RangeValuePattern]::Pattern, [ref]$range)) {
        return [double]$range.Current.Value
    }
    return $null
}

# Short list of what is on screen (buttons, links, fields, texts), for a log line when a step fails.
function Write-VisibleSummary {
    param([string]$Title, [int]$Max = 60)
    $types = 'Button', 'Hyperlink', 'Edit', 'Text', 'ListItem', 'TabItem', 'ComboBox', 'CheckBox'
    $lines = @()
    foreach ($window in @(Get-UiaAppWindows)) {
        $lines += "window: $(Format-Element $window)"
        foreach ($type in $types) {
            foreach ($element in @(Find-Element -Root $window -ControlType $type -All -VisibleOnly)) {
                if ($lines.Count -ge $Max) { break }
                $lines += "  $(Format-Element $element)"
            }
        }
    }
    Write-Host "----- on screen: $Title"
    $lines | Select-Object -First $Max | ForEach-Object { Write-Host "  $_" }
}
