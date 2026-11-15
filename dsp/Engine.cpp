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
void Engine::prepare (double sampleRate, int maxBlockSize)
{
    fs = sampleRate;
    uint32_t seed = 12345u;
    for (auto& v : voices)
        v.prepare (fs, seed += 0x9E3779B9u);
    mono.assign ((size_t) std::max (1, maxBlockSize), 0.0f);
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
    body.reset();
    dcL.reset(); dcR.reset();
    gain = std::pow (10.0f, params.levelDb / 20.0f);
}

void Engine::noteOn (int note, float velocity)
{
    if (note < 0 || note > 127) return;
    if (velocity <= 0.0f) { noteOff (note); return; }
    sustained[(size_t) note] = false;

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
    if (sustainDown) { sustained[(size_t) note] = true; return; }
    for (auto& v : voices)
        if (v.isActive() && ! v.isStealing() && v.getNote() == note && ! v.isReleased())
            v.release();
}

void Engine::setSustain (bool down)
{
    sustainDown = down;
    if (! down)
        for (int n = 0; n < 128; ++n)
            if (sustained[(size_t) n]) { sustained[(size_t) n] = false; noteOff (n); }
}

void Engine::allNotesOff()
{
    sustainDown = false;
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
    if ((int) mono.size() < numSamples) mono.resize ((size_t) numSamples);   // only if host lied about block size
    std::fill (mono.begin(), mono.begin() + numSamples, 0.0f);

    const float bendSemis = bend * params.bendRange;
    for (auto& v : voices)
        if (v.isActive())
            v.render (mono.data(), numSamples, params, bendSemis, modWheel);

    body.setSize (params.bodySize);
    body.process (mono.data(), left, right, numSamples, clampf (params.bodyMix, 0.0f, 1.0f));

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
