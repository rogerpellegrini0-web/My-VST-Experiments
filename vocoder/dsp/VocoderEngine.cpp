#include "VocoderEngine.h"

#include <algorithm>
#include <cmath>

namespace vocoder
{
void Engine::Band::reset()
{
    for (auto& b : analyzer) b.reset();
    for (auto& b : synth) b.reset();
    envPole.reset();
    envSmooth.reset();
    cv = held = 0;
}

double Engine::levelKnobGain (float knob)
{
    const double k = std::clamp ((double) knob, 0.0, 10.0) / 10.0;
    return 4.0 * k * k;
}

double Engine::preampClip (double x)
{
    // Linear up to 1.2 x the LED threshold, then a smooth knee into the op-amp rail at 1.6
    // (the "headroom of about 4 dB above this level" of the manual).
    const double a = std::abs (x);
    if (a <= 1.2) return x;
    const double y = 1.2 + 0.4 * std::tanh ((a - 1.2) / 0.4);
    return x < 0 ? -y : y;
}

void Engine::prepare (double sampleRate)
{
    fs = sampleRate;

    for (int ch = 1; ch <= kNumChannels; ++ch)
    {
        auto& b = bands[(size_t) ch - 1];
        for (int s = 0; s < 2; ++s)
        {
            const auto proto = bandStage (ch, s);
            b.analyzer[s].setAnalog (proto, fs, proto.naturalFrequencyHz());
            b.synth[s].setAnalog (proto, fs, proto.naturalFrequencyHz());
        }
        b.envPole.setLowpass (envelopePoleHz (ch), fs);
        const auto sm = envelopeSmoother (ch);
        b.envSmooth.setAnalog (sm, fs, sm.naturalFrequencyHz());
    }

    auto set = [this] (Biquad& bq, const AnalogBiquad& proto) { bq.setAnalog (proto, fs, proto.naturalFrequencyHz()); };
    set (preFilter, programPreFilter());
    set (discLowSplit, discriminatorLowSplit());
    set (discHighSplit, discriminatorHighSplit());
    set (discLowSmooth, discriminatorSmoother());
    set (discHighSmooth, discriminatorSmoother());
    discLowPole.setLowpass (discriminatorPoleHz(), fs);
    discHighPole.setLowpass (discriminatorPoleHz(), fs);
    set (hfHighpass, hfChannelHighpass());
    set (hfLowpass, hfChannelLowpass());
    set (noiseHp, noiseHighpass());
    noiseRc.setHighpass (noiseRcHighpassHz(), fs);

    // AC coupling of the input jacks
    dcProgram.setHighpass (8.0, fs);
    dcCarrier.setHighpass (8.0, fs);

    // Comparator: integrator time constant R434 10k x C411 0.1 uF = 1 ms
    compCoef    = 1.0 / (0.001 * fs);
    gateAttack  = 1.0 - std::exp (-1.0 / (kGateAttackSeconds * fs));
    gateRelease = 1.0 - std::exp (-1.0 / (kGateReleaseSeconds * fs));
    smoothCoef  = 1.0 - std::exp (-1.0 / (0.005 * fs));

    reset();
}

void Engine::reset()
{
    for (auto& b : bands) b.reset();
    preFilter.reset();
    discLowSplit.reset(); discHighSplit.reset();
    discLowSmooth.reset(); discHighSmooth.reset();
    discLowPole.reset(); discHighPole.reset();
    hfHighpass.reset(); hfLowpass.reset();
    noiseHp.reset(); noiseRc.reset();
    dcProgram.reset(); dcCarrier.reset();
    comparator = 0;
    buzzGate = hissGate = hfGate = 0;
    ind = {};
}

double Engine::noise()
{
    // Approximately Gaussian white noise (sum of four uniforms), unit variance.
    double s = 0;
    for (int i = 0; i < 4; ++i)
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        s += (double) rng * (1.0 / 4294967296.0) - 0.5;
    }
    return s * std::sqrt (3.0);
}

// The gate transistor only conducts once its base is a diode drop (0.6 V of the
// ~13 V comparator swing) above ground; from there the OTA bias rises linearly.
double Engine::gateGain (double v)
{
    constexpr double threshold = 0.6 / 13.0;
    return std::clamp ((v - threshold) / (1.0 - threshold), 0.0, 1.0);
}

void Engine::updateGate (double& state, double target)
{
    // Charges through 1k and a diode when the control voltage is higher, always
    // discharges through 100k.
    if (target > state)
        state += (target - state) * gateAttack;
    state -= state * gateRelease;
}

void Engine::process (const float* program, const float* carrier, float* out, int numSamples, const Params& p)
{
    const double progGainTarget = levelKnobGain (p.programLevel) * (p.micInput ? 10.0 : 1.0);
    const double carGainTarget  = levelKnobGain (p.carrierLevel);

    // BALANCE: the pot sits between the noise (HISS) gate and the carrier (BUZZ) gate.
    const double t = (std::clamp ((double) p.balance, -5.0, 5.0) + 5.0) / 10.0;  // 0 = HISS end
    const double noiseGainTarget = std::min (1.0, 2.0 * (1.0 - t));
    const double buzzGainTarget  = std::min (1.0, 2.0 * t);

    const bool hissAllowed = p.mode != Mode::Buzz;
    const bool buzzAllowed = p.mode != Mode::Hiss;

    const double otaDrive = kOtaInputAtNominal / kOtaTwoVt;

    // Which analyzer output drives each synthesizer channel.
    std::array<int, kNumChannels> source {};
    for (int j = 0; j < kNumChannels; ++j)
        source[(size_t) j] = p.externalPatch ? p.patch[(size_t) j] : j;

    double progPeak = 0, carPeak = 0, hissLamp = 0, buzzLamp = 0;

    for (int n = 0; n < numSamples; ++n)
    {
        sProgGain  += (progGainTarget - sProgGain) * smoothCoef;
        sCarGain   += (carGainTarget - sCarGain) * smoothCoef;
        sNoiseGain += (noiseGainTarget - sNoiseGain) * smoothCoef;
        sBuzzGain  += (buzzGainTarget - sBuzzGain) * smoothCoef;
        sStatus    += ((p.vocoderOn ? 1.0 : 0.0) - sStatus) * smoothCoef;
        sPower     += ((p.power ? 1.0 : 0.0) - sPower) * smoothCoef;

        // ---- input preamps
        const double progIn = dcProgram.process (program[n]);
        const double carIn  = dcCarrier.process (carrier[n]);
        const double prog = preampClip (progIn * sProgGain);
        const double car  = preampClip (carIn * sCarGain);
        progPeak = std::max (progPeak, std::abs (prog));
        carPeak  = std::max (carPeak, std::abs (car));

        const double analyzerIn = p.preFilter ? preFilter.process (prog) : prog;
        if (! p.preFilter) preFilter.process (prog);   // keep its state warm

        // ---- hiss/buzz discriminator
        const double lo = discLowSmooth.process (discLowPole.process (std::abs (discLowSplit.process (analyzerIn))));
        const double hi = discHighSmooth.process (discHighPole.process (std::abs (discHighSplit.process (analyzerIn))));
        // Integrating comparator (A404D) swinging rail to rail. Its offset trim leans it
        // towards BUZZ, so with no input the carrier gate stays open (needed for HOLD).
        comparator = std::clamp (comparator + (lo - hi + kComparatorBias) / kComparatorRef * compCoef, -1.0, 1.0);
        const double buzzLine = std::max (0.0, comparator);
        const double hissLine = std::max (0.0, -comparator);
        buzzLamp = std::max (buzzLamp, buzzLine);
        hissLamp = std::max (hissLamp, hissLine);

        updateGate (buzzGate, buzzAllowed ? buzzLine : 0.0);
        updateGate (hissGate, hissAllowed ? hissLine : 0.0);
        updateGate (hfGate, hissAllowed ? (p.hfDirect ? 1.0 : hissLine) : 0.0);

        // ---- carrier / noise mix into the synthesizer VCAs
        const double nz = noiseRc.process (noiseHp.process (noise())) * kNoiseRms;
        const double excitation = car * gateGain (buzzGate) * sBuzzGain
                                + nz  * gateGain (hissGate) * sNoiseGain;
        // CA3080 transfer (input attenuated 20k:470): tanh, normalised to unity small-signal gain
        const double ota = std::tanh (excitation * otaDrive) / otaDrive;

        // ---- 16 channels
        double vocoded = 0;
        for (int ch = 0; ch < kNumChannels; ++ch)
        {
            auto& b = bands[(size_t) ch];
            const double y = b.analyzer[1].process (b.analyzer[0].process (analyzerIn));
            b.cv = b.envSmooth.process (b.envPole.process (std::abs (y)));
        }
        for (int ch = 0; ch < kNumChannels; ++ch)
        {
            auto& b = bands[(size_t) ch];
            if (! p.sampleHold)
            {
                const int src = source[(size_t) ch];
                b.held = src >= 0 && src < kNumChannels ? bands[(size_t) src].cv : 0.0;
            }
            const double cv = std::max (0.0, b.held);
            const double vca = ota * cv * kVcaGainPerVolt;
            vocoded += b.synth[1].process (b.synth[0].process (vca));
        }

        // ---- 5080-15000 Hz channel
        const double hf = hfLowpass.process (hfHighpass.process (prog) * gateGain (hfGate));

        const double vocoder = (vocoded + hf) * kOutputGain;
        const double y = (vocoder * sStatus + prog * (1.0 - sStatus)) * sPower;
        out[n] = (float) y;
    }

    // Lamps: peak-follow over the block
    ind.programPeak = (float) progPeak;
    ind.carrierPeak = (float) carPeak;
    ind.hiss = (float) hissLamp;
    ind.buzz = (float) buzzLamp;
    for (int ch = 0; ch < kNumChannels; ++ch)
        ind.analyzer[(size_t) ch] = (float) bands[(size_t) ch].cv;
}
} // namespace vocoder
