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
| **Bow** | Stick-slip friction junction | Bow Speed, Rosin, Bow Force, Attack, Bite |
| **Reed** | Pressure-controlled reed valve (reflection table) | Aperture, Reed Stiffness, Breath, Tongue |
| **Lips** | Outward-striking lip valve: a damped mass-spring pushed open by mouth pressure, with the flow solved exactly every sample | Lip Tension, Lip Opening, Breath, Tongue |
| **Air Jet** | Jet delay with a cubic edge nonlinearity | Jet Reflect, Jet Length, Breath, Chiff |

**Bite / Tongue / Chiff** shapes how notes start. It adds a short burst of extra breath or
bow force and a small pulse that kicks the tube or string straight into its mode, as a
player's tongue or a firm bow stroke does. On bowed strings the extra force produces the
scratchy multiple-slip "bite" of a strong stroke, with a prominent 3rd harmonic (an octave
and a fifth up). On brass it gives the characteristic attack blip.

The **Horn** gets brighter the harder it is blown, like real brass: mellow at low breath
pressure, brassy and blazing at high pressure (velocity, aftertouch and bite all count).

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

## Presets

The preset menu at the top of the window (with ◀ ▶ to step through) has 37 factory sounds.
A `*` after the name means you've changed something since loading it. Your own
sounds are saved with the Cubase project, and you can store them with Cubase's own preset
save (the disk icon in the plugin window), which writes a `.vstpreset` file.

| Category | Presets |
|---|---|
| **Realistic** | Init, Nylon Guitar, Steel Guitar, Concert Harp, Hammered Dulcimer, Cello, Violin, Clarinet, Alto Sax, Oboe, Trumpet, French Horn, Flute, Pan Pipe, Sitar, Tanpura, Sarangi, Santoor, Dizi |
| **Hybrid** | Bowed Cone, Reed Fiddle, Plucked Horn, Brass Strings, Air Clarinet, Kazoo Reed |
| **Strange** | Bell Garden, Gong Bloom, Underwater Plates, Glass Bow, Bowed Metal, E-Bow Drone, Brass Scream, Sax Growler, Broken Bugle, Chaos Choir, Jawari Halo, Buzz Reed Drone |

The Sitar, Sarangi and Santoor use sympathetic strings tuned to C. Set *Key* to the key
of your music. The Sitar plays in Legato mode, so overlapping notes slide (meend).

The wind, brass and bowed presets are set up for a keyboard with aftertouch: they sound
full without pressing, and key pressure swells them further (*AT → Breath* about 30–35 %). Wind and brass
presets use Legato mode, so overlapping notes slur. *Brass Scream* and *Sax Growler* also
push into the Strange knob as you press harder.

Every preset is checked by the test suite: stable, releases cleanly, sensible level with
and without aftertouch, and the realistic and hybrid ones in tune.

### Selecting presets with MIDI Program Change

The presets are also published as programs, so a Program Change from your keyboard (or a
program-change event in a Cubase part) selects them. Program 1 is Init, 2 is Nylon Guitar,
and so on. This uses the 1–128 numbering most keyboards display. If yours counts from 0,
subtract one. Program numbers beyond the list are ignored. In Cubase the presets also
appear in the instrument track's *Programs* field in the Inspector.

Re-sending the program that's already loaded does nothing, so Cubase re-sending the saved
program when a project opens never wipes your edits.

| # | Preset | # | Preset | # | Preset |
|---|---|---|---|---|---|
| 1 | Init | 14 | Pan Pipe | 27 | Gong Bloom |
| 2 | Nylon Guitar | 15 | Sitar | 28 | Underwater Plates |
| 3 | Steel Guitar | 16 | Tanpura | 29 | Glass Bow |
| 4 | Concert Harp | 17 | Sarangi | 30 | Bowed Metal |
| 5 | Hammered Dulcimer | 18 | Santoor | 31 | E-Bow Drone |
| 6 | Cello | 19 | Dizi | 32 | Brass Scream |
| 7 | Violin | 20 | Bowed Cone | 33 | Sax Growler |
| 8 | Clarinet | 21 | Reed Fiddle | 34 | Broken Bugle |
| 9 | Alto Sax | 22 | Plucked Horn | 35 | Chaos Choir |
| 10 | Oboe | 23 | Brass Strings | 36 | Jawari Halo |
| 11 | Trumpet | 24 | Air Clarinet | 37 | Buzz Reed Drone |
| 12 | French Horn | 25 | Kazoo Reed | | |
| 13 | Flute | 26 | Bell Garden | | |

## Playing it from a keyboard

- **Mode:** *Poly* (16 voices), *Mono* (one voice, every note re-articulates) or
  *Legato* (one voice; overlapping notes slur without a new attack, so the string
  or air column keeps vibrating and only its length changes). In Mono and Legato the
  last note pressed sounds, and releasing it falls back to a key still held.
  A legato *pluck* is a hammer-on / pull-off.
- **Glide:** portamento time in Mono and Legato.
- **Vel → Bite:** how much velocity controls Bite and the attack time. At 0 % Bite is
  the same for every note. At 100 % the Bite knob is the maximum, reached at full velocity:
  soft notes are breathed or bowed in gently, hard notes are tongued or bitten and start
  faster (the attack time shortens by up to half). Re-articulated notes in Mono mode get a
  fresh bite. Legato slurs get none, as on the real instrument.
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

- **AT → Strange:** how far key pressure pushes the Strange amount (see below), so
  digging into a note can make it break up or scream.

Driving a model harder doesn't just make it louder. It changes the physics: brass
gets brassier, reeds buzz, the bow digs in, and past a point reeds choke and
flutes overblow, as the real instruments do.

Other MIDI: pitch bend (range adjustable) and the sustain pedal.

## Buzz and sympathetic strings

**Buzz** (Resonator section) adds a buzzing bridge or membrane:

- On **plucked and bowed** sounds it models a sitar's *jawari* bridge. The string wraps
  onto a wide, curved bridge whenever it swings past it, which momentarily shortens it.
  That lossless length modulation keeps pumping energy into high partials, so the
  note blooms with buzz as it rings. The bridge sits at a fixed height, so loud notes
  buzz more than soft ones. The pitch is compensated and stays in tune.
- On **blown** sounds (reed, lips, air jet) it is a buzzing membrane that slaps against
  the air column, as on a Chinese dizi flute or a kazoo. It adds the rattle without
  disturbing the oscillation or the pitch.

**Sympathetic** strings are 12 undriven strings shared by all voices, like a sitar's taraf
strings or the open strings of a harp or a pedalled piano. Everything you play excites them,
and they ring on wherever their tuning matches the music, leaving a shimmering, stereo halo.

- **Key** and **Tuning** (chromatic, drone on tonic and fifth, major, minor, pentatonic)
  set what they're tuned to. Set the key to the key of the music.
- **Mix** sets how loud the halo is (0 % = off), and **Decay** how long it rings.

## The Strange knob

One knob that takes any model from realistic to inharmonic and chaotic. It does this
by bending the physics, not by adding an effect afterwards, so a strange sound still
responds to velocity, pressure, legato and pitch bend.

| Strange | What changes in the model | What you hear |
|---|---|---|
| 0 % | nothing | the realistic instrument |
| ~5–45 % | extra stiffness: the loop's dispersion pulls the partials apart | piano-like stretch, then bell / gong partials |
| ~30–75 % | lossless "partial scrambler": resonant allpasses tuned to bell/plate mode ratios (2.76, 5.40, 8.93 x the note), slowly wobbling | clangy, metallic, shimmering inharmonic tones |
| ~65–100 % | strings and reeds: saturation and extra gain in the loop (self-sustaining while held). Lips: over-blow up the horn's modes. All: pitch that rises with loudness (tension modulation) and an over-driven exciter | drones, buzz, growls, multiphonics, screams |

The fundamental stays in tune through the first stages, a level guard keeps the
loudness roughly steady as the knob moves, and every exciter/resonator pairing has
been tested at every 10 % step for stability.

The **Stiffness** knob uses the same dispersion: 0–10 % is clean, 30–50 % is
piano-like, and towards 100 % it becomes bell-like.

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
dsp/      framework-independent DSP: delay lines, filters, voices, engine, presets
plugin/   JUCE wrapper: parameters, MIDI handling, editor UI
tests/    offline renderer: tuning, stability, release, demo WAVs
```

## License note

JUCE 8 is dual-licensed (AGPLv3 or a commercial license, with a free tier for small
developers). Personal use and experimentation are fine. Check the JUCE licensing
terms before distributing or selling the plugin.
