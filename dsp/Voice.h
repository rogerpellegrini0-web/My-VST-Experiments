// Filament - waveguide physical modeling synth
#pragma once

#include "DspUtils.h"
#include "Params.h"

#include <array>
#include <cstdint>

namespace filament
{

// One polyphonic voice. Each voice owns the waveguide state for whichever model it
// was started with; the model is latched at note-on.
class Voice
{
public:
    void prepare (double sampleRate, uint32_t noiseSeed);

    void start (int note, float velocity, const Params& p);
    void retrigger (float velocity, const Params& p);   // same note pressed again
    void release();                                     // note-off
    void steal (int note, float velocity);              // fade out quickly, then start note
    void reset();

    // Adds this voice's output to `out`.
    void render (float* out, int numSamples, const Params& p, float bendSemis, float modWheel);

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

    void updateControl (const Params& p, float bendSemis, float modWheel, int chunk);
    void buildPluckExcitation (float velocity, const Params& p);
    float velocityToAmp (float velocity, const Params& p) const noexcept;
    float envTick() noexcept;

    void renderPluck (float* out, int n);
    void renderBow   (float* out, int n);
    void renderReed  (float* out, int n);
    void renderFlute (float* out, int n);

    double fs = 48000.0;
    Model  model = Model::PluckedString;
    int    note = 60;
    float  velocity = 0.8f, amp = 0.8f;
    bool   active = false, gate = false, firstControl = true;
    uint64_t startStamp = 0;
    int64_t  samplesSinceStart = 0;

    // voice stealing
    bool  stealing = false;
    int   pendingNote = 60;
    float pendingVelocity = 0.0f;
    float stealGain = 1.0f, stealStep = 0.0f;

    // waveguide state
    DelayLine dlA, dlB;
    OnePoleLP loopLP;
    std::array<Allpass1, kNumAllpass> disp;
    DCBlocker dc, outDc;
    Noise     noise;

    // smoothly-interpolated control values (per sample)
    float dA = 100.0f, dAStep = 0.0f;
    float dB = 100.0f, dBStep = 0.0f;
    float loopGain = 0.99f, loopGainStep = 0.0f;

    // exciter state
    std::vector<float> excitation, scratch;   // preallocated: no allocation on the audio thread
    int   exLen = 0, exPos = 0;
    float env = 0.0f, attackInc = 0.001f, releaseCoef = 0.999f;
    float maxDrive = 0.0f;       // bow velocity / breath pressure target
    float noiseGain = 0.0f;
    float shapeA = 0.0f;         // model-specific: bow slope | reed slope | jet reflection
    float shapeB = 0.0f;         // model-specific: reed offset
    float outGain = 1.0f;

    // modulation
    float vibPhase = 0.0f;

    // envelope follower for voice-end detection / stealing
    float level = 0.0f;
};

} // namespace filament
