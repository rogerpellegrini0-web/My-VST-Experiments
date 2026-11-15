// Filament - waveguide physical modeling synth
#pragma once

#include "DspUtils.h"
#include "Params.h"

#include <array>
#include <cstdint>

namespace filament
{

// Per-voice modulation computed by the engine each block.
struct VoiceMod
{
    float bendSemis  = 0.0f;
    float wheel      = 0.0f;   // 0..1
    float expression = 1.0f;   // drive multiplier from aftertouch / breath / wheel (0..1)
};

// One voice: an Exciter driving a Resonator through a scattering junction.
// Exciter and resonator are latched at note-on.
class Voice
{
public:
    void prepare (double sampleRate, uint32_t noiseSeed);

    void start (int note, float velocity, const Params& p);
    void retrigger (float velocity, const Params& p);   // same note pressed again
    void retriggerTo (int note, float velocity, const Params& p);  // mono: re-articulate at a new pitch
    void legatoTo (int note);                           // mono: slur to a new pitch, no re-attack
    void release();                                     // note-off
    void steal (int note, float velocity);              // fade out quickly, then start note
    void reset();

    // Adds this voice's output to `out`.
    void render (float* out, int numSamples, const Params& p, const VoiceMod& mod);

    bool     isActive()   const noexcept { return active; }
    bool     isReleased() const noexcept { return !gate; }
    bool     isStealing() const noexcept { return stealing; }
    int      getNote()    const noexcept { return stealing ? pendingNote : note; }
    float    getLevel()   const noexcept { return level; }
    uint64_t getAge()     const noexcept { return startStamp; }
    void     setStamp (uint64_t s) noexcept { startStamp = s; }

private:
    static constexpr int kControlBlock = 32;
    static constexpr int kNumAllpass   = 4;

    void updateControl (const Params& p, const VoiceMod& mod, int chunk);
    void setupExciter (const Params& p, float f, float period);
    float setupResonator (const Params& p, float f, float w, float period);   // returns loop gain
    void buildPluckExcitation (float velocity, const Params& p);
    float velocityToAmp (float velocity, const Params& p) const noexcept;
    float envTick() noexcept;
    float excite (float incoming) noexcept;
    void renderSamples (float* out, int n) noexcept;

    double    fs = 48000.0;
    Exciter   exciter   = Exciter::Pluck;
    Resonator resonator = Resonator::String;
    bool      twoSegment = true;
    int    note = 60;
    float  pitch = 60.0f;     // sounding pitch (MIDI note number), glides towards `note`
    bool   gliding = false;
    float  velocity = 0.8f, amp = 0.8f;
    bool   active = false, gate = false, firstControl = true;
    uint64_t startStamp = 0;
    int64_t  samplesSinceStart = 0;

    // voice stealing
    bool  stealing = false;
    int   pendingNote = 60;
    float pendingVelocity = 0.0f;
    float stealGain = 1.0f, stealStep = 0.0f;

    // resonator state
    DelayLine dlA;     // far segment (to bridge / bell) or the whole bore
    DelayLine dlB;     // near segment (to nut / mouthpiece), two-segment resonators only
    DelayLine dlJet;   // air-jet delay (Jet exciter)
    OnePoleLP loopLP;
    std::array<Allpass1, kNumAllpass> disp;
    DCBlocker loopDc, outDc;
    float reflSign = -1.0f;   // end reflection sign for single-segment tubes
    bool  useLoopDc = false;

    // smoothly-interpolated control values (per sample)
    float dA = 100.0f, dAStep = 0.0f;
    float dB = 100.0f, dBStep = 0.0f;
    float dJ = 10.0f,  dJStep = 0.0f;
    float loopGain = 0.99f, loopGainStep = 0.0f;

    // exciter state
    std::vector<float> excitation, scratch;   // preallocated: no allocation on the audio thread
    int   exLen = 0, exPos = 0;
    float env = 0.0f, attackInc = 0.001f, releaseCoef = 0.999f;
    float maxDrive  = 0.0f;   // bow velocity / breath pressure target
    float drive = 0.0f, driveStep = 0.0f;       // smoothed drive incl. expression
    float expr = 1.0f;                          // smoothed expression
    float growl = 0.0f, growlStep = 0.0f, growlPhase = 0.0f, growlWalk = 0.0f;
    float noiseGain = 0.0f;
    float shapeA = 0.0f;      // bow slope | reed slope | jet reflection
    float shapeB = 0.0f;      // bow stick threshold | reed offset
    Noise noise;

    // lip (mass-spring, 2-pole resonant lowpass), Lips exciter
    float lipB = 0.0f, lipA1 = 0.0f, lipA2 = 0.0f, lipRest = 0.05f;
    float lipY1 = 0.0f, lipY2 = 0.0f;
    float lipPitchOct = -0.3f;    // lip resonance relative to the sounding mode
    float boreScale = 1.0f;       // pitch compensation for exciters that pull the resonance

    float outGain = 1.0f;

    // modulation
    float vibPhase = 0.0f;

    // envelope follower for voice-end detection / stealing
    float level = 0.0f;
};

} // namespace filament
