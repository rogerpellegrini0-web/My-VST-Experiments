// Offline tests for the vocoder circuit model, plus demo renders.
//   vocoder_test <output dir>
// Returns non-zero if any check fails.

#include "VocoderEngine.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace vocoder;

static int failures = 0;
#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        if (! (cond)) { std::printf ("FAIL: " __VA_ARGS__); std::printf ("\n"); ++failures; } \
    } while (0)

// ---------------------------------------------------------------------------
static double digitalMagnitude (const Biquad& b, double f, double fs)
{
    const std::complex<double> z1 = std::polar (1.0, -2.0 * kPi * f / fs);
    const auto num = b.b0 + b.b1 * z1 + b.b2 * z1 * z1;
    const auto den = 1.0 + b.a1 * z1 + b.a2 * z1 * z1;
    return std::abs (num / den);
}

struct BandResponse { double peakHz, peakDb, lowHz, highHz; };

static BandResponse measureBand (int ch, double fs)
{
    Biquad st[2];
    for (int s = 0; s < 2; ++s)
    {
        auto proto = bandStage (ch, s);
        st[s].setAnalog (proto, fs, proto.naturalFrequencyHz());
    }
    std::vector<double> f, m;
    for (double x = 10; x < std::min (20000.0, 0.49 * fs); x *= 1.0005)
    {
        f.push_back (x);
        m.push_back (digitalMagnitude (st[0], x, fs) * digitalMagnitude (st[1], x, fs));
    }
    const auto it = std::max_element (m.begin(), m.end());
    const size_t pk = (size_t) (it - m.begin());
    const double lim = *it / std::sqrt (2.0);
    size_t lo = pk, hi = pk;
    while (lo > 0 && m[lo] > lim) --lo;
    while (hi + 1 < m.size() && m[hi] > lim) ++hi;
    return { f[pk], 20 * std::log10 (*it), ch == 1 ? 0.0 : f[lo], f[hi] };
}

// ---------------------------------------------------------------------------
// Test signals

struct Rng { std::uint32_t s = 1; double operator()() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s / 4294967296.0 * 2 - 1; } };

// Two-pole resonator (formant)
struct Reson
{
    double a1 = 0, a2 = 0, g = 0, y1 = 0, y2 = 0;
    void set (double f, double bw, double fs)
    {
        const double r = std::exp (-kPi * bw / fs);
        a1 = 2 * r * std::cos (2 * kPi * f / fs); a2 = -r * r; g = 1 - r;
    }
    double operator() (double x) { const double y = g * x + a1 * y1 + a2 * y2; y2 = y1; y1 = y; return y; }
};

// A crude formant-synthesised "voice": vowels a-e-i-o-u with "s" sounds between them.
static std::vector<float> makeVoice (double fs, double seconds, double level = 0.09)  // peaks at -12 dBFS
{
    struct Vowel { double f1, f2, f3; };
    const Vowel vowels[] = { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 }, { 570, 840, 2410 }, { 300, 870, 2240 } };
    std::vector<float> out ((size_t) (seconds * fs));
    Rng rng;
    Reson r[3];
    Reson sNoise; sNoise.set (6000, 3000, fs);
    double phase = 0;
    const double segment = 0.55;   // seconds per syllable: 0.4 vowel + 0.15 "s"
    for (size_t n = 0; n < out.size(); ++n)
    {
        const double t = n / fs;
        const int seg = (int) (t / segment);
        const double u = t - seg * segment;
        const auto& v = vowels[seg % 5];
        r[0].set (v.f1, 80, fs); r[1].set (v.f2, 100, fs); r[2].set (v.f3, 140, fs);

        const double f0 = 120 * (1 + 0.01 * std::sin (2 * kPi * 5 * t));
        phase += f0 / fs;
        if (phase >= 1) phase -= 1;
        const double glottal = phase < 0.4 ? std::sin (kPi * phase / 0.4) * std::sin (kPi * phase / 0.4) : 0.0;
        const double src = glottal - 0.25;

        double env = 0, hiss = 0;
        if (u < 0.4) env = std::min (1.0, std::min (u / 0.02, (0.4 - u) / 0.03));
        else         hiss = std::min (1.0, std::min ((u - 0.4) / 0.01, (segment - u) / 0.02));

        const double voiced = (r[0] (src) * 1.0 + r[1] (src) * 0.5 + r[2] (src) * 0.25) * env;
        const double unvoiced = sNoise (rng()) * hiss * 0.6;
        out[n] = (float) (voiced * 4 + unvoiced * 2.5);
    }
    // normalise: peak = level x 2.8 (about 0.7 for the default level)
    float pk = 1e-9f;
    for (float v : out) pk = std::max (pk, std::abs (v));
    for (auto& v : out) v *= (float) (level * 2.8 / pk);
    return out;
}

// Narrow pulse wave (10 % duty) - the carrier recommended in the Bode instructions -
// playing a chord, or a sawtooth.
static std::vector<float> makeCarrier (double fs, double seconds, bool saw, double level = 0.25)
{
    const double notes[] = { 110.0, 138.6, 164.8, 220.0 };
    std::vector<float> out ((size_t) (seconds * fs));
    double ph[4] = { 0, 0.3, 0.6, 0.1 };
    for (size_t n = 0; n < out.size(); ++n)
    {
        double s = 0;
        for (int i = 0; i < 4; ++i)
        {
            ph[i] += notes[i] / fs; if (ph[i] >= 1) ph[i] -= 1;
            s += saw ? (2 * ph[i] - 1) : (ph[i] < 0.1 ? 1.0 : -0.111);
        }
        out[n] = (float) (level * s / 4);
    }
    return out;
}

static void writeWav (const std::string& path, const std::vector<float>& x, double fs)
{
    std::ofstream f (path, std::ios::binary);
    auto u32 = [&f] (std::uint32_t v) { f.write ((const char*) &v, 4); };
    auto u16 = [&f] (std::uint16_t v) { f.write ((const char*) &v, 2); };
    float peak = 1e-9f;
    for (float v : x) peak = std::max (peak, std::abs (v));
    const float g = peak > 0.98f ? 0.98f / peak : 1.0f;
    f.write ("RIFF", 4); u32 (36 + (std::uint32_t) x.size() * 2); f.write ("WAVEfmt ", 8);
    u32 (16); u16 (1); u16 (1); u32 ((std::uint32_t) fs); u32 ((std::uint32_t) fs * 2); u16 (2); u16 (16);
    f.write ("data", 4); u32 ((std::uint32_t) x.size() * 2);
    for (float v : x) u16 ((std::uint16_t) (std::int16_t) std::lround (std::clamp (v * g, -1.0f, 1.0f) * 32767));
}

static double rms (const std::vector<float>& x, size_t from = 0, size_t to = 0)
{
    if (to == 0) to = x.size();
    double s = 0;
    for (size_t i = from; i < to; ++i) s += (double) x[i] * x[i];
    return std::sqrt (s / std::max<size_t> (1, to - from));
}

static std::vector<float> run (Engine& e, const std::vector<float>& prog, const std::vector<float>& car, const Params& p,
                               std::vector<Indicators>* lamps = nullptr)
{
    std::vector<float> out (prog.size());
    const int block = 256;
    for (size_t i = 0; i < prog.size(); i += block)
    {
        const int n = (int) std::min<size_t> (block, prog.size() - i);
        e.process (prog.data() + i, car.data() + i, out.data() + i, n, p);
        if (lamps) lamps->push_back (e.indicators());
    }
    return out;
}

// Spectral centroid via a bank of the analyzer filters (cheap and good enough here)
static double centroid (const std::vector<float>& x, double fs)
{
    double num = 0, den = 0;
    for (int ch = 2; ch <= kNumChannels; ++ch)
    {
        Biquad st[2];
        for (int s = 0; s < 2; ++s) { auto pr = bandStage (ch, s); st[s].setAnalog (pr, fs, pr.naturalFrequencyHz()); }
        double e = 0;
        for (float v : x) { const double y = st[1].process (st[0].process (v)); e += y * y; }
        const double fc = std::sqrt ((double) kPanelEdgesHz[(size_t) ch - 1] * kPanelEdgesHz[(size_t) ch]);
        num += e * std::log2 (fc); den += e;
    }
    return std::exp2 (num / den);
}

// ---------------------------------------------------------------------------
int main (int argc, char** argv)
{
    const std::string outDir = argc > 1 ? argv[1] : ".";

    // 1. Filter bank: band edges against the panel legends, at several sample rates.
    std::printf ("Analyzer/synthesizer band-pass filters (from schematic R/C values):\n");
    std::printf ("  ch  panel band     stage1        stage2         -3 dB band       peak\n");
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        for (int ch = 1; ch <= kNumChannels; ++ch)
        {
            const auto r = measureBand (ch, fs);
            const double lo = kPanelEdgesHz[(size_t) ch - 1], hi = kPanelEdgesHz[(size_t) ch];
            if (fs == 48000.0)
            {
                const auto s0 = bandStage (ch, 0), s1 = bandStage (ch, 1);
                std::printf ("  %2d  %4.0f-%4.0f Hz  %6.1f Hz Q%4.2f  %6.1f Hz Q%4.2f  %6.1f-%6.1f Hz  %+.1f dB\n",
                             ch, lo, hi, s0.naturalFrequencyHz(), s0.q(), s1.naturalFrequencyHz(), s1.q(),
                             r.lowHz, r.highHz, r.peakDb);
            }
            if (ch > 1)
            {
                CHECK (std::abs (r.lowHz / lo - 1) < 0.05, "ch%d lower edge %.1f vs panel %.0f (fs %.0f)", ch, r.lowHz, lo, fs);
                CHECK (std::abs (r.highHz / hi - 1) < 0.05, "ch%d upper edge %.1f vs panel %.0f (fs %.0f)", ch, r.highHz, hi, fs);
                CHECK (r.peakDb > 0.2 && r.peakDb < 2.5, "ch%d peak gain %.2f dB", ch, r.peakDb);
            }
            else
            {
                CHECK (std::abs (r.highHz / hi - 1) < 0.05, "ch1 corner %.1f vs panel %.0f", r.highHz, hi);
            }
        }
    }

    // Skirt slope: 12 dB/octave asymptotically (two 2-pole band-passes) - check channel 9
    // one and two octaves away.
    {
        Biquad st[2];
        for (int s = 0; s < 2; ++s) { auto pr = bandStage (9, s); st[s].setAnalog (pr, 96000, pr.naturalFrequencyHz()); }
        auto mag = [&] (double f) { return 20 * std::log10 (digitalMagnitude (st[0], f, 96000) * digitalMagnitude (st[1], f, 96000)); };
        const double fc = 900, a1 = mag (fc * 4) - mag (fc * 8), a2 = mag (fc / 4) - mag (fc / 8);
        std::printf ("  channel 9 skirts, 2->3 octaves away: %.1f dB/oct above, %.1f dB/oct below\n", a1, a2);
        CHECK (a1 > 10 && a1 < 14 && a2 > 10 && a2 < 14, "ch9 skirt slope %.1f / %.1f dB/oct", a1, a2);
        std::printf ("  channel 9 attenuation at 1/3 octave beyond the band edges: %.1f / %.1f dB\n",
                     mag (1008 * 1.26) - mag (900), mag (800 / 1.26) - mag (900));
    }

    // Other filters
    {
        auto pr = programPreFilter(); std::printf ("  program pre-filter HP %.0f Hz Q%.2f\n", pr.naturalFrequencyHz(), pr.q());
        auto lo = discriminatorLowSplit(), hi = discriminatorHighSplit();
        std::printf ("  discriminator split LP %.0f Hz / HP %.0f Hz\n", lo.naturalFrequencyHz(), hi.naturalFrequencyHz());
        auto h1 = hfChannelHighpass(), h2 = hfChannelLowpass();
        std::printf ("  HF channel HP %.0f Hz Q%.2f gain %.2f, LP %.0f Hz Q%.2f\n", h1.naturalFrequencyHz(), h1.q(), h1.b2,
                     h2.naturalFrequencyHz(), h2.q());
        CHECK (std::abs (h1.naturalFrequencyHz() - 5029) < 30, "HF channel HP");
        CHECK (std::abs (h2.naturalFrequencyHz() - 15005) < 50, "HF channel LP");
    }

    // 2. Envelope follower speed (channel 9, 900 Hz tone burst)
    {
        const double fs = 48000;
        Engine e; e.prepare (fs);
        Params p; p.preFilter = false;
        std::vector<float> prog ((size_t) (0.2 * fs)), car (prog.size(), 0.0f), out (prog.size());
        for (size_t n = (size_t) (0.05 * fs); n < prog.size(); ++n) prog[n] = (float) (0.5 * std::sin (2 * kPi * 900 * n / fs));
        std::vector<double> cv;
        for (size_t n = 0; n < prog.size(); ++n)
        {
            e.process (&prog[n], &car[n], &out[n], 1, p);
            cv.push_back (e.indicators().analyzer[8]);
        }
        const double final = cv.back();
        size_t t10 = 0, t90 = 0;
        for (size_t n = 0; n < cv.size(); ++n) { if (! t10 && cv[n] > 0.1 * final) t10 = n; if (! t90 && cv[n] > 0.9 * final) t90 = n; }
        const double riseMs = (t90 - t10) * 1000 / fs;
        std::printf ("Envelope follower (ch 2-16): 10-90%% rise %.1f ms, steady CV %.3f V for a 0.5 V tone\n", riseMs, final);
        CHECK (riseMs > 2 && riseMs < 12, "envelope rise time %.1f ms", riseMs);
        // ripple at 2 x 900 Hz should be small
        double mn = 1e9, mx = -1e9;
        for (size_t n = cv.size() - 2000; n < cv.size(); ++n) { mn = std::min (mn, cv[n]); mx = std::max (mx, cv[n]); }
        std::printf ("  ripple %.2f %%\n", 100 * (mx - mn) / final);
        CHECK ((mx - mn) / final < 0.05, "envelope ripple");
    }

    // 3. Hiss/buzz discriminator
    {
        const double fs = 48000;
        Engine e; e.prepare (fs);
        Params p;
        auto voice = makeVoice (fs, 2.75);
        auto carrier = makeCarrier (fs, 2.75, false);
        std::vector<Indicators> lamps;
        run (e, voice, carrier, p, &lamps);
        // classify each block by the expected segment
        int vowelBuzz = 0, vowelBlocks = 0, sHiss = 0, sBlocks = 0;
        for (size_t b = 0; b < lamps.size(); ++b)
        {
            const double t = (b + 0.5) * 256 / fs;
            const double u = std::fmod (t, 0.55);
            if (u > 0.08 && u < 0.36) { ++vowelBlocks; vowelBuzz += lamps[b].buzz > 0.5 && lamps[b].hiss < 0.1; }
            if (u > 0.44 && u < 0.52) { ++sBlocks; sHiss += lamps[b].hiss > 0.5 && lamps[b].buzz < 0.1; }
        }
        std::printf ("Discriminator: BUZZ lamp on %d/%d vowel blocks, HISS lamp on %d/%d \"s\" blocks\n", vowelBuzz, vowelBlocks, sHiss, sBlocks);
        CHECK (vowelBuzz > 0.9 * vowelBlocks, "vowels detected as buzz");
        CHECK (sHiss > 0.9 * sBlocks, "s detected as hiss");
    }

    // 4. Vocoding, level, stability, at several sample rates
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        Engine e; e.prepare (fs);
        Params p;
        auto voice = makeVoice (fs, 2.75);
        auto carrier = makeCarrier (fs, 2.75, true);
        auto out = run (e, voice, carrier, p);
        bool finite = true; float peak = 0;
        for (float v : out) { finite &= std::isfinite (v); peak = std::max (peak, std::abs (v)); }
        std::printf ("fs %6.0f: voice rms %.3f, carrier rms %.3f -> vocoder rms %.3f, peak %.3f\n", fs, rms (voice), rms (carrier), rms (out), peak);
        CHECK (finite, "non-finite output at %.0f", fs);
        // At DAW levels (voice peaking at -12 dBFS) with the knobs at their defaults, the
        // output should come out at roughly the voice's own level, without spikes.
        const double relDb = 20 * std::log10 (rms (out) / rms (voice));
        const double crestDb = 20 * std::log10 (peak / rms (out));
        std::printf ("          output %.1f dB relative to the voice, crest factor %.1f dB\n", relDb, crestDb);
        CHECK (relDb > -6 && relDb < 6, "vocoder output %.1f dB relative to the voice at fs %.0f", relDb, fs);
        CHECK (crestDb < 20, "output crest factor %.1f dB (spikes?)", crestDb);

        // silence in -> silence out
        Engine e2; e2.prepare (fs);
        std::vector<float> zero (voice.size(), 0.0f);
        auto o2 = run (e2, zero, carrier, p);
        CHECK (rms (o2, o2.size() / 2) < 1e-4, "carrier leaks with silent program: %.6f", rms (o2, o2.size() / 2));
    }

    // 5. Status OUT = program passes straight through (after the preamp)
    {
        const double fs = 48000;
        Engine e; e.prepare (fs);
        Params p; p.vocoderOn = false;
        auto voice = makeVoice (fs, 1.0);
        auto carrier = makeCarrier (fs, 1.0, true);
        auto out = run (e, voice, carrier, p);
        // compare with the input after the same AC coupling as the input jack
        OnePole ac; ac.setHighpass (8.0, fs);
        double err = 0;
        for (size_t n = 0; n < out.size(); ++n)
        {
            const double ref = ac.process (voice[n]);
            if (n > (size_t) fs / 2) err = std::max (err, std::abs (out[n] - ref));
        }
        std::printf ("Bypass (STATUS OUT): max deviation from input %.4f\n", err);
        CHECK (err < 0.02, "bypass not transparent: %.4f", err);
    }

    // 6. Sample/Hold freezes the spectrum: hold during a vowel, then feed silence
    {
        const double fs = 48000;
        Engine e; e.prepare (fs);
        Params p;
        auto voice = makeVoice (fs, 0.3);
        auto carrier = makeCarrier (fs, 1.3, true);
        voice.resize (carrier.size(), 0.0f);
        std::vector<float> out (voice.size());
        for (size_t i = 0; i < voice.size(); i += 256)
        {
            p.sampleHold = i > (size_t) (0.25 * fs);
            const int n = (int) std::min<size_t> (256, voice.size() - i);
            e.process (voice.data() + i, carrier.data() + i, out.data() + i, n, p);
        }
        const double held = rms (out, (size_t) (0.8 * fs), (size_t) (1.3 * fs));
        std::printf ("Sample/Hold: output rms %.3f one second after the voice stopped\n", held);
        CHECK (held > 0.01, "sample/hold does not hold");
    }

    // 7. External patch: the 2-step "chipmunk / Donald Duck" patch raises the formants
    {
        const double fs = 48000;
        auto voice = makeVoice (fs, 2.75);
        auto carrier = makeCarrier (fs, 2.75, true);
        Params p;
        Engine e1; e1.prepare (fs);
        auto normal = run (e1, voice, carrier, p);
        p.externalPatch = true;
        for (int j = 0; j < kNumChannels; ++j) p.patch[(size_t) j] = j >= 2 ? j - 2 : -1;
        Engine e2; e2.prepare (fs);
        auto raised = run (e2, voice, carrier, p);
        const double c1 = centroid (normal, fs), c2 = centroid (raised, fs);
        std::printf ("External patch 2-step up: spectral centroid %.0f Hz -> %.0f Hz\n", c1, c2);
        CHECK (c2 > c1 * 1.3, "formant shift");
        // External patch IN with no cords = silence (apart from the HF channel)
        p.patch.fill (-1);
        p.hfDirect = false; p.mode = Mode::Buzz;
        Engine e3; e3.prepare (fs);
        auto none = run (e3, voice, carrier, p);
        CHECK (rms (none) < 1e-4, "unpatched synth inputs should be silent");
    }

    // 8. Demo renders (48 kHz)
    {
        const double fs = 48000;
        const double len = 2.75 * 2;
        auto voice = makeVoice (fs, len);
        auto pulse = makeCarrier (fs, len, false);
        auto saw = makeCarrier (fs, len, true);
        writeWav (outDir + "/vocoder_demo_program.wav", voice, fs);
        writeWav (outDir + "/vocoder_demo_carrier.wav", pulse, fs);

        auto render = [&] (const char* name, const std::vector<float>& car, Params p)
        {
            Engine e; e.prepare (fs);
            writeWav (outDir + "/vocoder_demo_" + name + ".wav", run (e, voice, car, p), fs);
        };
        Params p;
        render ("pulse_switched", pulse, p);
        p.hfDirect = true;
        render ("pulse_direct", pulse, p);
        render ("saw_direct", saw, p);
        Params chip = p; chip.externalPatch = true;
        for (int j = 0; j < kNumChannels; ++j) chip.patch[(size_t) j] = j >= 2 ? j - 2 : -1;
        render ("chipmunk", saw, chip);
        Params robot = p; robot.externalPatch = true;   // "Space Robot" patch from the Moog manual
        for (int a = 0; a < 7; ++a) robot.patch[(size_t) a + 9] = a;
        robot.patch[0] = 7; robot.patch[1] = 8; robot.patch[3] = 9; robot.patch[4] = 10;
        render ("space_robot", saw, robot);
        Params buzz = p; buzz.mode = Mode::Buzz;
        render ("buzz_only", saw, buzz);
    }

    std::printf (failures ? "\n%d check(s) FAILED\n" : "\nAll checks passed\n", failures);
    return failures ? 1 : 0;
}
