#pragma once
// Discrete-time models of the analog op-amp filter stages in the vocoder.
//
// Every filter is first written as its exact analog (s-domain) transfer function,
// computed from the resistor and capacitor values on the schematic (see Circuit.h).
// It is then mapped to a digital biquad with the bilinear transform, prewarped at the
// stage's own natural frequency. So each stage's centre/corner frequency is exact at any
// sample rate, and so are its peak gain and Q (up to small warping near Nyquist).

#include <cmath>

namespace vocoder
{
constexpr double kPi = 3.14159265358979323846;

// Analog second-order section: H(s) = (b0 + b1 s + b2 s^2) / (a0 + a1 s + a2 s^2)
struct AnalogBiquad
{
    double b0 = 1, b1 = 0, b2 = 0;
    double a0 = 1, a1 = 0, a2 = 0;

    double naturalFrequencyHz() const { return std::sqrt (a0 / a2) / (2.0 * kPi); }
    double q() const                  { return std::sqrt (a0 * a2) / a1; }

    // Magnitude of the analog response at f (Hz), used by the tests.
    double magnitudeAt (double f) const
    {
        const double w = 2.0 * kPi * f;
        const double nr = b0 - b2 * w * w, ni = b1 * w;
        const double dr = a0 - a2 * w * w, di = a1 * w;
        return std::sqrt ((nr * nr + ni * ni) / (dr * dr + di * di));
    }
};

// Transposed direct form II biquad, double precision (the low channels sit at
// 50-200 Hz, where float coefficients lose accuracy at 96 kHz and above).
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    void setAnalog (const AnalogBiquad& p, double sampleRate, double prewarpHz)
    {
        // Keep the prewarp frequency safely below Nyquist.
        prewarpHz = std::fmin (prewarpHz, 0.45 * sampleRate);
        const double wp = 2.0 * kPi * prewarpHz;
        const double k  = wp / std::tan (wp / (2.0 * sampleRate));
        const double kk = k * k;

        const double B0 = p.b0 + p.b1 * k + p.b2 * kk;
        const double B1 = 2.0 * (p.b0 - p.b2 * kk);
        const double B2 = p.b0 - p.b1 * k + p.b2 * kk;
        const double A0 = p.a0 + p.a1 * k + p.a2 * kk;
        const double A1 = 2.0 * (p.a0 - p.a2 * kk);
        const double A2 = p.a0 - p.a1 * k + p.a2 * kk;

        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0;
        a1 = A1 / A0; a2 = A2 / A0;
    }

    void reset() { z1 = z2 = 0; }

    double process (double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// First-order RC lowpass or highpass (exact pole mapping via bilinear transform).
struct OnePole
{
    double b0 = 1, b1 = 0, a1 = 0, z = 0;

    void setLowpass (double fcHz, double sampleRate)
    {
        const double k = std::tan (kPi * std::fmin (fcHz, 0.45 * sampleRate) / sampleRate);
        b0 = k / (1 + k); b1 = b0; a1 = (k - 1) / (1 + k);
    }
    void setHighpass (double fcHz, double sampleRate)
    {
        const double k = std::tan (kPi * std::fmin (fcHz, 0.45 * sampleRate) / sampleRate);
        b0 = 1 / (1 + k); b1 = -b0; a1 = (k - 1) / (1 + k);
    }
    void reset() { z = 0; }

    double process (double x)
    {
        const double y = b0 * x + z;
        z = b1 * x - a1 * y;
        return y;
    }
};

// ---------------------------------------------------------------------------
// Analog prototypes of the op-amp topologies used on the boards

// Multiple-feedback (Deliyannis) band-pass, as used for analyzer and synthesizer
// channels 2-16. R1 input, R2 node to ground, C1 node to output, C2 node to (-) input,
// R3 (-) input to output. (R4 on the schematic only balances the (+) input bias current.)
// The stage is inverting; two stages in series restore the polarity.
inline AnalogBiquad mfbBandpass (double R1, double R2, double R3, double C1, double C2)
{
    AnalogBiquad p;
    p.b0 = 0; p.b1 = -1.0 / (R1 * C1); p.b2 = 0;
    p.a2 = 1.0;
    p.a1 = (C1 + C2) / (R3 * C1 * C2);
    p.a0 = (1.0 / R1 + 1.0 / R2) / (R3 * C1 * C2);
    return p;
}

// Unity-gain Sallen-Key low-pass: Ra, Rb series resistors, C1 from the middle node to the
// output, C2 from the (+) input to ground. Rf is an optional resistor in parallel with C1
// (R246/R252 in the envelope smoothers); pass 0 for none.
inline AnalogBiquad sallenKeyLowpass (double Ra, double Rb, double C1, double C2, double Rf = 0)
{
    AnalogBiquad p;
    p.b0 = 1; p.b1 = 0; p.b2 = 0;
    p.a0 = 1;
    p.a1 = C2 * (Ra + Rb + (Rf > 0 ? Ra * Rb / Rf : 0.0));
    p.a2 = C1 * C2 * Ra * Rb;
    return p;
}

// Sallen-Key high-pass with equal capacitors C, R1 from the middle node to the output,
// R2 from the (+) input to ground, and non-inverting gain K.
inline AnalogBiquad sallenKeyHighpass (double C, double R1, double R2, double K = 1.0)
{
    AnalogBiquad p;
    p.b0 = 0; p.b1 = 0; p.b2 = K;
    p.a2 = 1;
    p.a1 = (2.0 / R2 + (1.0 - K) / R1) / C;
    p.a0 = 1.0 / (C * C * R1 * R2);
    return p;
}
} // namespace vocoder
