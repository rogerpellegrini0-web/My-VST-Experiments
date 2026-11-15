// Filament - waveguide physical modeling synth
//
// The four models are digital waveguides in the tradition of Karplus-Strong,
// Julius O. Smith III and the STK (Cook & Scavone):
//
//  * Plucked string : single delay loop + loss filter + dispersion allpasses,
//                     excited by a shaped, pick-position-combed burst.
//  * Bowed string   : two delay lines (bow->nut, bow->bridge) joined at the bow by a
//                     nonlinear friction (stick-slip) junction.
//  * Reed           : clarinet-style bore (closed/open tube, odd harmonics) driven by a
//                     pressure-controlled reed reflection table.
//  * Flute          : bore + air-jet delay with a cubic jet nonlinearity.
//
// Tuning is exact to within a few cents: each loop's target length is the period
// minus the phase delay (at the fundamental) of every filter inside the loop.

#include "Voice.h"

namespace filament
{

namespace
{
    float noteToHz (float semisFromA4) noexcept { return 440.0f * std::exp2 (semisFromA4 / 12.0f); }

    // Per-period loop gain that gives a T60 of `t60` seconds at frequency f.
    float loopGainForT60 (float t60, float f) noexcept
    {
        return std::pow (10.0f, -3.0f / (std::max (0.01f, t60) * f));
    }
}

void Voice::prepare (double sampleRate, uint32_t noiseSeed)
{
    fs = sampleRate;
    const int maxDelay = (int) (fs / 20.0) + 64;      // lowest pitch: 20 Hz
    dlA.setMaxDelay (maxDelay);
    dlB.setMaxDelay (maxDelay);
    excitation.assign ((size_t) (2 * maxDelay), 0.0f);
    scratch.assign (excitation.size(), 0.0f);
    dc.setup (fs, 15.0f);
    outDc.setup (fs, 10.0f);
    noise.seed (noiseSeed);
    reset();
}

void Voice::reset()
{
    active = gate = stealing = false;
    dlA.clear(); dlB.clear();
    loopLP.reset();
    for (auto& ap : disp) ap.reset();
    dc.reset();
    outDc.reset();
    env = level = 0.0f;
    exLen = exPos = 0;
    stealGain = 1.0f;
}

float Voice::velocityToAmp (float vel, const Params& p) const noexcept
{
    const float s = clampf (p.velocitySens, 0.0f, 1.0f);
    return (1.0f - s) + s * std::pow (clampf (vel, 0.0f, 1.0f), 1.3f);
}

void Voice::start (int newNote, float vel, const Params& p)
{
    reset();
    model    = p.model;
    note     = newNote;
    velocity = vel;
    amp      = velocityToAmp (vel, p);
    active = gate = true;
    firstControl = true;
    samplesSinceStart = 0;
    vibPhase = 0.0f;

    switch (model)
    {
        // Level-matched so every model sits around -18 dBFS RMS at default settings.
        // Winds respond to breath pressure rather than velocity, so velocity scales output.
        case Model::PluckedString: outGain = 3.2f;  break;
        case Model::BowedString:   outGain = 0.65f; break;
        case Model::Reed:          outGain = 0.55f * (0.35f + 0.65f * amp); break;
        case Model::Flute:         outGain = 0.33f * (0.3f + 0.7f * amp);  break;
    }

    if (model == Model::PluckedString)
        buildPluckExcitation (vel, p);
}

void Voice::retrigger (float vel, const Params& p)
{
    velocity = vel;
    amp  = velocityToAmp (vel, p);
    gate = true;
    if (model == Model::PluckedString)
        buildPluckExcitation (vel, p);   // re-pluck on top of the still-ringing string
}

void Voice::release() { gate = false; }

void Voice::steal (int newNote, float vel)
{
    stealing        = true;
    pendingNote     = newNote;
    pendingVelocity = vel;
    stealGain       = 1.0f;
    stealStep       = 1.0f / (float) (0.003 * fs);   // 3 ms fade
}

//==============================================================================
void Voice::buildPluckExcitation (float vel, const Params& p)
{
    const float period = (float) fs / noteToHz ((float) note - 69.0f);
    const int   maxLen = (int) excitation.size() / 2 - 4;
    const int   L      = std::max (4, std::min (maxLen, (int) std::lround (period)));

    const float hard  = clampf (p.tone * (0.55f + 0.6f * vel), 0.0f, 1.0f);
    const float nz    = clampf (p.noise, 0.0f, 1.0f);
    const float pos   = knob3 (p.position, 0.03f, 0.13f, 0.5f);
    const int   P     = std::max (1, (int) std::lround (pos * (float) L));
    const int   W     = std::max (2, (int) ((1.0f - hard) * (1.0f - hard) * (float) L * 0.5f) + 2);
    const int   len   = std::min ((int) excitation.size() - 1, std::max (L, W) + P);

    // raw burst: smooth "finger/pick" pulse + windowed noise
    std::fill (excitation.begin(), excitation.begin() + len + 1, 0.0f);
    float* raw = scratch.data();
    for (int n = 0; n < len; ++n)
    {
        float pulse = 0.0f;
        if (n < W) pulse = 0.5f - 0.5f * std::cos (kTwoPi * (float) n / (float) W);
        float nzs = 0.0f;
        if (n < L) nzs = noise.next() * std::sin (kPi * (float) n / (float) L);
        raw[n] = (1.0f - nz) * pulse + nz * nzs;
    }

    // pick-position comb (removes harmonics with a node at the pluck point),
    // then a hardness-dependent lowpass.
    OnePoleLP lp;
    lp.p = poleForRate ((1.0f - hard) * 0.9f, fs);
    float peak = 1e-9f;
    for (int n = 0; n < len; ++n)
    {
        const float combed = raw[n] - (n >= P ? raw[n - P] : 0.0f);
        const float y = lp.process (combed);
        excitation[(size_t) n] = y;
        peak = std::max (peak, std::fabs (y));
    }

    const float scale = 0.5f * amp / peak;
    for (int n = 0; n < len; ++n)
        excitation[(size_t) n] *= scale;

    exLen = len;
    exPos = 0;
}

float Voice::envTick() noexcept
{
    if (gate) env = std::min (1.0f, env + attackInc);
    else      env *= releaseCoef;
    return env;
}

//==============================================================================
void Voice::updateControl (const Params& p, float bendSemis, float modWheel, int chunk)
{
    const float t = (float) ((double) samplesSinceStart / fs);

    // Vibrato: fades in after the attack, mod wheel adds depth.
    const float onset = clampf ((t - 0.15f) / 0.45f, 0.0f, 1.0f);
    const float depth = p.vibratoDepth * p.vibratoDepth * 0.5f + modWheel * 0.4f;   // semitones
    vibPhase += kTwoPi * p.vibratoRate * (float) chunk / (float) fs;
    if (vibPhase > kTwoPi) vibPhase -= kTwoPi;

    const float semis = (float) note - 69.0f + bendSemis + depth * onset * std::sin (vibPhase);
    const float f     = clampf (noteToHz (semis), 20.0f, (float) fs / 8.0f);
    const float w     = kTwoPi * f / (float) fs;
    const float period = (float) fs / f;

    attackInc   = 1.0f / (float) (std::max (0.001f, p.attack) * fs);
    releaseCoef = (float) std::exp (-6.9 / (std::max (0.005f, p.release) * fs));

    float targetA = 2.0f, targetB = 2.0f, targetGain = 0.99f;

    // Common string loop: one-pole loss filter + dispersion allpass chain.
    auto setupStringLoop = [&] (float pole, bool limitPole) -> float
    {
        float t60 = p.decay * clampf (std::sqrt (220.0f / f), 0.3f, 1.5f);
        if (! gate) t60 = std::min (t60, p.release);
        const float g = loopGainForT60 (t60, f);

        // A plucked string's loss filter may never attenuate the fundamental more than the
        // decay time allows, otherwise high notes die instantly: limit the pole so
        // |H(w0)| >= g. (The bowed string is continuously driven and actually needs the
        // extra high-frequency loss to stay in stable Helmholtz motion.)
        const float m = std::min (0.99999f, g / 0.9999f);
        const float A = 1.0f - m * m, Bh = 1.0f - m * m * std::cos (w);
        const float pMax = (Bh - std::sqrt (std::max (0.0f, Bh * Bh - A * A))) / A;
        if (limitPole) pole = std::min (pole, pMax);

        loopLP.p = pole;
        float a  = -0.8f * clampf (p.stiffness, 0.0f, 1.0f);
        auto filterDelay = [&] (float coef)
        {
            return OnePoleLP::phaseDelay (pole, w) + (float) kNumAllpass * Allpass1::phaseDelay (coef, w);
        };
        float pd = filterDelay (a);
        while (pd > 0.5f * period && a < -0.001f) { a *= 0.7f; pd = filterDelay (a); }
        for (auto& ap : disp) ap.a = a;

        targetGain = g / OnePoleLP::magnitude (pole, w);
        return std::max (2.0f, period - pd);
    };

    switch (model)
    {
        case Model::PluckedString:
        {
            targetA = setupStringLoop (poleForRate (knob3 (p.damping, 0.02f, 0.3f, 0.8f), fs), true);
            targetGain = std::min (0.99995f, targetGain / DelayLine::interpolationMagnitude (targetA, w));
            break;
        }

        case Model::BowedString:
        {
            const float total = setupStringLoop (poleForRate (knob3 (p.damping, 0.3f, 0.6f, 0.88f), fs), false);
            const float beta  = knob3 (p.position, 0.03f, 0.127f, 0.45f);
            // +0.25 samples compensates the slight sharpening caused by the stick-slip junction.
            const float totalC = total + 0.25f;
            targetB = std::max (2.0f, totalC * beta);           // bow -> bridge -> bow
            targetA = std::max (2.0f, totalC - targetB);       // bow -> nut -> bow
            targetGain = std::min (0.99995f, targetGain / (DelayLine::interpolationMagnitude (targetA, w)
                                                           * DelayLine::interpolationMagnitude (targetB, w)));
            shapeA    = knob3 (p.pressure, 5.0f, 3.0f, 1.2f);  // friction curve slope
            maxDrive  = (0.03f + 0.2f * amp) * knob3 (p.tone, 0.4f, 1.0f, 1.8f);
            noiseGain = p.noise * 0.3f;
            break;
        }

        case Model::Reed:
        {
            loopLP.p = poleForRate (knob3 (p.damping, 0.05f, 0.3f, 0.75f), fs);
            // Closed-open tube: the inverting reflection makes the period two loop trips.
            targetA    = std::max (2.0f, 0.5f * period - OnePoleLP::phaseDelay (loopLP.p, w));
            targetGain = clampf (loopGainForT60 (p.decay, f), 0.88f, 0.985f);
            shapeA     = knob3 (p.position, -0.18f, -0.3f, -0.45f);   // reed stiffness
            shapeB     = knob3 (p.tone, 0.6f, 0.7f, 0.8f);            // reed aperture
            maxDrive   = knob3 (p.pressure, 0.35f, 0.6f, 0.95f) * (0.85f + 0.15f * amp);
            noiseGain  = p.noise * 0.6f;
            break;
        }

        case Model::Flute:
        {
            loopLP.p = poleForRate (knob3 (p.damping, 0.4f, 0.65f, 0.85f), fs);
            // The flute's pitch is set jointly by the bore and the jet loop, so it drifts with
            // jet length. The jet range is limited to where the model reliably speaks on its
            // fundamental, and the drift (measured offline: ~ -7.8 cents per 0.01 of jet
            // ratio, ~ +2.7 cents per octave) is compensated by scaling the whole instrument.
            const float jr    = knob3 (p.position, 0.44f, 0.5f, 0.62f);
            const float cents = -778.0f * (jr - 0.5064f) + 2.7f * std::log2 (f / 110.0f);
            const float pc    = period * std::exp2 (cents / 1200.0f);
            targetA    = std::max (2.0f, pc * jr);
            targetB    = std::max (2.0f, pc - OnePoleLP::phaseDelay (loopLP.p, w));
            targetGain = clampf (loopGainForT60 (p.decay, f), 0.9f, 1.0f);
            shapeA     = knob3 (p.tone, 0.3f, 0.5f, 0.7f);            // jet reflection
            maxDrive   = knob3 (p.pressure, 0.8f, 1.1f, 1.5f) * (0.85f + 0.15f * amp);
            noiseGain  = p.noise * 0.75f;
            break;
        }
    }

    if (firstControl)
    {
        dA = targetA; dB = targetB; loopGain = targetGain;
        dAStep = dBStep = loopGainStep = 0.0f;
        firstControl = false;
    }
    else
    {
        const float inv = 1.0f / (float) chunk;
        dAStep       = (targetA - dA) * inv;
        dBStep       = (targetB - dB) * inv;
        loopGainStep = (targetGain - loopGain) * inv;
    }
}

//==============================================================================
void Voice::renderPluck (float* out, int n)
{
    for (int i = 0; i < n; ++i)
    {
        dA += dAStep; loopGain += loopGainStep;
        float y = loopLP.process (dlA.read (dA));
        for (auto& ap : disp) y = ap.process (y);
        y *= loopGain;
        const float ex = exPos < exLen ? excitation[(size_t) exPos++] : 0.0f;
        const float s  = y + ex;
        dlA.push (s);
        out[i] = s;
    }
}

void Voice::renderBow (float* out, int n)
{
    for (int i = 0; i < n; ++i)
    {
        dA += dAStep; dB += dBStep; loopGain += loopGainStep;

        float b = loopLP.process (dlB.read (dB));
        for (auto& ap : disp) b = ap.process (b);
        const float bridgeRefl = -loopGain * b;
        const float nutRefl    = -dlA.read (dA);
        const float stringVel  = bridgeRefl + nutRefl;

        const float e      = envTick();
        const float bowVel = maxDrive * e * (1.0f + noiseGain * noise.next());
        const float dv     = bowVel - stringVel;

        // STK-style friction curve: sticks (reflection 1) at small relative velocity.
        const float tt = std::fabs (dv * shapeA) + 0.75f;
        const float t2 = tt * tt;
        const float friction = std::min (1.0f, 1.0f / (t2 * t2));
        const float contact  = gate ? 1.0f : e;           // bow lifts off on release
        const float newVel   = dv * friction * contact;

        dlA.push (bridgeRefl + newVel);
        dlB.push (nutRefl + newVel);
        out[i] = bridgeRefl;
    }
}

void Voice::renderReed (float* out, int n)
{
    for (int i = 0; i < n; ++i)
    {
        dA += dAStep; loopGain += loopGainStep;

        const float bore = dlA.read (dA);
        const float e    = envTick();
        float breath     = maxDrive * e;
        breath += breath * noiseGain * noise.next();

        const float refl  = -loopGain * loopLP.process (bore);
        const float pdiff = refl - breath;
        const float reed  = clampf (shapeB + shapeA * pdiff, -1.0f, 1.0f);
        dlA.push (breath + pdiff * reed);
        out[i] = bore;
    }
}

void Voice::renderFlute (float* out, int n)
{
    for (int i = 0; i < n; ++i)
    {
        dA += dAStep; dB += dBStep; loopGain += loopGainStep;

        const float boreOut = dlB.read (dB);
        const float e = envTick();
        float breath  = maxDrive * e;
        breath += breath * noiseGain * noise.next();

        const float temp   = dc.process (loopGain * loopLP.process (boreOut));
        const float jetOut = dlA.read (dA);
        dlA.push (breath - shapeA * temp);

        const float jet = clampf (jetOut * (jetOut * jetOut - 1.0f), -1.0f, 1.0f);
        dlB.push (jet + 0.5f * temp);
        out[i] = boreOut;
    }
}

//==============================================================================
void Voice::render (float* out, int numSamples, const Params& p, float bendSemis, float modWheel)
{
    int done = 0;
    while (active && done < numSamples)
    {
        if (stealing && stealGain <= 0.0f)
            start (pendingNote, pendingVelocity, p);   // clears `stealing`

        const int chunk = std::min (kControlBlock, numSamples - done);
        updateControl (p, bendSemis, modWheel, chunk);

        float tmp[kControlBlock];
        switch (model)
        {
            case Model::PluckedString: renderPluck (tmp, chunk); break;
            case Model::BowedString:   renderBow   (tmp, chunk); break;
            case Model::Reed:          renderReed  (tmp, chunk); break;
            case Model::Flute:         renderFlute (tmp, chunk); break;
        }

        float peak = 0.0f;
        for (int i = 0; i < chunk; ++i)
        {
            float s = outDc.process (tmp[i]) * outGain;
            if (stealing)
            {
                s *= stealGain;
                stealGain = std::max (0.0f, stealGain - stealStep);
            }
            out[done + i] += s;
            peak = std::max (peak, std::fabs (s));
        }

        if (! std::isfinite (peak))   // safety net: never let a blown-up voice keep running
        {
            reset();
            return;
        }

        level = std::max (peak, level * (float) std::exp (-(double) chunk / (0.05 * fs)));
        samplesSinceStart += chunk;
        done += chunk;

        const bool exciterDone = model == Model::PluckedString ? exPos >= exLen : env < 1e-4f;
        if (! stealing && (model == Model::PluckedString || ! gate) && exciterDone
            && level < 2e-5f && samplesSinceStart > (int64_t) (0.05 * fs))
            active = false;
    }
}

} // namespace filament
