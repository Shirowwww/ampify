using AtmosLeveler;

// Measures what the default output device plays, in memory only: nothing is written to disk.
// usage: AtmosLeveler [seconds] [--selftest]
if (args.Contains("--selftest"))
    return SelfTest();

int seconds = args.Length > 0 && int.TryParse(args[0], out var s) && s is > 0 and <= 3600 ? s : 30;
using var capture = new LoopbackCapture();
var meter = new LoudnessMeter(capture.SampleRate);
Console.WriteLine($"Loopback {capture.SampleRate} Hz, {capture.Channels} ch, {seconds} s");
var next = 5.0;
while (meter.Seconds < seconds)
{
    Thread.Sleep(20);
    capture.Drain((samples, frames) => meter.Add(samples, frames, capture.Channels));
    if (meter.Seconds >= next)
    {
        Console.WriteLine($"{meter.Seconds,5:0}s  short-term {meter.ShortTerm(),6:0.0} LUFS  integrated {meter.Integrated(),6:0.0} LUFS");
        next += 5;
    }
}
Console.WriteLine($"RESULT integrated {meter.Integrated():0.00} LUFS, sample peak {20 * Math.Log10(meter.SamplePeak):0.0} dBFS");
return 0;

// BS.1770: a 997 Hz sine at 0 dBFS reads -3.01 LUFS on one channel, 0.0 LUFS on both.
static int SelfTest()
{
    const int fs = 96000;
    var meter = new LoudnessMeter(fs);
    double amplitude = Math.Pow(10, -20 / 20.0);
    var buffer = new float[fs * 2];
    for (int i = 0; i < fs; i++)
        buffer[2 * i] = buffer[2 * i + 1] = (float)(amplitude * Math.Sin(2 * Math.PI * 997 * i / fs));
    for (int pass = 0; pass < 10; pass++) meter.Add(buffer, fs, 2);
    double lufs = meter.Integrated();
    const double expected = -20.0;
    Console.WriteLine($"selftest: stereo 997 Hz sine at -20 dBFS -> {lufs:0.00} LUFS (expected {expected:0.00})");
    return Math.Abs(lufs - expected) < 0.1 ? 0 : 1;
}
