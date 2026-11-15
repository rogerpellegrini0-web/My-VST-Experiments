// Filament - waveguide physical modeling synth
#pragma once

#include "Voice.h"

#include <array>
#include <vector>

namespace filament
{

// Stereo body resonator: a bank of resonant band-passes (instrument body modes),
// slightly detuned between left and right for natural width.
class BodyResonator
{
public:
    void prepare (double sampleRate);
    void setSize (float size01);
    void reset();
    void process (const float* in, float* outL, float* outR, int n, float mix);

private:
    static constexpr int kModes = 9;
    double fs = 48000.0;
    float  currentSize = -1.0f;
    std::array<BandPass, kModes> left, right;
    std::array<float, kModes> gains {};
};

// Polyphonic voice manager + output stage. Has no framework dependencies, so it can
// be driven by the plugin wrapper or by the offline test renderer.
class Engine
{
public:
    static constexpr int kMaxVoices = 16;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    void setParams (const Params& p) noexcept { params = p; }

    void noteOn (int note, float velocity);
    void noteOff (int note);
    void allNotesOff();
    void setPitchBend (float normalised) noexcept { bend = clampf (normalised, -1.0f, 1.0f); }   // -1..1
    void setModWheel (float v) noexcept { modWheel = clampf (v, 0.0f, 1.0f); }
    void setSustain (bool down);

    void render (float* left, float* right, int numSamples);

    int activeVoiceCount() const noexcept;

private:
    double fs = 48000.0;
    Params params;
    std::array<Voice, kMaxVoices> voices;
    std::array<bool, 128> sustained {};
    bool     sustainDown = false;
    float    bend = 0.0f, modWheel = 0.0f;
    uint64_t stamp = 0;

    std::vector<float> mono;
    BodyResonator body;
    DCBlocker dcL, dcR;
    float gain = 0.5f;
};

} // namespace filament
