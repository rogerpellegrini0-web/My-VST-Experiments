#pragma once
// Circuit model of the classic 16-channel vocoder (Bode 7702 / its licensed Moog-built
// version), framework-independent so it can be tested offline.
//
// Signal flow (as on the block diagram in the Bode manual):
//
//  PROGRAM -> preamp/overload -> [259 Hz pre-filter] -+-> 16 analyzer band-passes -> rectifiers
//                    |                                |      -> (external patch) -> sample/hold
//                    |                                +-> hiss/buzz discriminator      |
//                    +-> 5-15 kHz channel -> hiss gate (switched) ---------------+     v
//                    +-> bypass                                                  |  16 VCAs
//  CARRIER -> preamp/overload -> buzz gate --+                                   |     |
//  noise generator ----------------------> hiss gate --> balance -> OTA input -> 16 VCAs
//                                                                                |  16 synth band-passes
//                                                                                +-> summing -> status -> OUTPUT

#include "AnalogFilter.h"
#include "Circuit.h"

#include <array>
#include <cstdint>

namespace vocoder
{
enum class Mode { Hiss = 0, HissAndBuzz = 1, Buzz = 2 };

struct Params
{
    bool  power        = true;
    bool  micInput     = false;   // MIC/LINE switch: mic adds 20 dB
    float programLevel = 5.0f;    // 0..10
    float carrierLevel = 5.0f;    // 0..10
    bool  sampleHold   = false;   // IN = hold the spectrum
    bool  externalPatch = false;  // IN = the patch cords are used instead of the normal 1:1 links
    bool  hfDirect     = false;   // 5080-15000 Hz: false = SWITCHED (gated by "s" sounds), true = DIRECT
    float balance      = 0.0f;    // -5 (HISS) .. 0 .. +5 (BUZZ)
    Mode  mode         = Mode::HissAndBuzz;
    bool  vocoderOn    = true;    // STATUS: IN = vocoder, OUT = program passes straight through
    bool  preFilter    = true;    // 259 Hz analyzer pre-filter fitted (units above S/N 108)

    // Patch cords: synthesizer input j is driven by analyzer output patch[j] (0..15),
    // or nothing (-1). Only used when externalPatch is on.
    std::array<int, kNumChannels> patch {};

    Params() { patch.fill (-1); }
};

// Front-panel lamps and meters, updated every block.
struct Indicators
{
    float programPeak = 0;   // preamp output peak, 1.0 = overload threshold
    float carrierPeak = 0;
    float hiss = 0;          // 0..1 lamp brightness
    float buzz = 0;
    std::array<float, kNumChannels> analyzer {};   // analyzer output CVs (volts)
};

class Engine
{
public:
    void prepare (double sampleRate);
    void reset();

    // program/carrier: mono input; out: mono output. Any pointer may alias.
    void process (const float* program, const float* carrier, float* out, int numSamples, const Params& p);

    const Indicators& indicators() const noexcept { return ind; }
    double sampleRate() const noexcept { return fs; }

    // Calibration constants (public for the tests)
    static constexpr double kVcaGainPerVolt = 4.45;   // OTA transconductance x R221, per volt of CV
    static constexpr double kNoiseRms       = 0.35;   // noise source level at the balance control
    static constexpr double kOutputGain     = 1.0 + 84.5 / 20.0;   // output amp A308 (R367/R366); bypass stays at unity
    static constexpr double kComparatorRef  = 0.02;    // envelope difference (V) that swings the comparator rail to rail in 1 ms
    static constexpr double kComparatorBias = 0.0005;  // offset trim, leaning towards BUZZ
    static double levelKnobGain (float knob0to10);    // level pot: 5 = unity, 10 = +24 dB
    static double preampClip (double x);              // op-amp output stage, 4 dB above the LED threshold

private:
    struct Band
    {
        Biquad   analyzer[2], synth[2];
        OnePole  envPole;
        Biquad   envSmooth;
        double   cv = 0;      // analyzer output
        double   held = 0;    // after patch + sample/hold
        void reset();
    };

    double fs = 48000;
    std::array<Band, kNumChannels> bands;

    Biquad  preFilter;
    Biquad  discLowSplit, discHighSplit, discLowSmooth, discHighSmooth;
    OnePole discLowPole, discHighPole;
    double  comparator = 0;    // -1 (hiss) .. +1 (buzz)
    double  compCoef = 0;

    double  buzzGate = 0, hissGate = 0, hfGate = 0;
    double  gateAttack = 0, gateRelease = 0;

    Biquad  hfHighpass, hfLowpass;
    Biquad  noiseHp;
    OnePole noiseRc;
    std::uint32_t rng = 0x12345678u;

    OnePole dcProgram, dcCarrier;

    // smoothed switch/knob positions (avoid zipper noise and clicks)
    double sProgGain = 0, sCarGain = 0, sNoiseGain = 1, sBuzzGain = 1, sStatus = 1, sPower = 1;
    double smoothCoef = 0;

    Indicators ind;

    double noise();
    static double gateGain (double v);
    void   updateGate (double& state, double target);
};
} // namespace vocoder
