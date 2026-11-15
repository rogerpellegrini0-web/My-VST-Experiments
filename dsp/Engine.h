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

// A bank of undriven strings shared by all voices (sitar taraf strings, the open strings
// of a harp or a piano with the pedal down). They are excited by everything played and
// ring on wherever their tuning matches a partial of the music.
class SympatheticStrings
{
public:
    static constexpr int kStrings = 12;
    static constexpr int kNumTunings = 5;

    void prepare (double sampleRate);
    void reset();
    void configure (int key, int tuning, float decaySeconds);
    // in: mono signal of all voices; adds the strings' stereo output (scaled by mix)
    void process (const float* in, float* outL, float* outR, int n, float mix);

private:
    struct String
    {
        DelayLine delay;
        OnePoleLP loss;
        float length = 100.0f, gain = 0.99f, panL = 0.7f, panR = 0.7f;
    };
    double fs = 48000.0;
    std::array<String, kStrings> strings;
    int   key = -1, tuning = -1;
    float decay = -1.0f;
    bool  silent = true;
};

// Polyphonic voice manager + output stage. Has no framework dependencies, so it can
// be driven by the plugin wrapper or by the offline test renderer.
class Engine
{
public:
    static constexpr int kMaxVoices = 16;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    void setParams (const Params& p) noexcept;

    void noteOn (int note, float velocity);
    void noteOff (int note);
    void allNotesOff();
    void setPitchBend (float normalised) noexcept { bend = clampf (normalised, -1.0f, 1.0f); }   // -1..1
    void setModWheel (float v) noexcept { modWheel = clampf (v, 0.0f, 1.0f); }
    void setSustain (bool down);
    void setChannelPressure (float v) noexcept { channelPressure = clampf (v, 0.0f, 1.0f); }   // 0..1
    void setPolyPressure (int note, float v) noexcept { if (note >= 0 && note < 128) polyPressure[(size_t) note] = clampf (v, 0.0f, 1.0f); }
    void setBreath (float v) noexcept { breath = clampf (v, 0.0f, 1.0f); }                      // CC2

    void render (float* left, float* right, int numSamples);

    int activeVoiceCount() const noexcept;

private:
    double fs = 48000.0;
    Params params;
    std::array<Voice, kMaxVoices> voices;
    std::array<bool, 128> sustained {};
    bool     sustainDown = false;
    float    bend = 0.0f, modWheel = 0.0f;
    float    channelPressure = 0.0f, breath = 0.0f;
    std::array<float, 128> polyPressure {};

    // mono / legato: held notes, most recent last
    void monoNoteOn (int note, float velocity);
    void monoNoteOff (int note);
    void monoRemove (int note);
    std::array<int, 32> held {};
    int  numHeld = 0;
    bool monoReleasePending = false;   // keys up, sustain pedal still down
    float expressionFor (int note) const noexcept;
    float pressureFor (int note) const noexcept;   // aftertouch-like sources, curved
    float strangeFor (int note) const noexcept;
    uint64_t stamp = 0;

    std::vector<float> mono, symL, symR;
    SympatheticStrings sympathetic;
    BodyResonator body;
    DCBlocker dcL, dcR;
    float gain = 0.5f;
};

} // namespace filament
