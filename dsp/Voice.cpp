// Filament - waveguide physical modeling synth
//
// Each voice is an Exciter (driver) coupled to a Resonator through a scattering
// junction, in the tradition of Karplus-Strong, Julius O. Smith III's digital
// waveguides, the STK (Cook & Scavone) and the driver/pipe split of 1990s
// "virtual acoustic" synthesizers. Every sample:
//
//   1. the resonator reports `x`, the wave arriving at the excitation point
//      (for two-segment resonators: the sum of the waves from both sides),
//   2. the exciter returns `d`, what it injects there,
//   3. the resonator sends x/d onward: two-segment resonators pass each side's wave
//      through to the other side plus d; single-segment tubes reflect x + d back in.
//
// With d = 0 every resonator is a passive, decaying resonance, so any exciter can be
// paired with any resonator.
//
// Tuning is exact to within a few cents: each loop's length is the period minus the
// phase delay (at the fundamental) of every filter inside the loop, plus small measured
// corrections for the nonlinear junctions.

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

    // Output level trim per [exciter][resonator], measured with the offline renderer so every
    // combination sits around -18 dBFS RMS at default settings.
    constexpr float kComboGain[kNumExciters][kNumResonators] = {
        //  String  CylTube  ConTube  OpenTube  Horn
        {   3.78f,  5.22f,   2.09f,   7.32f,    6.93f },   // Pluck
        {   0.82f,  1.59f,   0.77f,   0.80f,    8.00f },   // Bow
        {   0.51f,  0.58f,   0.35f,   0.79f,    0.70f },   // Reed
        {   0.40f,  1.23f,   0.38f,   0.83f,    4.28f },   // Lips
        {   0.26f,  0.24f,   0.16f,   0.25f,    0.25f },   // Jet
    };
}

void Voice::prepare (double sampleRate, uint32_t noiseSeed)
{
    fs = sampleRate;
    const int maxDelay = (int) (2.0 * fs / 20.0) + 64;   // horn loop is two periods of 20 Hz
    dlA.setMaxDelay (maxDelay);
    dlB.setMaxDelay (maxDelay);
    dlJet.setMaxDelay (maxDelay);
    excitation.assign ((size_t) (2 * maxDelay), 0.0f);
    scratch.assign (excitation.size(), 0.0f);
    loopDc.setup (fs, 5.0f);
    outDc.setup (fs, 10.0f);
    noise.seed (noiseSeed);
    reset();
}

void Voice::reset()
{
    active = gate = stealing = false;
    dlA.clear(); dlB.clear(); dlJet.clear();
    loopLP.reset();
    for (auto& ap : disp) ap.reset();
    loopDc.reset();
    outDc.reset();
    lipY1 = lipY2 = 0.0f;
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
    exciter    = p.exciter;
    resonator  = p.resonator;
    twoSegment = isTwoSegment (resonator);
    note     = newNote;
    pitch    = (float) newNote;
    velocity = vel;
    amp      = velocityToAmp (vel, p);
    active = gate = true;
    firstControl = true;
    expr = 1.0f;
    drive = driveStep = 0.0f;
    growl = growlStep = growlPhase = growlWalk = 0.0f;
    samplesSinceStart = 0;
    vibPhase = 0.0f;

    reflSign  = resonator == Resonator::CylindricalTube ? -1.0f : 1.0f;
    useLoopDc = resonator == Resonator::OpenTube || resonator == Resonator::Horn;

    // Sustained exciters respond to breath/bow rather than velocity alone, so velocity
    // also scales their output a little.
    const float velScale = exciter == Exciter::Pluck ? 1.0f : (0.4f + 0.6f * amp);
    outGain = kComboGain[(int) exciter][(int) resonator] * velScale;

    if (exciter == Exciter::Pluck)
        buildPluckExcitation (vel, p);
}

void Voice::retrigger (float vel, const Params& p)
{
    velocity = vel;
    amp  = velocityToAmp (vel, p);
    gate = true;
    if (exciter == Exciter::Pluck)
        buildPluckExcitation (vel, p);   // re-pluck on top of the still-ringing resonator
}

void Voice::retriggerTo (int newNote, float vel, const Params& p)
{
    note = newNote;          // pitch glides there (or jumps when Glide is 0)
    retrigger (vel, p);
}

void Voice::legatoTo (int newNote)
{
    // Slur: the resonator keeps vibrating and the exciter keeps going; only the length
    // changes. On a plucked string this is a hammer-on / pull-off.
    note = newNote;
    gate = true;
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
    const int   W     = std::max (2, (int) ((1.0f - hard) * (1.0f - hard) * (float) L * 0.5f) + 2);

    // On a two-segment resonator the pick position comes for free (the burst is injected at
    // the excitation point and travels both ways). Single-segment tubes are driven at their
    // end, so the position comb is applied to the burst instead.
    const int P   = twoSegment ? 0 : std::max (1, (int) std::lround (knob3 (p.position, 0.03f, 0.13f, 0.5f) * (float) L));
    const int len = std::min ((int) excitation.size() - 1, std::max (L, W) + P);

    float* raw = scratch.data();
    for (int n = 0; n < len; ++n)
    {
        float pulse = 0.0f;
        if (n < W) pulse = 0.5f - 0.5f * std::cos (kTwoPi * (float) n / (float) W);
        float nzs = 0.0f;
        if (n < L) nzs = noise.next() * std::sin (kPi * (float) n / (float) L);
        raw[n] = (1.0f - nz) * pulse + nz * nzs;
    }

    OnePoleLP lp;
    lp.p = poleForRate ((1.0f - hard) * 0.9f, fs);
    float peak = 1e-9f;
    for (int n = 0; n < len; ++n)
    {
        const float combed = P > 0 ? raw[n] - (n >= P ? raw[n - P] : 0.0f) : raw[n];
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
void Voice::setupExciter (const Params& p, float f, float period)
{
    switch (exciter)
    {
        case Exciter::Pluck:
            break;

        case Exciter::Bow:
            shapeA    = knob3 (p.pressure, 5.0f, 3.0f, 1.2f);   // friction curve slope (force)
            shapeB    = knob3 (p.shape, 0.9f, 0.75f, 0.6f);     // stick threshold (rosin)
            maxDrive  = (0.03f + 0.2f * amp) * knob3 (p.tone, 0.4f, 1.0f, 1.8f);
            noiseGain = p.noise * 0.3f;
            break;

        case Exciter::Reed:
            // reed stiffness; the conical bore overblows to the octave with a stiff reed, so
            // its range is shifted softer
            shapeA    = resonator == Resonator::ConicalTube ? knob3 (p.shape, -0.15f, -0.24f, -0.4f)
                                                            : knob3 (p.shape, -0.18f, -0.3f, -0.45f);
            shapeB    = knob3 (p.tone, 0.6f, 0.7f, 0.8f);       // reed aperture
            maxDrive  = knob3 (p.pressure, 0.35f, 0.6f, 0.95f) * (0.85f + 0.15f * amp);
            noiseGain = p.noise * 0.6f;
            break;

        case Exciter::Lips:
        {
            // Outward-striking lip valve: a damped mass-spring (resonant lowpass, Q ~ 5) pushed
            // open by the pressure difference across it, with a small rest opening. Its tuning
            // relative to the bore mode bends the pitch, as a brass player's lip tension does.
            const float fBore = f / boreScale;
            const float lipHz = clampf (fBore * std::exp2 (lipPitchOct), 20.0f, (float) fs * 0.3f);
            const float wl = kTwoPi * lipHz / (float) fs;
            const float r  = std::exp (-wl / (2.0f * 5.0f));
            lipA1 = -2.0f * r * std::cos (wl);
            lipA2 = r * r;
            lipB  = 1.0f + lipA1 + lipA2;
            lipRest   = knob3 (p.shape, 0.02f, 0.05f, 0.12f);
            maxDrive  = knob3 (p.pressure, 0.3f, 0.6f, 1.0f) * (0.4f + 0.6f * amp);
            noiseGain = p.noise * 0.3f;
            break;
        }

        case Exciter::Jet:
        {
            const float jr = knob3 (p.shape, 0.44f, 0.5f, 0.56f);   // jet length / period
            const float target = std::max (2.0f, period * jr);
            if (firstControl) { dJ = target; dJStep = 0.0f; }
            else              dJStep = (target - dJ) / (float) kControlBlock;
            shapeA    = knob3 (p.tone, 0.3f, 0.5f, 0.7f);           // jet reflection
            maxDrive  = knob3 (p.pressure, 0.8f, 1.1f, 1.5f) * (0.85f + 0.15f * amp);
            noiseGain = p.noise * 0.75f;
            break;
        }
    }
}

float Voice::setupResonator (const Params& p, float f, float w, float period)
{
    const bool passive = exciter == Exciter::Pluck;

    // --- loss filter
    float pole = 0.0f;
    switch (resonator)
    {
        case Resonator::String:          pole = passive ? knob3 (p.damping, 0.02f, 0.3f, 0.8f)
                                                        : knob3 (p.damping, 0.3f, 0.6f, 0.88f); break;
        case Resonator::CylindricalTube: pole = knob3 (p.damping, 0.05f, 0.3f, 0.75f); break;
        case Resonator::ConicalTube:     pole = knob3 (p.damping, 0.05f, 0.42f, 0.8f); break;
        case Resonator::OpenTube:        pole = knob3 (p.damping, 0.4f, 0.65f, 0.85f); break;
        case Resonator::Horn:            pole = knob3 (p.damping, 0.05f, 0.2f, 0.7f);  break;
    }
    pole = poleForRate (pole, fs);

    // --- loop gain
    float t60 = p.decay * clampf (std::sqrt (220.0f / f), 0.3f, 1.5f);
    if (! gate) t60 = std::min (t60, p.release);
    float g = loopGainForT60 (t60, f);

    if (passive)
    {
        // A struck/plucked resonator's loss filter may never attenuate the fundamental more
        // than the decay time allows, otherwise high notes die instantly: limit the pole so
        // |H(w0)| >= g. (Driven resonators need the extra loss to oscillate stably.)
        const float m = std::min (0.99999f, g / 0.9999f);
        const float A = 1.0f - m * m, Bh = 1.0f - m * m * std::cos (w);
        const float pMax = (Bh - std::sqrt (std::max (0.0f, Bh * Bh - A * A))) / A;
        pole = std::min (pole, pMax);
    }
    else if (resonator != Resonator::String)
    {
        // Driven tubes: decay sets how resonant the bore is, within the range where the
        // exciters still speak reliably.
        g = loopGainForT60 (p.decay, f);
        switch (resonator)
        {
            case Resonator::OpenTube: g = clampf (g, 0.9f, 1.0f);    break;
            // lips need a well-damped horn to lock onto one mode; other exciters need more gain
            case Resonator::Horn:     g = exciter == Exciter::Lips ? clampf (g, 0.75f, 0.9f)
                                                                   : clampf (g, 0.88f, 0.985f); break;
            default:                  g = clampf (g, 0.88f, 0.985f); break;
        }
    }
    loopLP.p = pole;

    // --- dispersion (stiffness)
    float a = -0.8f * clampf (p.stiffness, 0.0f, 1.0f);
    auto filterDelay = [&] (float coef)
    {
        return OnePoleLP::phaseDelay (pole, w) + (float) kNumAllpass * Allpass1::phaseDelay (coef, w);
    };
    float pd = filterDelay (a);
    while (pd > 0.3f * period && a < -0.001f) { a *= 0.7f; pd = filterDelay (a); }
    for (auto& ap : disp) ap.a = a;

    // --- loop lengths
    float targetA = 2.0f, targetB = 2.0f;
    switch (resonator)
    {
        case Resonator::String:
        case Resonator::ConicalTube:
        {
            // +0.25 samples compensates the slight sharpening of the stick-slip bow junction.
            const float total = period - pd + (exciter == Exciter::Bow ? 0.25f : 0.0f);
            // fraction of the length on the far (bridge / bell) side of the excitation point
            const float beta = resonator == Resonator::String ? knob3 (p.position, 0.03f, 0.13f, 0.5f)
                                                              : knob3 (p.position, 0.95f, 0.8f, 0.55f);
            targetA = std::max (2.0f, total * beta);
            targetB = std::max (2.0f, total - targetA);
            break;
        }
        // Closed at the driver, open at the far end: the inverting reflection makes the
        // period two trips around the loop.
        case Resonator::CylindricalTube: targetA = std::max (2.0f, 0.5f * period - pd); break;
        case Resonator::OpenTube:        loopDc.setup (fs, 15.0f);
                                         targetA = std::max (2.0f, period - pd);        break;
        // Twice as long: modes at every half-harmonic, so the lips can pick between them.
        // For lips the horn is two periods long, giving modes at every
        // half-harmonic so the lips can pick between them like a bugle. Other exciters can't
        // select a mode and would sound an octave low, so they get a one-period horn.
        case Resonator::Horn:
        {
            const bool modal = exciter == Exciter::Lips;
            // In the two-period horn the in-loop high-pass tracks the note, weakening the
            // "pedal" mode an octave below so low notes don't fall into it.
            loopDc.setup (fs, exciter == Exciter::Lips ? 0.35f * f : 5.0f);
            targetA = std::max (2.0f, (modal ? 2.0f : 1.0f) * period - pd);
            break;
        }
    }

    // Compensate the loss of the fractional-delay interpolators, then the loss filter.
    float interp = DelayLine::interpolationMagnitude (targetA, w);
    if (twoSegment) interp *= DelayLine::interpolationMagnitude (targetB, w);
    if (passive || resonator == Resonator::String)
        g = std::min (0.99995f, g / (OnePoleLP::magnitude (pole, w) * interp));

    if (firstControl)
    {
        dA = targetA; dB = targetB;
        dAStep = dBStep = 0.0f;
    }
    else
    {
        const float inv = 1.0f / (float) kControlBlock;
        dAStep = (targetA - dA) * inv;
        dBStep = (targetB - dB) * inv;
    }
    return g;
}

void Voice::updateControl (const Params& p, const VoiceMod& mod, int chunk)
{
    const float t = (float) ((double) samplesSinceStart / fs);

    // Glide (portamento) towards the target note: exponential, ~95% after `glide` seconds.
    if (pitch != (float) note)
    {
        const float tau = std::max (0.0005f, p.glide / 3.0f);
        pitch += ((float) note - pitch) * (1.0f - std::exp (-(float) chunk / (tau * (float) fs)));
        if (std::fabs ((float) note - pitch) < 0.001f) pitch = (float) note;
    }

    // Vibrato: fades in after the attack, mod wheel adds depth.
    const float onset = clampf ((t - 0.15f) / 0.45f, 0.0f, 1.0f);
    const float depth = p.vibratoDepth * p.vibratoDepth * 0.5f + mod.wheel * p.wheelToVibrato * 0.4f;   // semitones
    vibPhase += kTwoPi * p.vibratoRate * (float) chunk / (float) fs;
    if (vibPhase > kTwoPi) vibPhase -= kTwoPi;

    const float semis = pitch - 69.0f + mod.bendSemis + depth * onset * std::sin (vibPhase);
    const float f     = clampf (noteToHz (semis), 20.0f, (float) fs / 8.0f);
    const float w     = kTwoPi * f / (float) fs;
    float period      = (float) fs / f;

    // The air jet's pitch is set jointly by the bore and the jet loop, so it drifts with
    // jet length. Measured offline: ~ -7.8 cents per 0.01 of jet ratio and ~ +2.7 cents per
    // octave; compensated by scaling the whole instrument.
    boreScale = 1.0f;
    if (exciter == Exciter::Jet)
    {
        const float jr    = knob3 (p.shape, 0.44f, 0.5f, 0.56f);
        const float cents = -778.0f * (jr - 0.5064f) + 2.7f * std::log2 (f / 110.0f);
        boreScale = std::exp2 (cents / 1200.0f);
    }
    else if (exciter == Exciter::Lips)
    {
        // Lips sound above the bore mode they lock to, more so the tighter they are.
        // Measured offline (cents vs lip tuning in octaves), compensated by lengthening the bore.
        static constexpr float kOct[]   = { -0.5f, -0.4f, -0.3f, -0.2f, -0.1f, 0.0f };
        static constexpr float kCents[] = { 42.0f, 48.0f, 60.0f, 79.0f, 126.0f, 210.0f };
        lipPitchOct = knob3 (p.tone, -0.45f, -0.3f, 0.0f);
        int i = 0;
        while (i < 4 && lipPitchOct > kOct[i + 1]) ++i;
        const float tt = clampf ((lipPitchOct - kOct[i]) / (kOct[i + 1] - kOct[i]), 0.0f, 1.0f);
        // The table is for the horn; other resonators load the lips differently.
        static constexpr float kResonatorOffset[kNumResonators] = { -59.0f, 202.0f, -57.0f, -30.0f, 0.0f };
        const float cents = lerpf (kCents[i], kCents[i + 1], tt)
                          + 5.0f * clampf (std::log2 (440.0f / f) * 0.5f, 0.0f, 1.0f)
                          + kResonatorOffset[(int) resonator];
        boreScale = std::exp2 (cents / 1200.0f);
    }
    else if (exciter == Exciter::Reed && resonator == Resonator::ConicalTube)
    {
        // The reed sharpens the cone's top register (measured: ~ 8.5 cents x (f / 1 kHz)^2).
        boreScale = std::exp2 (8.5f * (f / 1000.0f) * (f / 1000.0f) / 1200.0f);
    }
    period *= boreScale;

    attackInc   = 1.0f / (float) (std::max (0.001f, p.attack) * fs);
    releaseCoef = (float) std::exp (-6.9 / (std::max (0.005f, p.release) * fs));

    setupExciter (p, f, period);
    const float targetGain = setupResonator (p, f, w, period);

    // Expression (aftertouch / breath / wheel) scales the drive of sustained exciters,
    // smoothed (~8 ms) so stepped MIDI values don't zipper.
    expr += (clampf (mod.expression, 0.0f, 1.5f) - expr) * (1.0f - std::exp (-(float) chunk / (0.008f * (float) fs)));
    const float targetDrive = maxDrive * expr;

    // Growl: an irregular ~25-40 Hz throat flutter on the drive.
    growlWalk = clampf (growlWalk + 0.08f * noise.next(), -1.0f, 1.0f);
    growlPhase += kTwoPi * 32.0f * (1.0f + 0.25f * growlWalk) * (float) chunk / (float) fs;
    if (growlPhase > kTwoPi) growlPhase -= kTwoPi;
    const float targetGrowl = p.growl * 0.6f * (std::sin (growlPhase) + 0.3f * noise.next());

    if (firstControl)
    {
        loopGain = targetGain;
        loopGainStep = 0.0f;
        drive = targetDrive; driveStep = 0.0f;
        growl = targetGrowl; growlStep = 0.0f;
        firstControl = false;
    }
    else
    {
        const float inv = 1.0f / (float) chunk;
        loopGainStep = (targetGain - loopGain) * inv;
        driveStep    = (targetDrive - drive) * inv;
        growlStep    = (targetGrowl - growl) * inv;
    }

    // per-sample steps were computed for a full control block
    if (chunk != kControlBlock)
    {
        const float k = (float) kControlBlock / (float) chunk;
        dAStep *= k; dBStep *= k; dJStep *= k;
    }
}

//==============================================================================
float Voice::excite (float x) noexcept
{
    switch (exciter)
    {
        case Exciter::Pluck:
            return exPos < exLen ? excitation[(size_t) exPos++] : 0.0f;

        case Exciter::Bow:
        {
            const float e      = envTick();
            const float bowVel = drive * (1.0f + growl) * e * (1.0f + noiseGain * noise.next());
            const float dv     = bowVel - x;
            // STK-style friction curve: sticks (reflection 1) at small relative velocity.
            const float tt = std::fabs (dv * shapeA) + shapeB;
            const float t2 = tt * tt;
            const float friction = std::min (1.0f, 1.0f / (t2 * t2));
            const float contact  = gate ? 1.0f : e;           // bow lifts off on release
            return dv * friction * contact;
        }

        case Exciter::Reed:
        {
            const float e = envTick();
            float breath  = drive * (1.0f + growl) * e;
            breath += breath * noiseGain * noise.next();
            const float pdiff = x - breath;
            const float reed  = clampf (shapeB + shapeA * pdiff, -1.0f, 1.0f);
            return pdiff * (reed - 1.0f);    // fully closed reed (1) = rigid wall: no injection
        }

        case Exciter::Lips:
        {
            const float e = envTick();
            float pm = drive * (1.0f + growl) * e;
            pm += pm * noiseGain * noise.next();
            // Solve the flow exactly for this sample: with opening h, pressure drop dp and
            // injected wave d = h*sqrt|dp|, dp = q - d where q = pm - (pressure from the bore).
            // That is a quadratic in s = sqrt|dp| (an explicit one-sample-late solution
            // creates a parasitic squeal near fs/10).
            const float h  = std::max (0.0f, lipRest + lipY1);
            const float q  = pm - (twoSegment ? 1.0f : 2.0f) * x;
            const float sq = 0.5f * (-h + std::sqrt (h * h + 4.0f * std::fabs (q)));
            const float sg = q >= 0.0f ? 1.0f : -1.0f;
            const float dp = sg * sq * sq;
            const float lip = lipB * dp - lipA1 * lipY1 - lipA2 * lipY2;
            lipY2 = lipY1; lipY1 = lip;
            return h * sg * sq;
        }

        case Exciter::Jet:
        {
            const float e = envTick();
            float breath  = drive * (1.0f + growl) * e;
            breath += breath * noiseGain * noise.next();
            dJ += dJStep;
            // a two-segment resonator passes the injection both ways: halve the coupling
            const float k = twoSegment ? 0.5f : 1.0f;
            const float jetOut = dlJet.read (dJ);
            dlJet.push (breath - shapeA * k * x);
            const float jet = clampf (jetOut * (jetOut * jetOut - 1.0f), -1.0f, 1.0f);
            const float contact = gate ? 1.0f : e;   // no breath, no jet
            return (jet - 0.5f * k * x) * contact * k;   // the edge reflects half the bore wave
        }
    }
    return 0.0f;
}

void Voice::renderSamples (float* out, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        dA += dAStep; dB += dBStep; loopGain += loopGainStep;
        drive += driveStep; growl += growlStep;

        // 1. wave arriving at the excitation point
        const float rawFar = dlA.read (dA);
        float y = loopLP.process (rawFar);
        for (auto& ap : disp) y = ap.process (y);

        float reflFar, reflNear = 0.0f, x;
        if (twoSegment)
        {
            reflFar  = -loopGain * y;        // bridge / bell end (lossy, inverting)
            reflNear = -dlB.read (dB);       // nut / mouthpiece end (rigid, inverting)
            x = reflFar + reflNear;
        }
        else
        {
            reflFar = reflSign * loopGain * y;
            if (useLoopDc) reflFar = loopDc.process (reflFar);
            x = reflFar;
        }

        // 2. exciter
        const float d = excite (x);

        // 3. scatter
        if (twoSegment)
        {
            dlB.push (reflFar + d);
            dlA.push (reflNear + d);
        }
        else
        {
            dlA.push (x + d);
        }

        switch (resonator)
        {
            case Resonator::String:          out[i] = reflFar;           break;   // force on bridge
            case Resonator::ConicalTube:     out[i] = x + d;             break;   // pressure at reed
            case Resonator::CylindricalTube:
            case Resonator::OpenTube:        out[i] = rawFar;            break;
            case Resonator::Horn:            out[i] = rawFar - reflFar;  break;   // transmitted through the bell
        }
    }
}

//==============================================================================
void Voice::render (float* out, int numSamples, const Params& p, const VoiceMod& mod)
{
    int done = 0;
    while (active && done < numSamples)
    {
        if (stealing && stealGain <= 0.0f)
            start (pendingNote, pendingVelocity, p);   // clears `stealing`

        const int chunk = std::min (kControlBlock, numSamples - done);
        updateControl (p, mod, chunk);

        float tmp[kControlBlock];
        renderSamples (tmp, chunk);

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

        const bool exciterDone = exciter == Exciter::Pluck ? exPos >= exLen : env < 1e-4f;
        if (! stealing && (exciter == Exciter::Pluck || ! gate) && exciterDone
            && level < 2e-5f && samplesSinceStart > (int64_t) (0.05 * fs))
            active = false;
    }
}

} // namespace filament
