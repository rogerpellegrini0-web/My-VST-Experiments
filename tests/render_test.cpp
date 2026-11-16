// Offline test renderer for the Filament DSP engine (no JUCE needed).
//
//  * checks tuning of every model across the keyboard and at several sample rates
//  * checks that voices finish after release and that output stays finite / bounded
//  * writes demo WAV files so the models can be auditioned without a DAW
//
// Usage: filament_render_test [output_dir]
#include "../dsp/Engine.h"
#include "../dsp/Presets.h"

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

// YIN fundamental-frequency estimate. The analysis copy is low-passed (two one-pole stages
// at 3 kHz) so very bright sounds (ff brass, buzz) don't produce octave errors; this does
// not change the measured period.
static double estimatePitch (const float* xIn, int n, double fs)
{
    std::vector<float> lp ((size_t) n);
    {
        const double r = std::exp (-2.0 * (double) kPi * 3000.0 / fs);
        double z1 = 0, z2 = 0;
        for (int i = 0; i < n; ++i) { z1 = (1 - r) * xIn[i] + r * z1; z2 = (1 - r) * z1 + r * z2; lp[(size_t) i] = (float) z2; }
    }
    const float* x = lp.data();
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
                p.strange = u (rng) < 0.3f ? 0.0f : u (rng); p.atToStrange = u (rng);
                p.buzz = u (rng) < 0.5f ? 0.0f : u (rng);
                p.symMix = u (rng) < 0.5f ? 0.0f : u (rng); p.symDecay = 0.5f + 19.5f * u (rng);
                p.symKey = (int) (u (rng) * 12) % 12; p.symTuning = (int) (u (rng) * 5) % 5;
                p.voiceMode = (VoiceMode) std::min (2, (int) (u (rng) * 3)); p.glide = u (rng) * 0.3f;
                eng.setParams (p);
                eng.setChannelPressure (u (rng));
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

    // ---------------------------------------------------------------- playing modes
    std::printf ("\n== Mono / legato / glide / expression ==\n");
    {
        const double fs = 48000.0;
        auto pitchAt = [&] (const std::vector<float>& x, double t) {
            return 1200.0 * std::log2 (estimatePitch (x.data() + (int) (t * fs), (int) (0.12 * fs), fs) / 220.0);   // cents re A3
        };
        auto rmsAt = [&] (const std::vector<float>& x, double t0, double t1) {
            double r = 0; for (int i = (int) (t0 * fs); i < (int) (t1 * fs); ++i) r += (double) x[(size_t) i] * x[(size_t) i];
            return std::sqrt (r / ((t1 - t0) * fs));
        };
        auto check = [&] (bool ok, const char* what) { if (! ok) ++failures; std::printf ("  %-58s %s\n", what, ok ? "ok" : "FAIL"); };

        // Script: A3 at 0 s, C4 at 0.6 s (overlapping), release C4 at 1.2 s, release A3 at 1.8 s.
        auto run = [&] (Params p, std::vector<float>& L, int& maxVoices, float pressure = -1.0f) {
            Engine eng; eng.prepare (fs, 512); eng.setParams (p);
            const int total = (int) (2.4 * fs);
            L.assign ((size_t) total, 0.0f); std::vector<float> R ((size_t) total);
            maxVoices = 0;
            if (pressure >= 0.0f) eng.setChannelPressure (pressure);
            for (int pos = 0; pos < total; pos += 256)
            {
                const double t = pos / fs;
                auto at = [&] (double when) { return when >= t && when < t + 256 / fs; };
                if (at (0.0)) eng.noteOn (57, 0.8f);
                if (at (0.6)) eng.noteOn (60, 0.8f);
                if (at (1.2)) eng.noteOff (60);
                if (at (1.8)) eng.noteOff (57);
                eng.render (L.data() + pos, R.data() + pos, std::min (256, total - pos));
                maxVoices = std::max (maxVoices, eng.activeVoiceCount());
            }
        };

        std::vector<float> L; int mv = 0;
        Params p = comboParams (2, 1);                 // reed / cylindrical
        p.vibratoDepth = 0; p.bodyMix = 0; p.attack = 0.05f;

        p.voiceMode = VoiceMode::Legato; run (p, L, mv);
        check (mv == 1, "legato: a single voice");
        check (std::fabs (pitchAt (L, 0.40)) < 10.0, "legato: first note A3");
        check (std::fabs (pitchAt (L, 0.95) - 300.0) < 10.0, "legato: slurs to C4");
        check (std::fabs (pitchAt (L, 1.50)) < 10.0, "legato: returns to held A3 on release");
        // no re-attack: level around the slur stays close to the sustained level
        check (rmsAt (L, 0.60, 0.65) > 0.5 * rmsAt (L, 0.45, 0.55), "legato: no gap / re-attack at the slur");

        p.glide = 0.3f; run (p, L, mv);
        const double mid = pitchAt (L, 0.66);
        check (mid > 30.0 && mid < 270.0, "glide 0.3 s: pitch is between the notes after 60 ms");
        check (std::fabs (pitchAt (L, 1.05) - 300.0) < 12.0, "glide 0.3 s: arrives at C4");

        p.glide = 0.0f; p.voiceMode = VoiceMode::Poly; run (p, L, mv);
        check (mv == 2, "poly: two voices for overlapping notes");

        // Aftertouch as breath: fully assigned, no pressure -> silent; full pressure -> sounds
        p.voiceMode = VoiceMode::Mono; p.atToBreath = 1.0f;
        run (p, L, mv, 0.0f);
        check (rmsAt (L, 0.3, 0.5) < 1e-3, "AT->Breath 100%, no pressure: silent");
        run (p, L, mv, 1.0f);
        check (rmsAt (L, 0.3, 0.5) > 0.03, "AT->Breath 100%, full pressure: sounds");
        run (p, L, mv, 0.7f);
        const double partial = rmsAt (L, 0.3, 0.5);
        run (p, L, mv, 1.0f);
        check (partial < rmsAt (L, 0.3, 0.5), "AT->Breath: more pressure, more sound");

        // Growl stays stable on every sustained exciter
        p.atToBreath = 0.0f; p.growl = 1.0f;
        bool ok = true;
        for (int e = 1; e < kNumExciters; ++e) for (int r = 0; r < kNumResonators; ++r)
        { p.exciter = (Exciter) e; p.resonator = (Resonator) r; run (p, L, mv); ok = ok && allFinite (L) && rmsAt (L, 0.3, 0.5) > 1e-3; }
        check (ok, "growl 100%: all sustained pairings finite and sounding");

        // Mono pluck re-plucks; legato pluck is a hammer-on (no new burst)
        p = comboParams (0, 0); p.vibratoDepth = 0; p.bodyMix = 0; p.voiceMode = VoiceMode::Mono;
        run (p, L, mv);
        const double monoLevel = rmsAt (L, 0.60, 0.65);
        p.voiceMode = VoiceMode::Legato; run (p, L, mv);
        check (std::fabs (pitchAt (L, 0.75) - 300.0) < 10.0, "legato pluck: hammer-on changes pitch");
        check (rmsAt (L, 0.60, 0.65) < monoLevel, "legato pluck: softer than a re-pluck");
    }

    // ---------------------------------------------------------------- strange knob
    std::printf ("\n== Strange knob ==\n");
    {
        const double fs = 48000.0;
        auto check = [&] (bool ok, const char* what) { if (! ok) ++failures; std::printf ("  %-58s %s\n", what, ok ? "ok" : "FAIL"); };

        // every pairing, every 10 %: stable, releases, level within a sane window
        bool stable = true, released = true, levelsOk = true;
        float lo = 0.0f, hi = -200.0f;
        for (int e = 0; e < kNumExciters; ++e)
        for (int r = 0; r < kNumResonators; ++r)
        for (int k = 0; k <= 10; ++k)
        {
            Engine eng; eng.prepare (fs, 512);
            Params p = comboParams (e, r); p.strange = (float) k / 10.0f; p.bodyMix = 0;
            eng.setParams (p);
            Render rr = renderNote (eng, 57, 0.8f, fs, 1.0, 4.0);
            stable = stable && allFinite (rr.L);
            released = released && eng.activeVoiceCount() == 0;
            double rms = 0; const int a = (int) (0.3 * fs), len = (int) (0.5 * fs);
            for (int i = a; i < a + len; ++i) rms += (double) rr.L[(size_t) i] * rr.L[(size_t) i];
            const float db = (float) (10.0 * std::log10 (rms / len + 1e-20));
            if (db > -60.0f) { lo = std::min (lo, db); hi = std::max (hi, db); }
            if (db > -3.0f) levelsOk = false;
            if (verbose && (db < -40.0f || db > -8.0f))
                std::printf ("    %s/%s strange %.1f: %.1f dB\n", kExcNames[e], kResNames[r], p.strange, db);
        }
        check (stable, "all pairings x 0..100 %: output finite");
        check (released, "all pairings x 0..100 %: voices end after release");
        check (levelsOk, "all pairings x 0..100 %: no voice louder than -3 dBFS RMS");
        std::printf ("  (sounding levels span %.1f .. %.1f dBFS RMS)\n", lo, hi);

        // inharmonicity of a plucked A2: mean |deviation| of partials 2..8 from k * f0
        auto inharm = [&] (float strange) {
            Engine eng; eng.prepare (fs, 512);
            Params p = comboParams (0, 0); p.vibratoDepth = 0; p.bodyMix = 0; p.decay = 8; p.strange = strange;
            eng.setParams (p);
            Render rr = renderNote (eng, 45, 0.9f, fs, 0.8, 0.0);
            const int a = (int) (0.05 * fs), len = (int) (0.6 * fs);
            auto mag = [&] (double f) {
                double re = 0, im = 0;
                for (int i = 0; i < len; ++i) { const double w = 0.5 - 0.5 * std::cos (2.0 * (double) kPi * i / len), ph = 2.0 * (double) kPi * f * i / fs;
                                                re += w * rr.L[(size_t) (a + i)] * std::cos (ph); im -= w * rr.L[(size_t) (a + i)] * std::sin (ph); }
                return std::sqrt (re * re + im * im); };
            double dev = 0;
            for (int k = 2; k <= 8; ++k)
            {
                double best = 0, bf = 0;
                for (double f = 110.0 * (k - 0.45); f <= 110.0 * (k + 0.45); f += 0.5) { const double m = mag (f); if (m > best) { best = m; bf = f; } }
                dev += std::fabs (1200.0 * std::log2 (bf / (110.0 * k)));
            }
            return dev / 7.0; };
        const double h0 = inharm (0.0f), h5 = inharm (0.5f);
        std::printf ("  plucked A2 partial deviation: %.1f cents at 0 %%, %.1f cents at 50 %%\n", h0, h5);
        check (h0 < 3.0, "strange 0 %: harmonic partials");
        check (h5 > 20.0, "strange 50 %: clearly inharmonic partials");
    }

    // ---------------------------------------------------------------- buzz + sympathetic strings
    std::printf ("\n== Bridge buzz and sympathetic strings ==\n");
    {
        const double fs = 48000.0;
        auto check = [&] (bool ok, const char* what) { if (! ok) ++failures; std::printf ("  %-58s %s\n", what, ok ? "ok" : "FAIL"); };

        // High-frequency share (energy above ~2.5 kHz relative to total) of plucked notes,
        // with and without buzz. The contact is chaotic note to note (as on a real sitar),
        // so the check is on the average over several notes and velocities.
        auto hfShare = [&] (float buzz, int note, float vel, double t0, double t1) {
            Engine eng; eng.prepare (fs, 256);
            Params p; p.bodyMix = 0; p.vibratoDepth = 0; p.decay = 6; p.damping = 0.2f; p.tone = 0.6f; p.buzz = buzz;
            eng.setParams (p);
            Render rr = renderNote (eng, note, vel, fs, t1 + 0.1, 0.0);
            const double r = std::exp (-2.0 * (double) kPi * 2500.0 / fs);
            double x1 = 0, y1 = 0, y2 = 0, z1 = 0, e = 0, t = 0;
            for (int i = 0; i < (int) (t1 * fs); ++i)
            {
                const double x = rr.L[(size_t) i];
                const double y = r * (y1 + x - x1); x1 = x; y1 = y;
                const double z = r * (z1 + y - y2); y2 = y; z1 = z;
                if (i >= (int) (t0 * fs)) { e += z * z; t += x * x; }
            }
            return 10.0 * std::log10 (e / (t + 1e-20) + 1e-12); };
        double meanGain = 0, minGain = 1e9; int cases = 0;
        for (int note : { 40, 52, 64 })
        for (float vel : { 0.6f, 0.75f, 0.9f })
        {
            const double g = hfShare (0.8f, note, vel, 0.5, 1.5) - hfShare (0.0f, note, vel, 0.5, 1.5);
            meanGain += g; minGain = std::min (minGain, g); ++cases;
        }
        meanGain /= cases;
        std::printf ("  plucked notes, 0.5-1.5 s, extra energy above 2.5 kHz with buzz 80 %%: mean %+.1f dB, min %+.1f dB\n", meanGain, minGain);
        check (meanGain > 5.0 && minGain > 0.0, "buzz: brighter jawari tone on plucked strings");

        bool stable = true, released = true;
        for (int e = 0; e < kNumExciters; ++e)
        for (int r = 0; r < kNumResonators; ++r)
        for (float buzz : { 0.5f, 1.0f })
        {
            Engine eng; eng.prepare (fs, 512);
            Params p = comboParams (e, r); p.buzz = buzz; eng.setParams (p);
            Render rr = renderNote (eng, 57, 0.8f, fs, 1.0, 4.0);
            stable = stable && allFinite (rr.L) && peakOf (rr.L) < 1.0f;
            released = released && eng.activeVoiceCount() == 0;
        }
        check (stable, "buzz 50/100 % on all pairings: finite, below full scale");
        check (released, "buzz 50/100 % on all pairings: voices end after release");

        // sympathetic strings: a short, damped note; listen to what keeps ringing
        auto tail = [&] (int note, float mix, int tuning, double& pitch) {
            Engine eng; eng.prepare (fs, 256);
            Params p; p.bodyMix = 0; p.vibratoDepth = 0; p.decay = 0.4f; p.release = 0.05f;
            p.symMix = mix; p.symKey = 9; p.symTuning = tuning; p.symDecay = 6.0f;
            eng.setParams (p);
            Render rr = renderNote (eng, note, 0.9f, fs, 0.3, 2.0);
            const int a = (int) (1.0 * fs), len = (int) (0.5 * fs);
            double e = 0; for (int i = a; i < a + len; ++i) e += (double) rr.L[(size_t) i] * rr.L[(size_t) i];
            pitch = e > 1e-12 ? estimatePitch (rr.L.data() + a, len, fs) : 0.0;
            return 10.0 * std::log10 (e / len + 1e-20); };
        double pitchIn = 0, pitchOut = 0, pitchOff = 0;
        const double inKey = tail (57, 1.0f, 1, pitchIn), outKey = tail (58, 1.0f, 1, pitchOut), off = tail (57, 0.0f, 1, pitchOff);
        std::printf ("  tail 0.7 s after a short A3 / Bb3 (drone on A): %.1f / %.1f dB, off: %.1f dB, ring pitch %.1f Hz\n", inKey, outKey, off, pitchIn);
        check (inKey > -70.0 && inKey > outKey + 10.0, "sympathetic: in-key notes ring on, out-of-key much less");
        check (std::fabs (1200.0 * std::log2 (pitchIn / 220.0)) < 10.0, "sympathetic: rings at the played pitch");
        check (off < -150.0, "sympathetic: Mix 0 adds nothing");
    }

    // ---------------------------------------------------------------- presets
    std::printf ("\n== Factory presets (phrase G3 C4 G4 C5; no pressure / full pressure) ==\n");
    for (const auto& preset : factoryPresets())
    {
        const double fs = 48000.0;
        double levels[2] {}; double worstCents = 0; bool finite = true, released = true;
        for (int pass = 0; pass < 3; ++pass)
        {
            // passes 0/1: level without / with pressure. Pass 2: pitch, with the sympathetic
            // strings muted (a note plus a string ringing a fifth below is genuinely periodic
            // at the lower pitch, which fools any pitch tracker).
            Params pp = preset.params;
            if (pass == 2) pp.symMix = 0.0f;
            Engine eng; eng.prepare (fs, 512); eng.setParams (pp);
            eng.setChannelPressure (pass == 0 ? 0.0f : 1.0f);   // (pass 2 also uses full pressure)
            const int notes[] = { 55, 60, 67, 72 };
            const int perNote = (int) (0.7 * fs), total = perNote * 4 + (int) (5.0 * fs);
            std::vector<float> L ((size_t) total + 512), R ((size_t) total + 512);
            double sum = 0; int count = 0;
            for (int pos = 0; pos < total; pos += 512)
            {
                const int k = pos / perNote;
                if (pos % perNote < 512 && k < 4) { if (k > 0) eng.noteOff (notes[k - 1]); eng.noteOn (notes[k], 0.8f); }
                if (k == 4 && pos % perNote < 512) eng.noteOff (notes[3]);
                eng.render (L.data() + pos, R.data() + pos, 512);
            }
            for (int k = 0; k < 4; ++k)
            {
                const int a = k * perNote + (int) (0.25 * fs), len = (int) (0.35 * fs);
                double r = 0; for (int i = a; i < a + len; ++i) r += (double) L[(size_t) i] * L[(size_t) i];
                sum += r / len; ++count;
                if (std::string (preset.category) != "Strange" && pass == 2)
                {
                    const double expected = 440.0 * std::pow (2.0, (notes[k] - 69) / 12.0);
                    const double c = 1200.0 * std::log2 (estimatePitch (L.data() + a, len, fs) / expected);
                    // octave-equivalent error (the pitch tracker may pick a sub/super-octave)
                    const double ce = std::fabs (std::remainder (c, 1200.0));
                    worstCents = std::max (worstCents, ce);
                }
            }
            if (pass < 2) levels[pass] = 10.0 * std::log10 (sum / count + 1e-20);
            finite = finite && allFinite (L);
            released = released && eng.activeVoiceCount() == 0;
        }
        // attack: sustained realistic presets must speak quickly (level stays within 3 dB
        // of its final value from this point on), averaged over notes
        double settle = 0;
        if (std::string (preset.category) == "Realistic" && preset.params.exciter != Exciter::Pluck)
        {
            int n = 0;
            for (int note : { 48, 60, 72 })
            {
                Params pp = preset.params; pp.symMix = 0; pp.vibratoDepth = 0;
                Engine eng; eng.prepare (fs, 64); eng.setParams (pp);
                Render rr = renderNote (eng, note, 0.85f, fs, 1.4, 0.0);
                auto lv = [&] (double a, double b) { double r = 0; for (int i = (int) (a * fs); i < (int) (b * fs); ++i) r += (double) rr.L[(size_t) i] * rr.L[(size_t) i];
                                                     return 10.0 * std::log10 (r / ((b - a) * fs) + 1e-20); };
                const double fin = lv (1.2, 1.4); double t = 0;
                for (int k = 0; k < 110; ++k) if (lv (k * 0.01, k * 0.01 + 0.01) < fin - 3.0) t = (k + 1) * 0.01;
                settle += t; ++n;
            }
            settle /= n;
        }
        const bool pitched = std::string (preset.category) != "Strange";
        const bool ok = finite && released && levels[0] > -38.0 && levels[1] > -38.0
                        && levels[0] < -5.0 && levels[1] < -5.0 && (! pitched || worstCents < 15.0) && settle < 0.2;
        if (! ok) ++failures;
        std::printf ("  %-9s %-20s level %6.1f / %6.1f dB   %s  %s  %s\n", preset.category, preset.name, levels[0], levels[1],
                     pitched ? (std::string ("pitch err ") + std::to_string ((int) std::lround (worstCents)) + "c").c_str() : "           ",
                     settle > 0 ? (std::string ("speaks in ") + std::to_string ((int) std::lround (settle * 1000)) + " ms").c_str() : "                ",
                     ok ? "ok" : "FAIL");
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
