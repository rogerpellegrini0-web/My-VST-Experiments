// Filament - waveguide physical modeling synth
#pragma once

namespace filament
{

// The "driver": what sets the resonator vibrating.
enum class Exciter : int
{
    Pluck = 0,   // one-shot shaped burst (pick / mallet / finger)
    Bow   = 1,   // stick-slip friction
    Reed  = 2,   // pressure-controlled reed valve
    Lips  = 3,   // mass-spring lip valve (brass)
    Jet   = 4    // air jet against an edge (flute)
};

// The "pipe/string": what resonates and sets the pitch.
enum class Resonator : int
{
    String          = 0,   // two segments, driven at a point along the string, stiffness
    CylindricalTube = 1,   // closed at the driver: odd harmonics (clarinet)
    ConicalTube     = 2,   // all harmonics, driven near the narrow end (sax / oboe)
    OpenTube        = 3,   // open at both ends: all harmonics (flute)
    Horn            = 4    // long flared tube, many close modes the lips select between (brass)
};

constexpr int kNumExciters   = 5;
constexpr int kNumResonators = 5;

// All values are "knob" values; the models map them to physical quantities.
// Most 0..1 knobs are mapped so that 0.5 is the classic / textbook setting.
struct Params
{
    Exciter   exciter   = Exciter::Pluck;
    Resonator resonator = Resonator::String;

    // Exciter
    float tone        = 0.5f;   // hardness | bow speed | reed aperture | lip tension | jet reflection
    float shape       = 0.5f;   //    -     | rosin     | reed stiffness | lip Q      | jet length
    float pressure    = 0.5f;   //    -     | bow force | breath         | breath     | breath
    float noise       = 0.2f;   // pick / bow / breath noise
    float attack      = 0.05f;  // seconds, continuous exciters only

    // Resonator
    float position    = 0.5f;   // excitation point along string / bore (pluck position for tubes)
    float decay       = 4.0f;   // seconds, T60 of the fundamental / loop loss for driven tubes
    float damping     = 0.5f;   // loop lowpass (high-frequency loss)
    float stiffness   = 0.1f;   // dispersion / inharmonicity
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

inline bool isTwoSegment (Resonator r) noexcept
{
    return r == Resonator::String || r == Resonator::ConicalTube;
}

} // namespace filament
