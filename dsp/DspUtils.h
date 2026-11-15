// Filament - waveguide physical modeling synth
// Small, dependency-free DSP building blocks used by the waveguide models.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace filament
{

constexpr float kPi    = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

inline float clampf (float x, float lo, float hi) noexcept { return std::min (hi, std::max (lo, x)); }
inline float lerpf (float a, float b, float t) noexcept { return a + (b - a) * t; }

// Piecewise-linear map of a 0..1 knob so that 0 -> lo, 0.5 -> mid ("classic" value), 1 -> hi.
inline float knob3 (float x, float lo, float mid, float hi) noexcept
{
    x = clampf (x, 0.0f, 1.0f);
    return x < 0.5f ? lerpf (lo, mid, x * 2.0f) : lerpf (mid, hi, x * 2.0f - 1.0f);
}

// Convert a one-pole lowpass coefficient specified at 44.1 kHz to another sample rate
// (keeps the cutoff frequency the same).
inline float poleForRate (float poleAt44k, double sampleRate) noexcept
{
    if (poleAt44k <= 0.0f) return 0.0f;
    return (float) std::pow ((double) poleAt44k, 44100.0 / sampleRate);
}

//==============================================================================
// Fast white noise in [-1, 1).
class Noise
{
public:
    explicit Noise (uint32_t seed = 0x9E3779B9u) : state (seed ? seed : 1u) {}
    void seed (uint32_t s) noexcept { state = s ? s : 1u; }
    float next() noexcept
    {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        return (float) (int32_t) state * (1.0f / 2147483648.0f);
    }
private:
    uint32_t state;
};

//==============================================================================
// Circular delay line with 3rd-order Lagrange fractional reads.
// Convention: call read(d) *before* push(x) in a sample; read(d) then returns the
// value pushed exactly d samples earlier. Minimum delay is 2 samples.
class DelayLine
{
public:
    void setMaxDelay (int maxSamples)
    {
        int size = 16;
        while (size < maxSamples + 8) size <<= 1;
        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;
        writePos = 0;
    }

    void clear() noexcept { std::fill (buffer.begin(), buffer.end(), 0.0f); writePos = 0; }

    void push (float x) noexcept
    {
        buffer[(size_t) writePos] = x;
        writePos = (writePos + 1) & mask;
    }

    float tap (int k) const noexcept { return buffer[(size_t) ((writePos - k) & mask)]; }

    float read (float d) const noexcept
    {
        d = clampf (d, 2.0f, (float) (mask - 4));
        const int   k  = (int) d;
        const float D  = d - (float) k + 1.0f;     // 1 <= D < 2 : centred, best accuracy
        const float d1 = D - 1.0f, d2 = D - 2.0f, d3 = D - 3.0f;
        const float h0 = -d1 * d2 * d3 * (1.0f / 6.0f);
        const float h1 =  D * d2 * d3 * 0.5f;
        const float h2 = -D * d1 * d3 * 0.5f;
        const float h3 =  D * d1 * d2 * (1.0f / 6.0f);
        return h0 * tap (k - 1) + h1 * tap (k) + h2 * tap (k + 1) + h3 * tap (k + 2);
    }

    int capacity() const noexcept { return mask - 4; }

    // Magnitude response of the Lagrange interpolator for delay d at angular frequency w.
    // Used to compensate the (small, but per-period) high-frequency loss inside loops.
    static float interpolationMagnitude (float d, float w) noexcept
    {
        const float D  = d - std::floor (d) + 1.0f;
        const float d1 = D - 1.0f, d2 = D - 2.0f, d3 = D - 3.0f;
        const float h[4] = { -d1 * d2 * d3 * (1.0f / 6.0f), D * d2 * d3 * 0.5f,
                             -D * d1 * d3 * 0.5f, D * d1 * d2 * (1.0f / 6.0f) };
        std::complex<float> H (0.0f, 0.0f);
        for (int k = 0; k < 4; ++k) H += h[k] * std::polar (1.0f, -w * (float) k);
        return std::abs (H);
    }

private:
    std::vector<float> buffer;
    int mask = 0, writePos = 0;
};

//==============================================================================
// y[n] = (1 - p) x[n] + p y[n-1]   (unity DC gain lowpass)
struct OnePoleLP
{
    float p = 0.0f, z = 0.0f;
    void reset() noexcept { z = 0.0f; }
    float process (float x) noexcept { z = (1.0f - p) * x + p * z; return z; }

    static float phaseDelay (float p, float w) noexcept
    {
        return std::atan2 (p * std::sin (w), 1.0f - p * std::cos (w)) / w;
    }
    static float magnitude (float p, float w) noexcept
    {
        return (1.0f - p) / std::sqrt (std::max (1e-12f, 1.0f - 2.0f * p * std::cos (w) + p * p));
    }
};

// First-order allpass  H(z) = (a + z^-1) / (1 + a z^-1). Used for string stiffness (dispersion).
struct Allpass1
{
    float a = 0.0f, x1 = 0.0f, y1 = 0.0f;
    void reset() noexcept { x1 = y1 = 0.0f; }
    float process (float x) noexcept
    {
        const float y = a * x + x1 - a * y1;
        x1 = x; y1 = y;
        return y;
    }
    static float phaseDelay (float a, float w) noexcept
    {
        const std::complex<float> z1 = std::polar (1.0f, -w);
        const std::complex<float> H  = (a + z1) / (1.0f + a * z1);
        float ph = -std::arg (H);
        if (ph < 0.0f) ph += kTwoPi;
        return ph / w;
    }
};

struct DCBlocker
{
    float r = 0.995f, x1 = 0.0f, y1 = 0.0f;
    void setup (double sampleRate, float cutoffHz = 20.0f) noexcept
    {
        r = (float) std::exp (-kTwoPi * cutoffHz / sampleRate);
    }
    void reset() noexcept { x1 = y1 = 0.0f; }
    float process (float x) noexcept
    {
        const float y = x - x1 + r * y1;
        x1 = x; y1 = y;
        return y;
    }
};

// RBJ band-pass (0 dB peak gain), transposed direct form II.
struct BandPass
{
    float b0 = 0, b2 = 0, a1 = 0, a2 = 0, s1 = 0, s2 = 0;
    void setup (double sampleRate, float freq, float q) noexcept
    {
        freq = clampf (freq, 10.0f, (float) sampleRate * 0.45f);
        const float w = kTwoPi * freq / (float) sampleRate;
        const float alpha = std::sin (w) / (2.0f * q);
        const float a0 = 1.0f + alpha;
        b0 = alpha / a0;
        b2 = -alpha / a0;
        a1 = -2.0f * std::cos (w) / a0;
        a2 = (1.0f - alpha) / a0;
    }
    void reset() noexcept { s1 = s2 = 0.0f; }
    float process (float x) noexcept
    {
        const float y = b0 * x + s1;
        s1 = -a1 * y + s2;
        s2 = b2 * x - a2 * y;
        return y;
    }
};

// Second-order allpass  H(z) = (a2 + a1 z^-1 + z^-2) / (1 + a1 z^-1 + a2 z^-2), with poles at
// radius r and angle theta. Lossless; delays frequencies near theta much more than others.
struct Allpass2
{
    float a1 = 0.0f, a2 = 0.0f;
    float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
    void set (float r, float theta) noexcept { a1 = -2.0f * r * std::cos (theta); a2 = r * r; }
    void reset() noexcept { x1 = x2 = y1 = y2 = 0.0f; }
    float process (float x) noexcept
    {
        const float y = a2 * x + a1 * x1 + x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
    static float phaseDelay (float a1, float a2, float w) noexcept
    {
        const std::complex<float> z1 = std::polar (1.0f, -w), z2 = z1 * z1;
        const std::complex<float> H = (a2 + a1 * z1 + z2) / (1.0f + a1 * z1 + a2 * z2);
        float ph = -std::arg (H);
        while (ph < 0.0f) ph += kTwoPi;
        return ph / w;
    }
};

// Cheap smooth saturator ~ tanh(x) for |x| < 3, clamped beyond.
inline float fastTanh (float x) noexcept
{
    x = clampf (x, -3.0f, 3.0f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// Wideband 90-degree phase splitter (two polyphase allpass chains, coefficients by
// Olli Niemitalo). Outputs i and q are in quadrature from ~20 Hz to ~0.98 * Nyquist.
class Hilbert
{
public:
    void reset() noexcept { for (auto& st : state) st = {}; q1 = 0.0f; }
    void process (float x, float& i, float& q) noexcept
    {
        static constexpr float kA[4] = { 0.6923878f * 0.6923878f, 0.9360654322959f * 0.9360654322959f,
                                         0.9882295226860f * 0.9882295226860f, 0.9987488452737f * 0.9987488452737f };
        static constexpr float kB[4] = { 0.4021921162426f * 0.4021921162426f, 0.8561710882420f * 0.8561710882420f,
                                         0.9722909545651f * 0.9722909545651f, 0.9952884791278f * 0.9952884791278f };
        float a = x, b = x;
        for (int k = 0; k < 4; ++k) a = section (state[(size_t) k],     kA[k], a);
        for (int k = 0; k < 4; ++k) b = section (state[(size_t) k + 4], kB[k], b);
        i = a;
        q = q1;     // the second path carries one extra sample of delay
        q1 = b;
    }

private:
    struct Section { float x1 = 0, x2 = 0, y1 = 0, y2 = 0; };
    static float section (Section& s, float c, float x) noexcept
    {
        const float y = c * (x + s.y2) - s.x2;
        s.x2 = s.x1; s.x1 = x; s.y2 = s.y1; s.y1 = y;
        return y;
    }
    std::array<Section, 8> state {};
    float q1 = 0.0f;
};

} // namespace filament
