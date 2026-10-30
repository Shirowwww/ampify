namespace AtmosLeveler;

/// <summary>
/// ITU-R BS.1770-4 loudness of the first two channels: K-weighting, 400 ms blocks
/// with 75 % overlap, absolute (-70 LUFS) and relative (-10 LU) gating.
/// </summary>
internal sealed class LoudnessMeter
{
    private const double AbsoluteGateLufs = -70.0;
    private const double RelativeGateLu = -10.0;
    private const double ShortTermSeconds = 3.0;

    private readonly Biquad[] _shelf;
    private readonly Biquad[] _highPass;
    private readonly int _hop;               // 100 ms of frames
    private readonly List<double> _hopEnergy = new(); // mean square per 100 ms, summed over channels
    private double _accumulated;
    private int _accumulatedFrames;

    public double SamplePeak { get; private set; }

    public LoudnessMeter(int sampleRate)
    {
        _hop = sampleRate / 10;
        _shelf = [Biquad.Shelf(sampleRate), Biquad.Shelf(sampleRate)];
        _highPass = [Biquad.HighPass(sampleRate), Biquad.HighPass(sampleRate)];
    }

    public void Add(float[] interleaved, int frames, int channels)
    {
        int used = Math.Min(channels, 2);
        for (int f = 0; f < frames; f++)
        {
            for (int c = 0; c < used; c++)
            {
                double x = interleaved[f * channels + c];
                SamplePeak = Math.Max(SamplePeak, Math.Abs(x));
                double y = _highPass[c].Process(_shelf[c].Process(x));
                _accumulated += y * y;
            }
            if (++_accumulatedFrames == _hop)
            {
                _hopEnergy.Add(_accumulated / _hop);
                _accumulated = 0;
                _accumulatedFrames = 0;
            }
        }
    }

    public double Seconds => _hopEnergy.Count / 10.0;

    public double ShortTerm()
    {
        int hops = (int)(ShortTermSeconds * 10);
        if (_hopEnergy.Count < hops) return double.NegativeInfinity;
        return ToLufs(_hopEnergy.Skip(_hopEnergy.Count - hops).Average());
    }

    public double Integrated()
    {
        var blocks = new List<double>();
        for (int i = 3; i < _hopEnergy.Count; i++)
            blocks.Add((_hopEnergy[i] + _hopEnergy[i - 1] + _hopEnergy[i - 2] + _hopEnergy[i - 3]) / 4);
        var aboveAbsolute = blocks.Where(b => ToLufs(b) > AbsoluteGateLufs).ToList();
        if (aboveAbsolute.Count == 0) return double.NegativeInfinity;
        double relativeGate = ToLufs(aboveAbsolute.Average()) + RelativeGateLu;
        var gated = aboveAbsolute.Where(b => ToLufs(b) > relativeGate).ToList();
        return ToLufs(gated.Average());
    }

    private static double ToLufs(double meanSquare) => -0.691 + 10 * Math.Log10(meanSquare);

    private sealed class Biquad(double b0, double b1, double b2, double a1, double a2)
    {
        private double _z1, _z2;

        public double Process(double x)
        {
            double y = b0 * x + _z1;
            _z1 = b1 * x - a1 * y + _z2;
            _z2 = b2 * x - a2 * y;
            return y;
        }

        // Coefficients from BS.1770 re-derived for any sample rate (as in libebur128).
        public static Biquad Shelf(int fs)
        {
            const double f0 = 1681.974450955533, gainDb = 3.999843853973347, q = 0.7071752369554196;
            double k = Math.Tan(Math.PI * f0 / fs);
            double vh = Math.Pow(10, gainDb / 20);
            double vb = Math.Pow(vh, 0.4996667741545416);
            double a0 = 1 + k / q + k * k;
            return new Biquad((vh + vb * k / q + k * k) / a0, 2 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0,
                2 * (k * k - 1) / a0, (1 - k / q + k * k) / a0);
        }

        public static Biquad HighPass(int fs)
        {
            const double f0 = 38.13547087602444, q = 0.5003270373238773;
            double k = Math.Tan(Math.PI * f0 / fs);
            double a0 = 1 + k / q + k * k;
            return new Biquad(1, -2, 1, 2 * (k * k - 1) / a0, (1 - k / q + k * k) / a0);
        }
    }
}
