# Filament: waveguide physical modeling synth

Filament is a polyphonic VST3 instrument that builds its sounds from digital waveguides.
These are small simulations of vibrating strings and air columns, not samples or
oscillators. It's built with [JUCE](https://juce.com) and tested with Cubase on Windows in mind.

## How it works: exciter + resonator

Every sound is an **exciter** (what sets things vibrating) driving a **resonator**
(what vibrates and sets the pitch). The two are joined at a scattering junction,
the way the driver/pipe split works in 1990s "virtual acoustic" synthesizers.
Any exciter can drive any resonator, which gives 25 combinations.

| Exciter | How it works | Main controls |
|---|---|---|
| **Pluck** | Shaped pick/mallet burst (smooth pulse + noise, hardness lowpass) | Hardness, Pick Noise |
| **Bow** | Stick-slip friction junction | Bow Speed, Rosin, Bow Force, Attack |
| **Reed** | Pressure-controlled reed valve (reflection table) | Aperture, Reed Stiffness, Breath |
| **Lips** | Outward-striking lip valve: a damped mass-spring pushed open by mouth pressure, with the flow solved exactly every sample | Lip Tension, Lip Opening, Breath |
| **Air Jet** | Jet delay with a cubic edge nonlinearity | Jet Reflect, Jet Length, Breath |

| Resonator | How it works |
|---|---|
| **String** | Two waveguide segments either side of the excitation point; loss filter + stiffness (dispersion) allpasses |
| **Cylindrical Tube** | Closed at the driver, open at the far end: odd harmonics (clarinet-like) |
| **Conical Tube** | Driven near the narrow end: all harmonics (sax/oboe-like) |
| **Open Tube** | Open at both ends: all harmonics (flute-like) |
| **Horn** | Flared bore. With Lips it is two periods long, so its modes sit at every half-harmonic and lip tension chooses between them, bugle-style |

Classic pairings: Pluck/Bow + String, Reed + Cylindrical or Conical Tube, Lips + Horn,
Air Jet + Open Tube. The other pairings are there for experimenting (bowed cone,
plucked horn, reed-driven string...).

All pairings share these stages:
- an instrument body resonator: a bank of body modes, slightly detuned between left and right for stereo width
- vibrato with a delayed onset
- velocity response
- a soft-clipping output stage

## Playing it from a keyboard

- **Mode:** *Poly* (16 voices), *Mono* (one voice, every note re-articulates) or
  *Legato* (one voice; overlapping notes slur without a new attack, so the string
  or air column keeps vibrating and only its length changes). In Mono and Legato the
  last note pressed sounds, and releasing it falls back to a key still held.
  A legato *pluck* is a hammer-on / pull-off.
- **Glide:** portamento time in Mono and Legato.
- **AT → Breath:** how much aftertouch (channel or polyphonic) controls the drive of
  the sustained exciters: breath for reed, lips and jet, bow speed for the bow.
  At 0 % the drive is fixed. At 100 % you play the dynamics entirely with key
  pressure, starting from silence, as with a wind controller. Values in between
  let velocity start the note and pressure swell it. A breath controller (CC2)
  works the same way.
- **Wheel → Breath / Wheel → Vib:** the same for the mod wheel, plus how much the
  wheel adds vibrato.
- **Curve:** response of those controllers, from *Firm* (needs more pressure) to
  *Light* (responds to a light touch).
- **Growl:** an irregular 25–40 Hz throat flutter on the breath or bow, from a subtle
  edge to a full growl.

Driving a model harder doesn't just make it louder. It changes the physics: brass
gets brassier, reeds buzz, the bow digs in, and past a point reeds choke and
flutes overblow, as the real instruments do.

Other MIDI: pitch bend (range adjustable) and the sustain pedal.

Tuning is computed rather than approximated. Each loop's length is the note's period minus
the phase delay of every filter inside the loop. Where a nonlinear exciter pulls the pitch
(lips, air jet, bow), measured corrections are applied. The test suite checks that the
classic pairings stay within a few cents across the keyboard at 44.1, 48 and 96 kHz,
that all 25 pairings are stable, level-matched and release cleanly, and it renders demo WAVs.

Projects saved with version 0.1 (single "Model" menu) load with the matching exciter and resonator.

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
