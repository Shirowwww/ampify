using System.Runtime.InteropServices;

namespace AtmosLeveler;

// Minimal Core Audio COM interop for a shared-mode loopback capture of the
// default render endpoint. Unused vtable slots are kept so the order stays right.

[ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")]
internal class MMDeviceEnumeratorCom { }

internal enum EDataFlow { Render = 0, Capture = 1, All = 2 }
internal enum ERole { Console = 0, Multimedia = 1, Communications = 2 }

[ComImport, Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
internal interface IMMDeviceEnumerator
{
    int EnumAudioEndpoints(EDataFlow dataFlow, uint stateMask, out IntPtr devices);
    int GetDefaultAudioEndpoint(EDataFlow dataFlow, ERole role, out IMMDevice device);
}

[ComImport, Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
internal interface IMMDevice
{
    int Activate(ref Guid iid, uint clsCtx, IntPtr activationParams, [MarshalAs(UnmanagedType.IUnknown)] out object iface);
}

[ComImport, Guid("1CB9AD4C-DBFA-4c32-B178-C2F568A703B2"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
internal interface IAudioClient
{
    int Initialize(int shareMode, uint streamFlags, long bufferDuration, long periodicity, IntPtr format, IntPtr sessionGuid);
    int GetBufferSize(out uint frames);
    int GetStreamLatency(out long latency);
    int GetCurrentPadding(out uint frames);
    int IsFormatSupported(int shareMode, IntPtr format, out IntPtr closest);
    int GetMixFormat(out IntPtr format);
    int GetDevicePeriod(out long defaultPeriod, out long minimumPeriod);
    int Start();
    int Stop();
    int Reset();
    int SetEventHandle(IntPtr handle);
    int GetService(ref Guid iid, [MarshalAs(UnmanagedType.IUnknown)] out object service);
}

[ComImport, Guid("C8ADBD64-E71E-48a0-A4DE-185C395CD317"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
internal interface IAudioCaptureClient
{
    int GetBuffer(out IntPtr data, out uint frames, out uint flags, out ulong devicePosition, out ulong qpcPosition);
    int ReleaseBuffer(uint frames);
    int GetNextPacketSize(out uint frames);
}

[StructLayout(LayoutKind.Sequential, Pack = 2)]
internal struct WaveFormatEx
{
    public ushort FormatTag;
    public ushort Channels;
    public uint SamplesPerSec;
    public uint AvgBytesPerSec;
    public ushort BlockAlign;
    public ushort BitsPerSample;
    public ushort ExtraSize;
}

/// <summary>Loopback capture of the default render device, delivered as float frames.</summary>
internal sealed class LoopbackCapture : IDisposable
{
    private const int SharedMode = 0;
    private const uint StreamFlagsLoopback = 0x00020000;
    private const uint BufferFlagsSilent = 0x2;
    private const uint ClsctxAll = 0x17;
    private const long BufferDuration100ns = 2_000_000; // 200 ms
    private const ushort WaveFormatIeeeFloat = 3;
    private const ushort WaveFormatExtensible = 0xFFFE;

    private readonly IAudioClient _client;
    private readonly IAudioCaptureClient _capture;

    public int SampleRate { get; }
    public int Channels { get; }

    public LoopbackCapture()
    {
        var enumerator = (IMMDeviceEnumerator)new MMDeviceEnumeratorCom();
        Check(enumerator.GetDefaultAudioEndpoint(EDataFlow.Render, ERole.Multimedia, out var device));
        var clientIid = typeof(IAudioClient).GUID;
        Check(device.Activate(ref clientIid, ClsctxAll, IntPtr.Zero, out var clientObj));
        _client = (IAudioClient)clientObj;

        Check(_client.GetMixFormat(out var formatPtr));
        try
        {
            var format = Marshal.PtrToStructure<WaveFormatEx>(formatPtr);
            bool isFloat = format.FormatTag == WaveFormatIeeeFloat ||
                           (format.FormatTag == WaveFormatExtensible && format.BitsPerSample == 32);
            if (!isFloat)
                throw new NotSupportedException($"Mix format {format.FormatTag}/{format.BitsPerSample}-bit is not float.");
            SampleRate = (int)format.SamplesPerSec;
            Channels = format.Channels;
            Check(_client.Initialize(SharedMode, StreamFlagsLoopback, BufferDuration100ns, 0, formatPtr, IntPtr.Zero));
        }
        finally
        {
            Marshal.FreeCoTaskMem(formatPtr);
        }

        var captureIid = typeof(IAudioCaptureClient).GUID;
        Check(_client.GetService(ref captureIid, out var captureObj));
        _capture = (IAudioCaptureClient)captureObj;
        Check(_client.Start());
    }

    /// <summary>Drains every packet available now; interleaved samples go to <paramref name="sink"/>.</summary>
    public void Drain(Action<float[], int> sink)
    {
        Check(_capture.GetNextPacketSize(out var packet));
        while (packet > 0)
        {
            Check(_capture.GetBuffer(out var data, out var frames, out var flags, out _, out _));
            var samples = new float[frames * Channels];
            if ((flags & BufferFlagsSilent) == 0)
                Marshal.Copy(data, samples, 0, samples.Length);
            Check(_capture.ReleaseBuffer(frames));
            sink(samples, (int)frames);
            Check(_capture.GetNextPacketSize(out packet));
        }
    }

    public void Dispose() => _client.Stop();

    private static void Check(int hr)
    {
        if (hr < 0) Marshal.ThrowExceptionForHR(hr);
    }
}
