// Filament - waveguide physical modeling synth
#include "Engine.h"

namespace filament
{

namespace
{
    struct Mode { float freq, q, gain; };

    // Loosely based on measured guitar/violin body responses (air mode, main wood
    // modes, upper "bridge hill"). Scaled by the Body Size control.
    constexpr Mode kBodyModes[] = {
        {  105.0f, 12.0f, 1.00f },
        {  215.0f, 14.0f, 0.80f },
        {  290.0f, 10.0f, 0.60f },
        {  450.0f, 12.0f, 0.70f },
        {  680.0f, 10.0f, 0.50f },
        { 1050.0f,  9.0f, 0.45f },
        { 1600.0f,  8.0f, 0.35f },
        { 2500.0f,  6.0f, 0.30f },
        { 3800.0f,  5.0f, 0.20f },
    };
}

//==============================================================================
void BodyResonator::prepare (double sampleRate)
{
    fs = sampleRate;
    currentSize = -1.0f;
    setSize (0.5f);
    reset();
}

void BodyResonator::setSize (float size01)
{
    if (std::fabs (size01 - currentSize) < 1e-4f) return;
    currentSize = size01;
    const float scale = std::exp2 (lerpf (1.0f, -1.0f, clampf (size01, 0.0f, 1.0f)));  // bigger = lower
    for (int i = 0; i < kModes; ++i)
    {
        const auto& m = kBodyModes[i];
        const float detune = (i % 2 == 0) ? 1.03f : 0.97f;
        left[(size_t) i] .setup (fs, m.freq * scale, m.q);
        right[(size_t) i].setup (fs, m.freq * scale * detune, m.q);
        gains[(size_t) i] = m.gain;
    }
}

void BodyResonator::reset()
{
    for (auto& b : left)  b.reset();
    for (auto& b : right) b.reset();
}

void BodyResonator::process (const float* in, float* outL, float* outR, int n, float mix)
{
    constexpr float bodyGain = 2.2f;
    const float dry = 1.0f - 0.75f * mix;
    for (int i = 0; i < n; ++i)
    {
        float l = 0.0f, r = 0.0f;
        for (int k = 0; k < kModes; ++k)
        {
            l += gains[(size_t) k] * left[(size_t) k].process (in[i]);
            r += gains[(size_t) k] * right[(size_t) k].process (in[i]);
        }
        outL[i] = dry * in[i] + mix * bodyGain * l;
        outR[i] = dry * in[i] + mix * bodyGain * r;
    }
}

//==============================================================================
namespace
{
    // Semitones above the key (from C3) for each of the 12 strings.
    constexpr int kSymTunings[SympatheticStrings::kNumTunings][SympatheticStrings::kStrings] = {
        { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 },          // chromatic
        { 0, 7, 12, 19, 24, 31, 36, 0, 12, 24, 7, 19 },    // drone: tonic and fifth (doubles detuned)
        { 0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19 },      // major
        { 0, 2, 3, 5, 7, 8, 10, 12, 14, 15, 17, 19 },      // natural minor
        { 0, 2, 4, 7, 9, 12, 14, 16, 19, 21, 24, 26 },     // major pentatonic
    };
}

void SympatheticStrings::prepare (double sampleRate)
{
    fs = sampleRate;
    for (int i = 0; i < kStrings; ++i)
    {
        auto& s = strings[(size_t) i];
        s.delay.setMaxDelay ((int) (fs / 40.0) + 16);       // lowest string is C3 - a few cents
        // spread across the stereo field, alternating sides
        const float pan = (i % 2 == 0 ? -1.0f : 1.0f) * (0.25f + 0.6f * (float) (i / 2) / (kStrings / 2));
        s.panL = std::sqrt (0.5f * (1.0f - pan));
        s.panR = std::sqrt (0.5f * (1.0f + pan));
    }
    key = tuning = -1;
    reset();
}

void SympatheticStrings::reset()
{
    for (auto& s : strings) { s.delay.clear(); s.loss.reset(); }
    silent = true;
}

void SympatheticStrings::configure (int newKey, int newTuning, float decaySeconds)
{
    newKey = std::min (11, std::max (0, newKey));
    newTuning = std::min (kNumTunings - 1, std::max (0, newTuning));
    if (newKey == key && newTuning == tuning && std::fabs (decaySeconds - decay) < 1e-3f) return;
    key = newKey; tuning = newTuning; decay = decaySeconds;

    for (int i = 0; i < kStrings; ++i)
    {
        auto& s = strings[(size_t) i];
        const int  semis   = kSymTunings[tuning][i];
        const bool doubled = i >= 7 && tuning == 1;                   // drone doubles: slightly detuned
        const float midi   = 48.0f + (float) (key + semis) + (doubled ? 0.04f * (i % 2 == 0 ? 1.0f : -1.0f) : 0.0f);
        const float f      = 440.0f * std::exp2 ((midi - 69.0f) / 12.0f);
        const float w      = kTwoPi * f / (float) fs;
        s.loss.p = poleForRate (0.25f, fs);
        const float t60 = std::max (0.2f, decaySeconds) * std::sqrt (130.0f / f);   // higher strings ring shorter
        s.length = std::max (2.0f, (float) fs / f - OnePoleLP::phaseDelay (s.loss.p, w));
        s.gain   = std::min (0.9999f, std::pow (10.0f, -3.0f / (t60 * f))
                                      / (OnePoleLP::magnitude (s.loss.p, w) * DelayLine::interpolationMagnitude (s.length, w)));
    }
}

void SympatheticStrings::process (const float* in, float* outL, float* outR, int n, float mix)
{
    if (mix <= 0.0f)
    {
        if (! silent) reset();
        return;
    }
    silent = false;
    constexpr float coupling = 0.012f;             // how strongly the bridge drives the strings
    const float outScale = 0.7f * mix;
    for (int i = 0; i < n; ++i)
    {
        const float x = in[i] * coupling;
        float l = 0.0f, r = 0.0f;
        for (auto& s : strings)
        {
            const float y = s.gain * s.loss.process (s.delay.read (s.length));
            s.delay.push (y + x);
            l += y * s.panL;
            r += y * s.panR;
        }
        outL[i] = l * outScale;
        outR[i] = r * outScale;
    }
}

//==============================================================================
void Engine::prepare (double sampleRate, int maxBlockSize)
{
    fs = sampleRate;
    uint32_t seed = 12345u;
    for (auto& v : voices)
        v.prepare (fs, seed += 0x9E3779B9u);
    mono.assign ((size_t) std::max (1, maxBlockSize), 0.0f);
    symL.assign (mono.size(), 0.0f);
    symR.assign (mono.size(), 0.0f);
    sympathetic.prepare (fs);
    body.prepare (fs);
    dcL.setup (fs, 12.0f);
    dcR.setup (fs, 12.0f);
    reset();
}

void Engine::reset()
{
    for (auto& v : voices) v.reset();
    sustained.fill (false);
    sustainDown = false;
    numHeld = 0;
    monoReleasePending = false;
    polyPressure.fill (0.0f);
    sympathetic.reset();
    body.reset();
    dcL.reset(); dcR.reset();
    gain = std::pow (10.0f, params.levelDb / 20.0f);
}

void Engine::setParams (const Params& p) noexcept
{
    if (p.voiceMode != params.voiceMode)
    {
        // switching between poly and mono: let everything ring out and start clean
        for (auto& v : voices)
            if (v.isActive()) v.release();
        numHeld = 0;
        monoReleasePending = false;
        sustained.fill (false);
    }
    params = p;
}

float Engine::expressionFor (int note) const noexcept
{
    // Aftertouch-like sources: channel pressure, poly pressure of this key, breath CC.
    float src = std::max (channelPressure, breath);
    if (note >= 0 && note < 128) src = std::max (src, polyPressure[(size_t) note]);

    const float exponent = std::exp2 (-1.5f * clampf (params.exprCurve, -1.0f, 1.0f));
    auto curve = [exponent] (float x) { return std::pow (clampf (x, 0.0f, 1.0f), exponent); };

    // Each amount blends from "fixed full drive" (0) to "drive follows the controller" (1).
    const float a = clampf (params.atToBreath, 0.0f, 1.0f);
    const float w = clampf (params.wheelToBreath, 0.0f, 1.0f);
    // Full pressure may drive up to 20 % past the knob setting (louder, brighter).
    return (1.0f - a + a * 1.2f * curve (src)) * (1.0f - w + w * 1.2f * curve (modWheel));
}

float Engine::pressureFor (int note) const noexcept
{
    float src = std::max (channelPressure, breath);
    if (note >= 0 && note < 128) src = std::max (src, polyPressure[(size_t) note]);
    const float exponent = std::exp2 (-1.5f * clampf (params.exprCurve, -1.0f, 1.0f));
    return std::pow (clampf (src, 0.0f, 1.0f), exponent);
}

float Engine::strangeFor (int note) const noexcept
{
    return clampf (params.atToStrange, 0.0f, 1.0f) * pressureFor (note);
}

void Engine::monoRemove (int note)
{
    int j = 0;
    for (int i = 0; i < numHeld; ++i)
        if (held[(size_t) i] != note) held[(size_t) j++] = held[(size_t) i];
    numHeld = j;
}

void Engine::monoNoteOn (int note, float velocity)
{
    Voice& v = voices[0];
    const bool overlapping = numHeld > 0;
    monoRemove (note);
    if (numHeld == (int) held.size()) monoRemove (held[0]);
    held[(size_t) numHeld++] = note;
    monoReleasePending = false;

    if (! v.isActive() || v.isStealing())
        v.start (note, velocity, params);
    else if (params.voiceMode == VoiceMode::Legato && overlapping && ! v.isReleased())
        v.legatoTo (note);
    else
        v.retriggerTo (note, velocity, params);
    v.setStamp (++stamp);
}

void Engine::monoNoteOff (int note)
{
    const bool wasSounding = numHeld > 0 && held[(size_t) numHeld - 1] == note;
    monoRemove (note);
    if (! wasSounding) return;

    Voice& v = voices[0];
    if (numHeld > 0)
        v.legatoTo (held[(size_t) numHeld - 1]);   // fall back to the key still held
    else if (sustainDown)
        monoReleasePending = true;
    else
        v.release();
}

void Engine::noteOn (int note, float velocity)
{
    if (note < 0 || note > 127) return;
    if (velocity <= 0.0f) { noteOff (note); return; }
    sustained[(size_t) note] = false;
    polyPressure[(size_t) note] = 0.0f;

    if (params.voiceMode != VoiceMode::Poly) { monoNoteOn (note, velocity); return; }

    // Same note still sounding: re-excite that voice (keeps the string/bore state).
    for (auto& v : voices)
        if (v.isActive() && ! v.isStealing() && v.getNote() == note)
        {
            v.retrigger (velocity, params);
            v.setStamp (++stamp);
            return;
        }

    for (auto& v : voices)
        if (! v.isActive())
        {
            v.start (note, velocity, params);
            v.setStamp (++stamp);
            return;
        }

    // Steal: prefer the quietest released voice, otherwise the oldest one.
    Voice* victim = nullptr;
    for (auto& v : voices)
        if (! v.isStealing() && v.isReleased() && (victim == nullptr || v.getLevel() < victim->getLevel()))
            victim = &v;
    if (victim == nullptr)
        for (auto& v : voices)
            if (! v.isStealing() && (victim == nullptr || v.getAge() < victim->getAge()))
                victim = &v;
    if (victim == nullptr) victim = &voices[0];

    victim->steal (note, velocity);
    victim->setStamp (++stamp);
}

void Engine::noteOff (int note)
{
    if (note < 0 || note > 127) return;
    if (params.voiceMode != VoiceMode::Poly) { monoNoteOff (note); return; }
    if (sustainDown) { sustained[(size_t) note] = true; return; }
    for (auto& v : voices)
        if (v.isActive() && ! v.isStealing() && v.getNote() == note && ! v.isReleased())
            v.release();
}

void Engine::setSustain (bool down)
{
    sustainDown = down;
    if (! down && monoReleasePending)
    {
        monoReleasePending = false;
        if (numHeld == 0) voices[0].release();
    }
    if (! down)
        for (int n = 0; n < 128; ++n)
            if (sustained[(size_t) n]) { sustained[(size_t) n] = false; noteOff (n); }
}

void Engine::allNotesOff()
{
    sustainDown = false;
    numHeld = 0;
    monoReleasePending = false;
    sustained.fill (false);
    for (auto& v : voices)
        if (v.isActive()) v.release();
}

int Engine::activeVoiceCount() const noexcept
{
    int n = 0;
    for (auto& v : voices) n += v.isActive() ? 1 : 0;
    return n;
}

void Engine::render (float* left, float* right, int numSamples)
{
    if ((int) mono.size() < numSamples)   // only if the host lied about the block size
    {
        mono.resize ((size_t) numSamples); symL.resize ((size_t) numSamples); symR.resize ((size_t) numSamples);
    }
    std::fill (mono.begin(), mono.begin() + numSamples, 0.0f);

    VoiceMod mod;
    mod.bendSemis = bend * params.bendRange;
    mod.wheel     = modWheel;
    for (auto& v : voices)
        if (v.isActive())
        {
            mod.expression = expressionFor (v.getNote());
            mod.strangeAdd = strangeFor (v.getNote());
            v.render (mono.data(), numSamples, params, mod);
        }

    // Sympathetic strings: driven by everything played; their sound goes through the body
    // (mid) and is spread across the stereo field (side).
    const float symMix = clampf (params.symMix, 0.0f, 1.0f);
    sympathetic.configure (params.symKey, params.symTuning, params.symDecay);
    sympathetic.process (mono.data(), symL.data(), symR.data(), numSamples, symMix);
    if (symMix > 0.0f)
        for (int i = 0; i < numSamples; ++i)
            mono[(size_t) i] += 0.5f * (symL[(size_t) i] + symR[(size_t) i]);

    body.setSize (params.bodySize);
    body.process (mono.data(), left, right, numSamples, clampf (params.bodyMix, 0.0f, 1.0f));

    if (symMix > 0.0f)
        for (int i = 0; i < numSamples; ++i)
        {
            const float side = 0.5f * (symL[(size_t) i] - symR[(size_t) i]);
            left[i] += side;
            right[i] -= side;
        }

    const float targetGain = std::pow (10.0f, params.levelDb / 20.0f);
    const float gStep = (targetGain - gain) / (float) numSamples;
    for (int i = 0; i < numSamples; ++i)
    {
        gain += gStep;
        // soft clip keeps runaway resonances from ever exceeding full scale
        left[i]  = std::tanh (dcL.process (left[i])  * gain);
        right[i] = std::tanh (dcR.process (right[i]) * gain);
    }
}

} // namespace filament
