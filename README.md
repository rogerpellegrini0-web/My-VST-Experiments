# Filament: waveguide physical modeling synth

Filament is a polyphonic VST3 instrument that builds its sounds from digital waveguides.
These are small simulations of vibrating strings and air columns, not samples or
oscillators. It's built with [JUCE](https://juce.com) and tested with Cubase on Windows in mind.

## Models

| Model | How it works | Main controls |
|---|---|---|
| **Plucked String** | Single delay loop with a loss filter and stiffness (dispersion) allpasses, excited by a shaped pick burst. A pick-position comb removes harmonics that have a node at the pluck point. | Hardness, Pluck Pos, Pick Noise, Decay, Damping, Stiffness |
| **Bowed String** | Two waveguide segments (bow→nut, bow→bridge) joined by a nonlinear stick-slip friction junction. | Bow Speed, Bow Pos, Bow Force, Attack |
| **Reed** | Clarinet-style closed/open bore (odd harmonics) driven by a pressure-dependent reed reflection table. | Aperture, Reed Stiffness, Breath, Breath Noise |
| **Flute** | Open bore coupled to an air-jet delay with a cubic jet nonlinearity. | Jet Reflect, Jet Length, Breath, Breath Noise |

All four models share these stages:
- an instrument body resonator: a bank of body modes, slightly detuned between left and right for stereo width
- vibrato with a delayed onset
- velocity response
- a soft-clipping output stage

MIDI: notes, velocity, pitch bend (range adjustable), mod wheel (adds vibrato) and the sustain pedal.

Tuning is computed rather than approximated. Each loop's length is the note's period minus
the phase delay of every filter inside the loop. The test suite checks that every model
stays within a few cents across the keyboard at 44.1, 48 and 96 kHz.

## Installing on Windows (Cubase)

1. Open this repository on GitHub and go to **Actions → Build**. Pick the latest
   successful run and download the **Filament-Windows-x64** artifact (a zip).
2. Unzip it. Copy the whole **`Filament.vst3`** folder to
   `C:\Program Files\Common Files\VST3\`
3. In Cubase, open **Studio → VST Plug-in Manager** and click *Rescan*, or just restart Cubase.
4. Add an instrument track and choose **Filament** (it's listed under *My VST Experiments*).

The zip also includes `Filament.exe`, a standalone version you can play without a DAW.
The **Filament-demo-audio** artifact contains short WAV demos of each model, rendered by the test suite.

## Building it yourself

You need CMake (3.22 or newer) and a C++17 compiler. On Windows, that means Visual Studio 2022 with the
"Desktop development with C++" workload. JUCE is downloaded automatically.

```bash
cmake -B build
cmake --build build --config Release --target Filament_VST3
```

To work on the DSP alone (fast, no JUCE):

```bash
cmake -B build -DFILAMENT_BUILD_PLUGIN=OFF
cmake --build build
ctest --test-dir build --output-on-failure   # writes demo_*.wav into build/
```

## Layout

```
dsp/      framework-independent DSP: delay lines, filters, voices, engine
plugin/   JUCE wrapper: parameters, MIDI handling, editor UI
tests/    offline renderer: tuning, stability, release, demo WAVs
```

## License note

JUCE 8 is dual-licensed (AGPLv3 or a commercial license, with a free tier for small
developers). Personal use and experimentation are fine. Check the JUCE licensing
terms before distributing or selling the plugin.
