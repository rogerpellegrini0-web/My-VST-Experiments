// Offline test renderer for the Filament DSP engine (no JUCE needed).
//
//  * checks tuning of every model across the keyboard and at several sample rates
//  * checks that voices finish after release and that output stays finite / bounded
//  * writes demo WAV files so the models can be auditioned without a DAW
//
// Usage: filament_render_test [output_dir]
#include "../dsp/Engine.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using namespace filament;

static const char* kExcNames[] = { "pluck", "bow", "reed", "lips", "jet" };
static const char* kResNames[] = { "string", "cyl", "cone", "open", "horn" };

// The classic pairings must be in tune across the keyboard; every other pairing must be
// stable and is reported for information.
static bool isClassic (int e, int r)
{
    return (e == 0) || (e == 1 && r == 0) || (e == 2 && (r == 1 || r == 2)) || (e == 3 && r == 4) || (e == 4 && r == 3);
}

static Params comboParams (int e, int r)
{
    Params p;
    p.exciter = (Exciter) e;
    p.resonator = (Resonator) r;
    return p;
}

static void writeWav (const std::string& path, const std::vector<float>& L, const std::vector<float>& R, int fs)
{
    std::ofstream f (path, std::ios::binary);
    auto u32 = [&] (uint32_t v) { f.write ((const char*) &v, 4); };
    auto u16 = [&] (uint16_t v) { f.write ((const char*) &v, 2); };
    const uint32_t frames = (uint32_t) L.size();
    f.write ("RIFF", 4); u32 (36 + frames * 4); f.write ("WAVE", 4);
    f.write ("fmt ", 4); u32 (16); u16 (1); u16 (2); u32 ((uint32_t) fs); u32 ((uint32_t) fs * 4); u16 (4); u16 (16);
    f.write ("data", 4); u32 (frames * 4);
    for (uint32_t i = 0; i < frames; ++i)
    {
        int16_t l = (int16_t) std::lround (clampf (L[i], -1.0f, 1.0f) * 32767.0f);
        int16_t r = (int16_t) std::lround (clampf (R[i], -1.0f, 1.0f) * 32767.0f);
        f.write ((const char*) &l, 2); f.write ((const char*) &r, 2);
    }
}

// YIN fundamental-frequency estimate.
static double estimatePitch (const float* x, int n, double fs)
{
    const int tauMin = (int) (fs / 4500.0), tauMax = std::min (n / 2, (int) (fs / 35.0));
    const int W = n - tauMax;
    std::vector<double> d ((size_t) tauMax + 2, 0.0), c ((size_t) tauMax + 2, 1.0);
    for (int tau = 1; tau <= tauMax + 1; ++tau)
    {
        double s = 0.0;
        for (int j = 0; j < W; ++j) { const double e = x[j] - x[j + tau]; s += e * e; }
        d[(size_t) tau] = s;
    }
    double run = 0.0;
    for (int tau = 1; tau <= tauMax + 1; ++tau)
    {
        run += d[(size_t) tau];
        c[(size_t) tau] = run > 0 ? d[(size_t) tau] * tau / run : 1.0;
    }
    int best = -1;
    for (int tau = std::max (2, tauMin); tau < tauMax; ++tau)
        if (c[(size_t) tau] < 0.12 && c[(size_t) tau] <= c[(size_t) tau - 1] && c[(size_t) tau] <= c[(size_t) tau + 1]) { best = tau; break; }
    if (best < 0)
    {
        best = std::max (2, tauMin);
        for (int tau = best; tau < tauMax; ++tau) if (c[(size_t) tau] < c[(size_t) best]) best = tau;
    }
    const double a = c[(size_t) best - 1], b = c[(size_t) best], cc = c[(size_t) best + 1];
    const double den = a - 2 * b + cc;
    const double shift = std::fabs (den) > 1e-12 ? 0.5 * (a - cc) / den : 0.0;
    return fs / (best + shift);
}

struct Render { std::vector<float> L, R; };

static Render renderNote (Engine& e, int note, float vel, double fs, double holdSec, double tailSec)
{
    Render r;
    const int block = 256;
    const int hold = (int) (holdSec * fs), total = hold + (int) (tailSec * fs);
    r.L.resize ((size_t) total); r.R.resize ((size_t) total);
    e.noteOn (note, vel);
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        if (pos <= hold && pos + n > hold) e.noteOff (note);
        e.render (r.L.data() + pos, r.R.data() + pos, n);
    }
    return r;
}

static bool allFinite (const std::vector<float>& v)
{
    for (float s : v) if (! std::isfinite (s)) return false;
    return true;
}

static float peakOf (const std::vector<float>& v, size_t a = 0, size_t b = SIZE_MAX)
{
    float p = 0; b = std::min (b, v.size());
    for (size_t i = a; i < b; ++i) p = std::max (p, std::fabs (v[i]));
    return p;
}

int main (int argc, char** argv)
{
    const std::string outDir = argc > 1 ? argv[1] : ".";
    const bool verbose = argc > 2;
    int failures = 0;

    // ---------------------------------------------------------------- tuning
    std::printf ("== Tuning, classic pairings (cents error) ==\n");
    const int notes[] = { 28, 40, 52, 64, 76, 88, 96 };
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        for (int e = 0; e < kNumExciters; ++e)
        for (int r = 0; r < kNumResonators; ++r)
        {
            if (! isClassic (e, r)) continue;
            std::printf ("fs=%6.0f %-5s/%-6s", fs, kExcNames[e], kResNames[r]);
            for (int note : notes)
            {
                Engine eng;
                eng.prepare (fs, 512);
                Params p = comboParams (e, r);
                p.vibratoDepth = 0.0f;
                p.bodyMix = 0.0f;
                p.decay = 8.0f;
                eng.setParams (p);
                const bool pluck = e == 0;
                Render rr = renderNote (eng, note, 0.8f, fs, 1.2, 0.0);
                const int start = (int) ((pluck ? 0.15 : 0.6) * fs);
                const int len   = (int) (0.4 * fs);
                const double f0 = estimatePitch (rr.L.data() + start, len, fs);
                const double expected = 440.0 * std::pow (2.0, (note - 69) / 12.0);
                const double cents = 1200.0 * std::log2 (f0 / expected);
                const float level = peakOf (rr.L, (size_t) start, (size_t) (start + len));
                const bool silent = level < 1e-3f;
                // Winds overblow / can't speak at the very extremes of the keyboard; only the
                // mid range is a hard requirement for them.
                const bool required = pluck || e == 1 || (note >= 40 && note <= 88);
                const bool bad = ! allFinite (rr.L) || (required && (silent || std::fabs (cents) > 10.0));
                if (bad) ++failures;
                if (silent) std::printf (" %3d:silent%s", note, bad ? "!" : " ");
                else        std::printf (" %3d:%+6.1f%s", note, cents, bad ? "!" : " ");
            }
            std::printf ("\n");
        }
    }

    // ---------------------------------------------------------------- full matrix
    std::printf ("\n== All pairings at 48 kHz: cents error @ A2/A3/A4, RMS (dBFS) @ A3 ==\n");
    std::printf ("%-12s", "");
    for (int r = 0; r < kNumResonators; ++r) std::printf ("| %-29s", kResNames[r]);
    std::printf ("\n");
    float rmsTable[kNumExciters][kNumResonators] {};
    for (int e = 0; e < kNumExciters; ++e)
    {
        std::printf ("%-12s", kExcNames[e]);
        for (int r = 0; r < kNumResonators; ++r)
        {
            char cell[64]; int pos = 0;
            for (int note : { 45, 57, 69 })
            {
                Engine eng; eng.prepare (48000.0, 512);
                Params p = comboParams (e, r); p.vibratoDepth = 0; p.bodyMix = 0;
                eng.setParams (p);
                Render rr = renderNote (eng, note, 0.8f, 48000.0, 1.0, 0.0);
                if (! allFinite (rr.L)) ++failures;
                const int start = (int) ((e == 0 ? 0.1 : 0.5) * 48000), len = 19200;
                double rms = 0; for (int i = start; i < start + len; ++i) rms += (double) rr.L[(size_t) i] * rr.L[(size_t) i];
                rms = std::sqrt (rms / len);
                const double expected = 440.0 * std::pow (2.0, (note - 69) / 12.0);
                const double c = 1200.0 * std::log2 (estimatePitch (rr.L.data() + start, len, 48000.0) / expected);
                if (note == 57) rmsTable[e][r] = (float) rms;
                if (rms < 1e-4) pos += std::snprintf (cell + pos, sizeof (cell) - (size_t) pos, "   --  ");
                else            pos += std::snprintf (cell + pos, sizeof (cell) - (size_t) pos, "%+6.0f ", std::fabs (c) > 9999 ? 9999.0 : c);
            }
            std::snprintf (cell + pos, sizeof (cell) - (size_t) pos, "%5.1f", 20 * std::log10 (rmsTable[e][r] + 1e-9));
            std::printf ("| %-29s", cell);
        }
        std::printf ("\n");
    }
    if (verbose)
    {
        std::printf ("\nGain trims for -18 dBFS RMS (multiply into kComboGain):\n");
        for (int e = 0; e < kNumExciters; ++e)
        {
            std::printf ("  {");
            for (int r = 0; r < kNumResonators; ++r)
                std::printf (" %5.2ff,", rmsTable[e][r] > 1e-4f ? 0.1259f / rmsTable[e][r] : 1.0f);
            std::printf (" },   // %s\n", kExcNames[e]);
        }
    }

    // ---------------------------------------------------------------- release / levels
    std::printf ("\n== Voice release (48 kHz, A3) ==\n");
    for (int e = 0; e < kNumExciters; ++e)
    for (int r = 0; r < kNumResonators; ++r)
    {
        Engine eng; eng.prepare (48000.0, 512);
        eng.setParams (comboParams (e, r));
        Render rr = renderNote (eng, 57, 0.8f, 48000.0, 1.0, 4.0);
        const bool bad = ! allFinite (rr.L) || eng.activeVoiceCount() != 0;
        if (bad) { ++failures; std::printf ("%s/%s: voices left %d FAIL\n", kExcNames[e], kResNames[r], eng.activeVoiceCount()); }
    }
    std::printf ("all pairings checked\n");

    // ---------------------------------------------------------------- stress
    std::printf ("\n== Stress: 400 random notes per exciter, random resonators + extreme params ==\n");
    for (int e = 0; e < kNumExciters; ++e)
    {
        Engine eng; eng.prepare (48000.0, 512);
        std::mt19937 rng (1234 + e);
        std::uniform_real_distribution<float> u (0.0f, 1.0f);
        Params p = comboParams (e, 0);
        std::vector<float> L (512), R (512);
        bool finite = true; float pk = 0;
        for (int b = 0; b < 4000; ++b)
        {
            if (b % 10 == 0)
            {
                p.resonator = (Resonator) std::min (kNumResonators - 1, (int) (u (rng) * kNumResonators));
                p.tone = u (rng); p.shape = u (rng); p.position = u (rng); p.pressure = u (rng); p.noise = u (rng);
                p.damping = u (rng); p.stiffness = u (rng); p.decay = 0.1f + 30 * u (rng);
                p.attack = 0.001f + u (rng); p.release = 0.01f + 3 * u (rng);
                p.vibratoDepth = u (rng); p.bodyMix = u (rng); p.bodySize = u (rng);
                eng.setParams (p);
                eng.setPitchBend (u (rng) * 2 - 1);
                eng.setModWheel (u (rng));
                eng.noteOn (24 + (int) (u (rng) * 84), 0.05f + u (rng) * 0.95f);
                if (u (rng) < 0.6f) eng.noteOff (24 + (int) (u (rng) * 84));
                if (u (rng) < 0.05f) eng.allNotesOff();
            }
            eng.render (L.data(), R.data(), 512);
            finite = finite && allFinite (L) && allFinite (R);
            pk = std::max (pk, peakOf (L));
        }
        if (! finite) ++failures;
        std::printf ("%-6s finite %s  peak %.3f\n", kExcNames[e], finite ? "yes" : "NO", pk);
    }

    // ---------------------------------------------------------------- demos
    std::printf ("\n== Writing demo WAVs to %s ==\n", outDir.c_str());
    struct Demo { const char* name; int e, r; float attack, vib, body; };
    const Demo demos[] = {
        { "pluck_string", 0, 0, 0.05f, 0.15f, 0.5f },
        { "bow_string",   1, 0, 0.12f, 0.35f, 0.4f },
        { "reed_cyl",     2, 1, 0.04f, 0.10f, 0.0f },
        { "reed_cone",    2, 2, 0.04f, 0.20f, 0.0f },
        { "lips_horn",    3, 4, 0.05f, 0.10f, 0.0f },
        { "jet_open",     4, 3, 0.06f, 0.30f, 0.0f },
        { "bow_cone",     1, 2, 0.10f, 0.20f, 0.2f },
        { "pluck_horn",   0, 4, 0.05f, 0.00f, 0.3f },
        { "reed_string",  2, 0, 0.05f, 0.15f, 0.4f },
    };
    for (const auto& dm : demos)
    {
        const double fs = 48000.0;
        Engine eng; eng.prepare (fs, 512);
        Params p = comboParams (dm.e, dm.r);
        p.attack = dm.attack; p.vibratoDepth = dm.vib; p.bodyMix = dm.body; p.release = 0.4f;
        if (dm.e == 0) p.decay = 5.0f;
        eng.setParams (p);

        const int phrase[][3] = { {50,0,2},{57,2,2},{62,4,2},{64,6,1},{65,7,1},{64,8,2},{62,10,2},
                                  {57,12,4},{50,12,4},{53,12,4} };
        const double beat = 0.32;
        const int total = (int) (19 * beat * fs);
        std::vector<float> L ((size_t) total), R ((size_t) total);
        for (int pos = 0; pos < total; pos += 64)
        {
            const double t0 = pos / fs, t1 = (pos + 64) / fs;
            for (auto& ev : phrase)
            {
                const double on = ev[1] * beat, off = (ev[1] + ev[2]) * beat - 0.03;
                if (on >= t0 && on < t1) eng.noteOn (ev[0], 0.85f);
                if (off >= t0 && off < t1) eng.noteOff (ev[0]);
            }
            eng.render (L.data() + pos, R.data() + pos, std::min (64, total - pos));
        }
        const std::string path = outDir + "/demo_" + dm.name + ".wav";
        writeWav (path, L, R, (int) fs);
        std::printf ("%s  (peak %.3f)\n", path.c_str(), peakOf (L));
    }

    std::printf ("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
