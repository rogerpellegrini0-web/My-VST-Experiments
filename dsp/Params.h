// Filament - waveguide physical modeling synth
#pragma once

namespace filament
{

enum class Model : int
{
    PluckedString = 0,
    BowedString   = 1,
    Reed          = 2,
    Flute         = 3
};

constexpr int kNumModels = 4;

// All values are "knob" values; the models map them to physical quantities.
// Most 0..1 knobs are mapped so that 0.5 is the classic / textbook setting.
struct Params
{
    Model model       = Model::PluckedString;

    // Exciter
    float tone        = 0.5f;   // pluck hardness | bow speed | reed aperture | jet reflection
    float position    = 0.5f;   // pluck position | bow position | reed stiffness | jet length
    float pressure    = 0.5f;   // (unused)      | bow pressure | breath pressure | breath pressure
    float noise       = 0.2f;   // pluck noise   | bow noise | breath noise | breath noise
    float attack      = 0.05f;  // seconds, continuous exciters only

    // Resonator
    float decay       = 4.0f;   // seconds, T60 of the fundamental (strings) / loop loss (winds)
    float damping     = 0.5f;   // loop lowpass (high-frequency loss)
    float stiffness   = 0.1f;   // dispersion / inharmonicity (strings)
    float release     = 0.3f;   // seconds

    // Modulation
    float vibratoRate  = 5.0f;  // Hz
    float vibratoDepth = 0.15f; // 0..1
    float bendRange    = 2.0f;  // semitones
    float velocitySens = 0.7f;  // 0..1

    // Body / output
    float bodyMix     = 0.4f;   // 0..1
    float bodySize    = 0.5f;   // 0..1
    float levelDb     = -6.0f;
};

} // namespace filament
