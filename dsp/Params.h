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

enum class VoiceMode : int
{
    Poly   = 0,
    Mono   = 1,   // one voice, every note re-articulates (glides if Glide > 0)
    Legato = 2    // one voice, overlapping notes slur without re-attacking
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
    float bite        = 0.5f;   // 0..1: onset transient (tonguing, bow bite, lip blip)

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

    // Playing
    VoiceMode voiceMode = VoiceMode::Poly;
    float glide        = 0.0f;  // seconds (mono / legato)

    // Expression (keyboard-friendly breath control)
    float atToBreath     = 0.0f; // 0..1: how much aftertouch / breath CC controls the drive
    float wheelToBreath  = 0.0f; // 0..1: same for the mod wheel
    float wheelToVibrato = 1.0f; // 0..1
    float exprCurve      = 0.0f; // -1..1: response curve (positive = more sensitive)
    float growl          = 0.0f; // 0..1: throat growl on sustained exciters
    float atToStrange    = 0.0f; // 0..1: how far aftertouch / breath CC pushes Strange

    // The strange knob: 0 = realistic ... 1 = inharmonic / chaotic
    float strange        = 0.0f;

    // Bridge buzz (sitar "jawari"; a kazoo-like membrane on tubes): 0 = off
    float buzz           = 0.0f;

    // Sympathetic strings (shared by all voices)
    float symMix         = 0.0f;   // 0..1, 0 = off
    float symDecay       = 6.0f;   // seconds
    int   symKey         = 0;      // 0 = C ... 11 = B
    int   symTuning      = 1;      // see SympatheticStrings::kTunings (0 chromatic, 1 drone, 2 major, 3 minor, 4 pentatonic)

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
