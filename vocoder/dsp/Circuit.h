#pragma once
// Component values of the 16-channel vocoder (Bode Vocoder model 7702, later built under
// licence by Moog Music), read from the schematic in the Bode user manual:
// control board 7702-4C (long board) and filter board 7702-3C.
//
// Everything the DSP does is derived from these numbers, so the filter frequencies,
// bandwidths, skirts and envelope speeds follow the hardware.

#include "AnalogFilter.h"
#include <array>

namespace vocoder
{
constexpr int kNumChannels = 16;

// Panel legends between the jacks. They are the channel band EDGES:
// channel 1 = 50-159 Hz, channel 2 = 159-200 Hz ... channel 16 = 4032-5080 Hz.
constexpr std::array<int, kNumChannels + 1> kPanelEdgesHz {
    50, 159, 200, 252, 317, 400, 504, 635, 800, 1008, 1270, 1600, 2016, 2504, 3200, 4032, 5080
};

// ---------------------------------------------------------------------------
// Channels 2-16: two cascaded multiple-feedback band-pass stages, stagger-tuned
// (each stage Q ~ 7.8, the second tuned ~18 % above the first), giving a flat-topped
// 1/3-octave band with 12 dB/octave asymptotic skirts on each side, steeper near the band.
//
// Table from the schematic. Stage 1 = R1..R4 + C1,C2. Stage 2 = R5..R8 + C3,C4.
// (R4 = R3 and R8 = R7 are bias-balancing resistors and don't affect the response.)
struct MfbChannel
{
    double C;              // C1 = C2 = C3 = C4, farads
    double R1, R2, R3;     // stage 1
    double R5, R6, R7;     // stage 2
};

constexpr double kK = 1.0e3, pF = 1.0e-12, uF = 1.0e-6;

// Index 0 is unused (channel 1 is the Sallen-Key low-pass below).
constexpr std::array<MfbChannel, kNumChannels> kMfbChannels {{
    { 0, 0, 0, 0, 0, 0, 0 },
    { 20000 * pF, 210 * kK, 3.16 * kK, 750 * kK, 178 * kK, 2.67 * kK, 634 * kK },  //  2
    { 20000 * pF, 169 * kK, 2.49 * kK, 590 * kK, 143 * kK, 2.10 * kK, 499 * kK },  //  3
    { 20000 * pF, 133 * kK, 1.96 * kK, 464 * kK, 113 * kK, 1.69 * kK, 392 * kK },  //  4
    { 10000 * pF, 210 * kK, 3.16 * kK, 750 * kK, 178 * kK, 2.67 * kK, 634 * kK },  //  5
    { 10000 * pF, 169 * kK, 2.49 * kK, 590 * kK, 143 * kK, 2.10 * kK, 499 * kK },  //  6
    { 10000 * pF, 133 * kK, 1.96 * kK, 464 * kK, 113 * kK, 1.69 * kK, 392 * kK },  //  7
    {  5000 * pF, 210 * kK, 3.16 * kK, 750 * kK, 178 * kK, 2.67 * kK, 634 * kK },  //  8
    {  5000 * pF, 169 * kK, 2.49 * kK, 590 * kK, 143 * kK, 2.10 * kK, 499 * kK },  //  9
    {  5000 * pF, 133 * kK, 1.96 * kK, 464 * kK, 113 * kK, 1.69 * kK, 392 * kK },  // 10
    {  2500 * pF, 210 * kK, 3.16 * kK, 750 * kK, 178 * kK, 2.67 * kK, 634 * kK },  // 11
    {  2500 * pF, 169 * kK, 2.49 * kK, 590 * kK, 143 * kK, 2.10 * kK, 499 * kK },  // 12
    {  2500 * pF, 133 * kK, 1.96 * kK, 464 * kK, 113 * kK, 1.69 * kK, 392 * kK },  // 13
    {  2500 * pF, 107 * kK, 1.58 * kK, 374 * kK, 90.9 * kK, 1.33 * kK, 316 * kK }, // 14
    {  2500 * pF, 84.5 * kK, 1.24 * kK, 294 * kK, 71.5 * kK, 1.05 * kK, 249 * kK }, // 15
    {  2500 * pF, 66.5 * kK, 1.00 * kK, 237 * kK, 56.2 * kK, 0.825 * kK, 200 * kK }, // 16
}};

// Channel 1 (50-159 Hz): two identical unity-gain Sallen-Key low-pass stages
// (analyzer A201A/B: R201-R202, R204-R205 = 56k; C201/C203 = 22 nF, C202/C204 = 10 nF).
// fc = 191.6 Hz, Q = 0.74 each; overall -3 dB at ~157 Hz, 24 dB/octave.
inline AnalogBiquad channel1Stage() { return sallenKeyLowpass (56 * kK, 56 * kK, 22000 * pF, 10000 * pF); }

inline AnalogBiquad bandStage (int channel /*1..16*/, int stage /*0,1*/)
{
    if (channel == 1)
        return channel1Stage();
    const auto& c = kMfbChannels[(size_t) channel - 1];
    return stage == 0 ? mfbBandpass (c.R1, c.R2, c.R3, c.C, c.C)
                      : mfbBandpass (c.R5, c.R6, c.R7, c.C, c.C);
}

// ---------------------------------------------------------------------------
// Analyzer rectifiers: precision full-wave rectifiers (gain 1) whose summing amplifier is
// also an RC integrator, followed by a Sallen-Key smoothing filter.
//   Channel 1:    R212 40.2k || C205 0.22 uF   -> 18.0 Hz pole
//                 R214 = R215 = 300k, C206 0.1 uF (|| R246 620k), C207 0.047 uF -> 7.7 Hz, Q 0.59
//   Channels 2-16: R234 20k || C212 0.1 uF     -> 79.6 Hz pole
//                 R236 = R237 = 220k, C213 0.022 uF (|| R252 470k), C214 0.01 uF -> 48.8 Hz, Q 0.60
// That is the "response time in the order of 6 milliseconds" of the Bode brochure.
inline double envelopePoleHz (int channel)
{
    return channel == 1 ? 1.0 / (2 * kPi * 40.2 * kK * 0.22 * uF)
                        : 1.0 / (2 * kPi * 20 * kK * 0.1 * uF);
}
inline AnalogBiquad envelopeSmoother (int channel)
{
    return channel == 1 ? sallenKeyLowpass (300 * kK, 300 * kK, 0.1 * uF, 0.047 * uF, 620 * kK)
                        : sallenKeyLowpass (220 * kK, 220 * kK, 0.022 * uF, 0.01 * uF, 470 * kK);
}

// ---------------------------------------------------------------------------
// Program pre-filter, A402C (marked "this circuit added above S/N 108"):
// unity-gain Sallen-Key high-pass, C435 = C436 = 0.022 uF, R500 20k, R501 39k
// -> 259 Hz, Q 0.70. It feeds the analyzer and the hiss/buzz discriminator.
inline AnalogBiquad programPreFilter() { return sallenKeyHighpass (0.022 * uF, 20 * kK, 39 * kK); }

// ---------------------------------------------------------------------------
// Hiss/buzz discriminator: the program is split at ~1.85 kHz; each half is rectified and
// smoothed like an analyzer channel, then compared.
//   Buzz (low) side:  A402D Sallen-Key LP, R423 = R424 = 27k, C406 4700 pF, C407 2200 pF -> 1833 Hz
//   Hiss (high) side: A402A Sallen-Key HP, C412 = C413 2200 pF, R448 27k, R449 56k     -> 1860 Hz
//   Each: rectifier/integrator 20k || 0.1 uF (79.6 Hz), then Sallen-Key LP
//         22k / 22k, 0.1 uF, 0.051 uF -> 101 Hz.
inline AnalogBiquad discriminatorLowSplit()  { return sallenKeyLowpass (27 * kK, 27 * kK, 4700 * pF, 2200 * pF); }
inline AnalogBiquad discriminatorHighSplit() { return sallenKeyHighpass (2200 * pF, 27 * kK, 56 * kK); }
inline AnalogBiquad discriminatorSmoother()  { return sallenKeyLowpass (22 * kK, 22 * kK, 0.1 * uF, 0.051 * uF); }
inline double discriminatorPoleHz()          { return 1.0 / (2 * kPi * 20 * kK * 0.1 * uF); }

// ---------------------------------------------------------------------------
// High-frequency channel (5080-15000 Hz), A405A/B:
//   Sallen-Key HP, C422 = C423 = 750 pF, R481 = R482 = 42.2k, gain 1 + 66.5k/113k = 1.59
//   -> 5029 Hz, Q 0.71 (Butterworth)
//   Sallen-Key LP, R485 = R486 = 10k, C424 1500 pF, C425 750 pF -> 15.0 kHz, Q 0.71
inline AnalogBiquad hfChannelHighpass() { return sallenKeyHighpass (750 * pF, 42.2 * kK, 42.2 * kK, 1.0 + 66.5 / 113.0); }
inline AnalogBiquad hfChannelLowpass()  { return sallenKeyLowpass (10 * kK, 10 * kK, 1500 * pF, 750 * pF); }

// ---------------------------------------------------------------------------
// Noise generator: a reverse-biased transistor junction (Q302) followed by a unity-gain
// Sallen-Key HP (C304 = C305 = 2200 pF, R318 = R319 = 220k -> 329 Hz, Q 0.5) and an RC HP
// (C306 0.1 uF into 9.1k || 10k -> 334 Hz).
inline AnalogBiquad noiseHighpass() { return sallenKeyHighpass (2200 * pF, 220 * kK, 220 * kK); }
inline double noiseRcHighpassHz()   { return 1.0 / (2 * kPi * 0.1 * uF * (9.1 * kK * 10 * kK / (19.1 * kK))); }

// ---------------------------------------------------------------------------
// Gates (hiss/buzz switching): the comparator output charges a capacitor through
// 1k + diode and it discharges through 100k; a PNP follower and 39k set the OTA bias.
//   C = 0.1 uF -> attack ~0.1 ms, release time constant 10 ms.
constexpr double kGateAttackSeconds  = 1 * kK * 0.1 * uF;
constexpr double kGateReleaseSeconds = 100 * kK * 0.1 * uF;

// Synthesizer VCAs: a CA3080 OTA with a 20k/470 ohm input attenuator. The OTA's tanh
// transfer gives the slight compression of a hot carrier. Below: OTA input (volts) when
// the carrier is at the overload-LED threshold (an estimate; the carrier summing-amp gains
// are not all legible on the schematic).
constexpr double kOtaInputAtNominal = 0.040;   // 40 mV peak
constexpr double kOtaTwoVt          = 0.0517;  // 2 kT/q at room temperature
} // namespace vocoder
