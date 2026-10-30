# Audio output checks for the live bench: the default render endpoint (Core Audio), whether spatial objects and the
# metadata stream Apple Music uses for Dolby Atmos are available on it (ISpatialAudioClient), and the Windows spatial
# sound format (Windows Sonic for Headphones). Nothing here plays or records sound.
# Works in Windows PowerShell 5.1 and PowerShell 7; the spatial sound format (WinRT) is read through powershell.exe.

Set-StrictMode -Version Latest

if (-not ('AmkAudio.Endpoints' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

namespace AmkAudio {
    [StructLayout(LayoutKind.Sequential)]
    public struct PropertyKey { public Guid fmtid; public int pid; }

    [StructLayout(LayoutKind.Sequential)]
    public struct PropVariant { public ushort vt; public ushort r1, r2, r3; public IntPtr p1; public IntPtr p2; }

    [ComImport, Guid("886D8EEB-8CF2-4446-8D02-CDBA1DBDCF99"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IPropertyStore {
        [PreserveSig] int GetCount(out int count);
        [PreserveSig] int GetAt(int index, out PropertyKey key);
        [PreserveSig] int GetValue(ref PropertyKey key, out PropVariant value);
        [PreserveSig] int SetValue(ref PropertyKey key, ref PropVariant value);
        [PreserveSig] int Commit();
    }

    [ComImport, Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IMMDevice {
        [PreserveSig] int Activate(ref Guid iid, int clsCtx, IntPtr activationParams, [MarshalAs(UnmanagedType.IUnknown)] out object iface);
        [PreserveSig] int OpenPropertyStore(int access, out IPropertyStore store);
        [PreserveSig] int GetId([MarshalAs(UnmanagedType.LPWStr)] out string id);
        [PreserveSig] int GetState(out int state);
    }

    [ComImport, Guid("0BD7A1BE-7A1A-44DB-8397-CC5392387B5E"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IMMDeviceCollection {
        [PreserveSig] int GetCount(out int count);
        [PreserveSig] int Item(int index, out IMMDevice device);
    }

    [ComImport, Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IMMDeviceEnumerator {
        [PreserveSig] int EnumAudioEndpoints(int dataFlow, int stateMask, out IMMDeviceCollection devices);
        [PreserveSig] int GetDefaultAudioEndpoint(int dataFlow, int role, out IMMDevice device);
        [PreserveSig] int GetDevice([MarshalAs(UnmanagedType.LPWStr)] string id, out IMMDevice device);
        [PreserveSig] int RegisterEndpointNotificationCallback(IntPtr client);
        [PreserveSig] int UnregisterEndpointNotificationCallback(IntPtr client);
    }

    [ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")]
    class MMDeviceEnumeratorObject { }

    [ComImport, Guid("BBF8E066-AAAA-49BE-9A4D-FD2A858EA27F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface ISpatialAudioClient {
        [PreserveSig] int GetStaticObjectPosition(int type, out float x, out float y, out float z);
        [PreserveSig] int GetNativeStaticObjectTypeMask(out int mask);
        [PreserveSig] int GetMaxDynamicObjectCount(out int value);
        [PreserveSig] int GetSupportedAudioObjectFormatEnumerator(out IntPtr enumerator);
        [PreserveSig] int GetMaxFrameCount(IntPtr format, out int frames);
        [PreserveSig] int IsAudioObjectFormatSupported(IntPtr format);
        [PreserveSig] int IsSpatialAudioStreamAvailable(ref Guid streamUuid, IntPtr auxiliaryInfo);
        [PreserveSig] int ActivateSpatialAudioStream(IntPtr activationParams, ref Guid riid, out IntPtr stream);
    }

    // Undocumented but stable since Windows 7: what the Sound control panel uses to pick the default device.
    // Only SetDefaultEndpoint is called; the slots before it keep the vtable order.
    [ComImport, Guid("F8679F50-850A-41CF-9C72-430F290290C8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IPolicyConfig {
        [PreserveSig] int GetMixFormat();
        [PreserveSig] int GetDeviceFormat();
        [PreserveSig] int ResetDeviceFormat();
        [PreserveSig] int SetDeviceFormat();
        [PreserveSig] int GetProcessingPeriod();
        [PreserveSig] int SetProcessingPeriod();
        [PreserveSig] int GetShareMode();
        [PreserveSig] int SetShareMode();
        [PreserveSig] int GetPropertyValue();
        [PreserveSig] int SetPropertyValue();
        [PreserveSig] int SetDefaultEndpoint([MarshalAs(UnmanagedType.LPWStr)] string id, int role);
        [PreserveSig] int SetEndpointVisibility();
    }

    [ComImport, Guid("870AF99C-171D-4F9E-AF0D-E63DF40C2BC9")]
    class PolicyConfigObject { }

    public class Endpoint {
        public string Id;
        public string Name;
    }

    public class SpatialState {
        public string Error;
        public int MaxDynamicObjects = -1;
        public int ObjectStreamHr;
        public int MetadataStreamHr;
    }

    public static class Endpoints {
        const int eRender = 0, eConsole = 0, DEVICE_STATE_ACTIVE = 1, CLSCTX_ALL = 23;

        static IMMDeviceEnumerator Enumerator() { return (IMMDeviceEnumerator)new MMDeviceEnumeratorObject(); }

        static string FriendlyName(IMMDevice device) {
            IPropertyStore store;
            if (device.OpenPropertyStore(0, out store) != 0) return "";
            PropertyKey key = new PropertyKey();
            key.fmtid = new Guid("A45C254E-DF1C-4EFD-8020-67D146A850E0");
            key.pid = 14;  // PKEY_Device_FriendlyName
            PropVariant value;
            if (store.GetValue(ref key, out value) != 0 || value.vt != 31 || value.p1 == IntPtr.Zero) return "";
            return Marshal.PtrToStringUni(value.p1);
        }

        static Endpoint Describe(IMMDevice device) {
            Endpoint e = new Endpoint();
            device.GetId(out e.Id);
            e.Name = FriendlyName(device);
            return e;
        }

        public static Endpoint[] ActiveRender() {
            IMMDeviceCollection devices;
            if (Enumerator().EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, out devices) != 0) return new Endpoint[0];
            int count;
            devices.GetCount(out count);
            Endpoint[] list = new Endpoint[count];
            for (int i = 0; i < count; i++) {
                IMMDevice device;
                devices.Item(i, out device);
                list[i] = Describe(device);
            }
            return list;
        }

        public static Endpoint DefaultRender() {
            IMMDevice device;
            if (Enumerator().GetDefaultAudioEndpoint(eRender, eConsole, out device) != 0 || device == null) return null;
            return Describe(device);
        }

        // Sets the default device for the three roles (console, multimedia, communications). Returns the first error.
        public static int SetDefault(string id) {
            IPolicyConfig policy = (IPolicyConfig)new PolicyConfigObject();
            for (int role = 0; role < 3; role++) {
                int hr = policy.SetDefaultEndpoint(id, role);
                if (hr != 0) return hr;
            }
            return 0;
        }

        // What ISpatialAudioClient says on the default render device: dynamic objects, and whether the plain object
        // stream and the metadata stream (Dolby Atmos) can be opened. 0 = available.
        public static SpatialState Spatial() {
            SpatialState state = new SpatialState();
            IMMDevice device;
            if (Enumerator().GetDefaultAudioEndpoint(eRender, eConsole, out device) != 0 || device == null) {
                state.Error = "aucun périphérique de sortie par défaut";
                return state;
            }
            Guid iid = new Guid("BBF8E066-AAAA-49BE-9A4D-FD2A858EA27F");
            object raw;
            int hr = device.Activate(ref iid, CLSCTX_ALL, IntPtr.Zero, out raw);
            if (hr != 0 || raw == null) {
                state.Error = string.Format("ISpatialAudioClient indisponible (0x{0:X8})", hr);
                return state;
            }
            ISpatialAudioClient client = (ISpatialAudioClient)raw;
            int max;
            if (client.GetMaxDynamicObjectCount(out max) == 0) state.MaxDynamicObjects = max;
            Guid objects = new Guid("BAB5F473-B423-477B-85F5-B5A332A04153");
            Guid metadata = new Guid("BBC9C907-48D5-4A2E-A0C7-F7F0D67C1FB1");
            state.ObjectStreamHr = client.IsSpatialAudioStreamAvailable(ref objects, IntPtr.Zero);
            state.MetadataStreamHr = client.IsSpatialAudioStreamAvailable(ref metadata, IntPtr.Zero);
            return state;
        }
    }
}
'@
}

# Active render endpoints (name and id).
function Get-RenderEndpoints {
    return @([AmkAudio.Endpoints]::ActiveRender())
}

# Default render endpoint, or $null.
function Get-DefaultRenderEndpoint {
    return [AmkAudio.Endpoints]::DefaultRender()
}

function Set-DefaultRenderEndpoint {
    param([Parameter(Mandatory)][string]$Id)
    return [AmkAudio.Endpoints]::SetDefault($Id)
}

function Format-HResult {
    param([int]$Value)
    if ($Value -eq 0) { return 'oui' }
    return ('non (0x{0:X8})' -f $Value)
}

# One line on ISpatialAudioClient for the log.
function Get-SpatialAudioSummary {
    $state = [AmkAudio.Endpoints]::Spatial()
    if ($state.Error) { return $state.Error }
    return "objets dynamiques max : $($state.MaxDynamicObjects) ; flux d'objets : $(Format-HResult $state.ObjectStreamHr) ; " +
        "flux à métadonnées (Atmos) : $(Format-HResult $state.MetadataStreamHr)"
}

function Get-SpatialAudioState {
    return [AmkAudio.Endpoints]::Spatial()
}

# Spatial sound format of the default render device, read (and set with -Enable) through the WinRT class
# SpatialAudioDeviceConfiguration. WinRT types load only in Windows PowerShell 5.1, so this runs in powershell.exe.
# Returns the lines that script printed.
function Invoke-SpatialSoundConfig {
    param([switch]$Enable)
    $script = @'
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
try {
    Add-Type -AssemblyName System.Runtime.WindowsRuntime
    $null = [Windows.Media.Devices.MediaDevice,Windows.Media.Devices,ContentType=WindowsRuntime]
    $null = [Windows.Media.Audio.SpatialAudioDeviceConfiguration,Windows.Media.Audio,ContentType=WindowsRuntime]
    $null = [Windows.Media.Audio.SpatialAudioFormatSubtype,Windows.Media.Audio,ContentType=WindowsRuntime]
    $null = [Windows.Media.Audio.SetDefaultSpatialAudioFormatResult,Windows.Media.Audio,ContentType=WindowsRuntime]
    $id = [Windows.Media.Devices.MediaDevice]::GetDefaultAudioRenderId([Windows.Media.Devices.AudioDeviceRole]::Default)
    if (-not $id) { 'aucun périphérique de sortie par défaut (WinRT)'; exit 0 }
    $config = [Windows.Media.Audio.SpatialAudioDeviceConfiguration]::GetForDeviceId($id)
    $sonic = [Windows.Media.Audio.SpatialAudioFormatSubtype]::WindowsSonic
    $atmos = [Windows.Media.Audio.SpatialAudioFormatSubtype]::DolbyAtmosForHeadphones
    function Show-State {
        "son spatial pris en charge : $($config.IsSpatialAudioSupported)"
        "Windows Sonic pris en charge : $($config.IsSpatialAudioFormatSupported($sonic))"
        "Dolby Atmos for Headphones pris en charge : $($config.IsSpatialAudioFormatSupported($atmos))"
        $active = if ($config.ActiveSpatialAudioFormat -eq $sonic) { 'Windows Sonic' } elseif ($config.ActiveSpatialAudioFormat) { $config.ActiveSpatialAudioFormat } else { 'aucun' }
        $default = if ($config.DefaultSpatialAudioFormat -eq $sonic) { 'Windows Sonic' } elseif ($config.DefaultSpatialAudioFormat) { $config.DefaultSpatialAudioFormat } else { 'aucun' }
        "format spatial actif : $active ; par défaut : $default"
    }
    Show-State
    if (__ENABLE__ -and $config.ActiveSpatialAudioFormat -ne $sonic) {
        $asTask = [System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
            $_.Name -eq 'AsTask' -and $_.IsGenericMethod -and $_.GetParameters().Count -eq 1 -and
            $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1'
        } | Select-Object -First 1
        $operation = $config.SetDefaultSpatialAudioFormatAsync($sonic)
        $task = $asTask.MakeGenericMethod([Windows.Media.Audio.SetDefaultSpatialAudioFormatResult]).Invoke($null, @($operation))
        $null = $task.Wait(30000)
        "activation de Windows Sonic : $($task.Result.Status)"
        Start-Sleep -Seconds 2
        Show-State
    }
} catch {
    "son spatial (WinRT) : erreur : $($_.Exception.Message)"
}
'@
    $script = $script.Replace('__ENABLE__', $(if ($Enable) { '$true' } else { '$false' }))
    $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($script))
    $previous = [Console]::OutputEncoding
    try {
        [Console]::OutputEncoding = [System.Text.Encoding]::UTF8
        $output = & powershell.exe -NoProfile -NonInteractive -EncodedCommand $encoded 2>&1
    } finally {
        [Console]::OutputEncoding = $previous
    }
    return @($output | ForEach-Object { [string]$_ })
}
