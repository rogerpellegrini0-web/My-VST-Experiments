// Filament - waveguide physical modeling synth
// Factory presets. Each preset starts from the default Params and changes only what it
// needs, so every parameter (voice mode, expression, Strange...) is always fully defined.
#pragma once

#include "Params.h"

#include <vector>

namespace filament
{

struct Preset
{
    const char* category;   // "Realistic", "Hybrid", "Strange"
    const char* name;
    Params params;
};

inline std::vector<Preset> factoryPresets()
{
    std::vector<Preset> list;
    auto add = [&list] (const char* category, const char* name, auto&& edit)
    {
        Params p;
        edit (p);
        list.push_back ({ category, name, p });
    };

    using E = Exciter;
    using R = Resonator;
    using M = VoiceMode;

    // ------------------------------------------------------------------ Realistic
    add ("Realistic", "Init", [] (Params&) {});

    add ("Realistic", "Nylon Guitar", [] (Params& p) {
        p.exciter = E::Pluck; p.resonator = R::String;
        p.tone = 0.38f; p.noise = 0.12f; p.position = 0.55f;
        p.decay = 3.5f; p.damping = 0.58f; p.stiffness = 0.05f; p.release = 0.35f;
        p.vibratoDepth = 0.05f; p.bodyMix = 0.6f; p.bodySize = 0.55f; });

    add ("Realistic", "Steel Guitar", [] (Params& p) {
        p.exciter = E::Pluck; p.resonator = R::String;
        p.tone = 0.72f; p.noise = 0.18f; p.position = 0.4f;
        p.decay = 5.0f; p.damping = 0.35f; p.stiffness = 0.18f; p.release = 0.4f;
        p.vibratoDepth = 0.0f; p.bodyMix = 0.5f; p.bodySize = 0.42f; p.levelDb = -1.0f; });

    add ("Realistic", "Concert Harp", [] (Params& p) {
        p.exciter = E::Pluck; p.resonator = R::String;
        p.tone = 0.42f; p.noise = 0.08f; p.position = 0.7f;
        p.decay = 6.5f; p.damping = 0.45f; p.stiffness = 0.12f; p.release = 1.2f;
        p.vibratoDepth = 0.0f; p.bodyMix = 0.3f; p.bodySize = 0.75f; });

    add ("Realistic", "Hammered Dulcimer", [] (Params& p) {
        p.exciter = E::Pluck; p.resonator = R::String;
        p.tone = 0.85f; p.noise = 0.05f; p.position = 0.3f;
        p.decay = 7.0f; p.damping = 0.3f; p.stiffness = 0.25f; p.release = 1.5f;
        p.vibratoDepth = 0.0f; p.bodyMix = 0.35f; p.bodySize = 0.6f; p.levelDb = 3.0f; });

    add ("Realistic", "Cello", [] (Params& p) {
        p.exciter = E::Bow; p.resonator = R::String;
        p.tone = 0.5f; p.shape = 0.5f; p.pressure = 0.55f; p.noise = 0.15f; p.attack = 0.15f;
        p.position = 0.5f; p.decay = 4.0f; p.damping = 0.55f; p.stiffness = 0.08f; p.release = 0.5f;
        p.vibratoRate = 5.2f; p.vibratoDepth = 0.35f; p.atToBreath = 0.5f;
        p.bodyMix = 0.55f; p.bodySize = 0.78f; });

    add ("Realistic", "Violin", [] (Params& p) {
        p.exciter = E::Bow; p.resonator = R::String;
        p.tone = 0.55f; p.shape = 0.5f; p.pressure = 0.5f; p.noise = 0.18f; p.attack = 0.1f;
        p.position = 0.42f; p.decay = 3.5f; p.damping = 0.5f; p.stiffness = 0.06f; p.release = 0.4f;
        p.vibratoRate = 5.8f; p.vibratoDepth = 0.4f; p.atToBreath = 0.5f;
        p.bodyMix = 0.55f; p.bodySize = 0.32f; });

    add ("Realistic", "Clarinet", [] (Params& p) {
        p.exciter = E::Reed; p.resonator = R::CylindricalTube;
        p.tone = 0.5f; p.shape = 0.5f; p.pressure = 0.55f; p.noise = 0.12f; p.attack = 0.05f;
        p.damping = 0.45f; p.release = 0.15f;
        p.vibratoDepth = 0.08f; p.voiceMode = M::Legato; p.glide = 0.03f; p.atToBreath = 0.6f;
        p.bodyMix = 0.05f; });

    add ("Realistic", "Alto Sax", [] (Params& p) {
        p.exciter = E::Reed; p.resonator = R::ConicalTube;
        p.tone = 0.55f; p.shape = 0.45f; p.pressure = 0.6f; p.noise = 0.2f; p.attack = 0.04f;
        p.damping = 0.5f; p.release = 0.15f;
        p.vibratoRate = 5.0f; p.vibratoDepth = 0.25f; p.voiceMode = M::Legato; p.glide = 0.04f;
        p.atToBreath = 0.6f; p.growl = 0.08f; p.bodyMix = 0.05f; });

    add ("Realistic", "Oboe", [] (Params& p) {
        p.exciter = E::Reed; p.resonator = R::ConicalTube;
        p.tone = 0.42f; p.shape = 0.6f; p.pressure = 0.55f; p.noise = 0.1f; p.attack = 0.05f;
        p.position = 0.7f; p.damping = 0.3f; p.release = 0.12f;
        p.vibratoRate = 5.5f; p.vibratoDepth = 0.2f; p.voiceMode = M::Legato; p.glide = 0.02f;
        p.atToBreath = 0.6f; p.bodyMix = 0.0f; });

    add ("Realistic", "Trumpet", [] (Params& p) {
        p.exciter = E::Lips; p.resonator = R::Horn;
        p.tone = 0.55f; p.shape = 0.45f; p.pressure = 0.6f; p.noise = 0.1f; p.attack = 0.03f;
        p.damping = 0.4f; p.release = 0.12f;
        p.vibratoDepth = 0.12f; p.voiceMode = M::Legato; p.glide = 0.03f; p.atToBreath = 0.6f;
        p.bodyMix = 0.0f; });

    add ("Realistic", "French Horn", [] (Params& p) {
        p.exciter = E::Lips; p.resonator = R::Horn;
        p.tone = 0.4f; p.shape = 0.55f; p.pressure = 0.45f; p.noise = 0.06f; p.attack = 0.07f;
        p.damping = 0.75f; p.release = 0.25f;
        p.vibratoDepth = 0.08f; p.voiceMode = M::Legato; p.glide = 0.05f; p.atToBreath = 0.6f;
        p.bodyMix = 0.2f; p.bodySize = 0.85f; });

    add ("Realistic", "Flute", [] (Params& p) {
        p.exciter = E::Jet; p.resonator = R::OpenTube;
        p.tone = 0.5f; p.shape = 0.5f; p.pressure = 0.55f; p.noise = 0.3f; p.attack = 0.08f;
        p.damping = 0.5f; p.release = 0.15f;
        p.vibratoRate = 5.0f; p.vibratoDepth = 0.35f; p.voiceMode = M::Legato; p.glide = 0.03f;
        p.atToBreath = 0.6f; p.bodyMix = 0.0f; });

    add ("Realistic", "Pan Pipe", [] (Params& p) {
        p.exciter = E::Jet; p.resonator = R::OpenTube;
        p.tone = 0.42f; p.shape = 0.45f; p.pressure = 0.5f; p.noise = 0.55f; p.attack = 0.04f;
        p.damping = 0.65f; p.release = 0.25f;
        p.vibratoDepth = 0.0f; p.atToBreath = 0.4f; p.bodyMix = 0.0f; });

    // ------------------------------------------------------------------ Hybrid
    add ("Hybrid", "Bowed Cone", [] (Params& p) {
        p.exciter = E::Bow; p.resonator = R::ConicalTube;
        p.tone = 0.5f; p.pressure = 0.5f; p.noise = 0.2f; p.attack = 0.25f;
        p.damping = 0.4f; p.release = 0.6f; p.vibratoDepth = 0.25f; p.atToBreath = 0.5f;
        p.bodyMix = 0.3f; p.bodySize = 0.6f; });

    add ("Hybrid", "Reed Fiddle", [] (Params& p) {
        p.exciter = E::Reed; p.resonator = R::String;
        p.tone = 0.5f; p.shape = 0.45f; p.pressure = 0.55f; p.noise = 0.15f; p.attack = 0.06f;
        p.position = 0.4f; p.decay = 3.0f; p.damping = 0.55f; p.release = 0.3f;
        p.vibratoDepth = 0.3f; p.voiceMode = M::Legato; p.glide = 0.04f; p.atToBreath = 0.5f;
        p.bodyMix = 0.5f; p.bodySize = 0.45f; });

    add ("Hybrid", "Plucked Horn", [] (Params& p) {
        p.exciter = E::Pluck; p.resonator = R::Horn;
        p.tone = 0.6f; p.noise = 0.1f; p.position = 0.35f;
        p.decay = 4.0f; p.damping = 0.35f; p.stiffness = 0.15f; p.release = 0.8f;
        p.vibratoDepth = 0.0f; p.bodyMix = 0.3f; p.bodySize = 0.8f; });

    add ("Hybrid", "Brass Strings", [] (Params& p) {
        p.exciter = E::Lips; p.resonator = R::String;
        p.tone = 0.5f; p.shape = 0.5f; p.pressure = 0.5f; p.attack = 0.06f;
        p.decay = 3.0f; p.damping = 0.6f; p.release = 0.3f;
        p.vibratoDepth = 0.15f; p.voiceMode = M::Legato; p.glide = 0.04f; p.atToBreath = 0.6f;
        p.bodyMix = 0.45f; p.bodySize = 0.5f; });

    add ("Hybrid", "Air Clarinet", [] (Params& p) {
        p.exciter = E::Jet; p.resonator = R::CylindricalTube;
        p.tone = 0.5f; p.shape = 0.5f; p.pressure = 0.55f; p.noise = 0.35f; p.attack = 0.06f;
        p.damping = 0.5f; p.release = 0.2f; p.vibratoDepth = 0.25f;
        p.voiceMode = M::Legato; p.glide = 0.03f; p.atToBreath = 0.6f; p.bodyMix = 0.0f; });

    // ------------------------------------------------------------------ Strange
    add ("Strange", "Bell Garden", [] (Params& p) {
        p.exciter = E::Pluck; p.resonator = R::String;
        p.tone = 0.8f; p.noise = 0.05f; p.position = 0.35f;
        p.decay = 8.0f; p.damping = 0.3f; p.stiffness = 0.3f; p.release = 2.5f;
        p.vibratoDepth = 0.0f; p.bodyMix = 0.2f; p.bodySize = 0.7f; p.strange = 0.5f; p.levelDb = -2.0f; });

    add ("Strange", "Gong Bloom", [] (Params& p) {
        p.exciter = E::Pluck; p.resonator = R::Horn;
        p.tone = 0.55f; p.noise = 0.2f; p.position = 0.3f;
        p.decay = 12.0f; p.damping = 0.3f; p.stiffness = 0.4f; p.release = 2.5f;
        p.vibratoDepth = 0.0f; p.bodyMix = 0.4f; p.bodySize = 0.95f; p.strange = 0.6f; });

    add ("Strange", "Underwater Plates", [] (Params& p) {
        p.exciter = E::Pluck; p.resonator = R::ConicalTube;
        p.tone = 0.65f; p.noise = 0.1f;
        p.decay = 10.0f; p.damping = 0.35f; p.release = 3.0f;
        p.vibratoRate = 0.8f; p.vibratoDepth = 0.2f; p.bodyMix = 0.3f; p.bodySize = 0.8f; p.strange = 0.65f; p.levelDb = -2.0f; });

    add ("Strange", "Glass Bow", [] (Params& p) {
        p.exciter = E::Bow; p.resonator = R::ConicalTube;
        p.tone = 0.45f; p.pressure = 0.45f; p.noise = 0.1f; p.attack = 0.35f;
        p.damping = 0.3f; p.release = 1.0f; p.vibratoDepth = 0.15f; p.atToBreath = 0.5f;
        p.bodyMix = 0.25f; p.bodySize = 0.3f; p.strange = 0.45f; });

    add ("Strange", "Bowed Metal", [] (Params& p) {
        p.exciter = E::Bow; p.resonator = R::String;
        p.tone = 0.6f; p.pressure = 0.6f; p.noise = 0.2f; p.attack = 0.2f;
        p.decay = 6.0f; p.damping = 0.4f; p.stiffness = 0.6f; p.release = 1.5f;
        p.vibratoDepth = 0.1f; p.atToBreath = 0.5f; p.atToStrange = 0.4f;
        p.bodyMix = 0.3f; p.bodySize = 0.9f; p.strange = 0.55f; });

    add ("Strange", "E-Bow Drone", [] (Params& p) {
        p.exciter = E::Pluck; p.resonator = R::String;
        p.tone = 0.5f; p.noise = 0.1f; p.position = 0.45f;
        p.decay = 8.0f; p.damping = 0.45f; p.stiffness = 0.1f; p.release = 1.5f;
        p.vibratoDepth = 0.1f; p.bodyMix = 0.4f; p.bodySize = 0.6f; p.strange = 0.82f; });

    add ("Strange", "Brass Scream", [] (Params& p) {
        p.exciter = E::Lips; p.resonator = R::Horn;
        p.tone = 0.55f; p.shape = 0.5f; p.pressure = 0.6f; p.noise = 0.12f; p.attack = 0.04f;
        p.damping = 0.4f; p.release = 0.15f;
        p.vibratoDepth = 0.15f; p.voiceMode = M::Legato; p.glide = 0.05f;
        p.atToBreath = 0.6f; p.atToStrange = 0.9f; p.exprCurve = -0.3f; p.growl = 0.15f; p.bodyMix = 0.1f; });

    add ("Strange", "Sax Growler", [] (Params& p) {
        p.exciter = E::Reed; p.resonator = R::ConicalTube;
        p.tone = 0.55f; p.shape = 0.45f; p.pressure = 0.6f; p.noise = 0.25f; p.attack = 0.04f;
        p.damping = 0.5f; p.release = 0.15f;
        p.vibratoDepth = 0.2f; p.voiceMode = M::Legato; p.glide = 0.04f;
        p.atToBreath = 0.6f; p.atToStrange = 0.6f; p.growl = 0.45f; p.strange = 0.15f; p.bodyMix = 0.05f; });

    add ("Strange", "Broken Bugle", [] (Params& p) {
        p.exciter = E::Lips; p.resonator = R::Horn;
        p.tone = 0.6f; p.pressure = 0.55f; p.attack = 0.05f; p.damping = 0.5f; p.release = 0.2f;
        p.vibratoDepth = 0.1f; p.voiceMode = M::Mono; p.atToBreath = 0.5f;
        p.bodyMix = 0.15f; p.strange = 0.9f; });

    add ("Strange", "Chaos Choir", [] (Params& p) {
        p.exciter = E::Jet; p.resonator = R::CylindricalTube;
        p.tone = 0.5f; p.shape = 0.5f; p.pressure = 0.6f; p.noise = 0.4f; p.attack = 0.3f;
        p.damping = 0.45f; p.release = 0.8f; p.vibratoRate = 4.0f; p.vibratoDepth = 0.45f;
        p.atToBreath = 0.4f; p.bodyMix = 0.3f; p.bodySize = 0.8f; p.strange = 0.7f; });

    add ("Strange", "Buzz Reed Drone", [] (Params& p) {
        p.exciter = E::Reed; p.resonator = R::CylindricalTube;
        p.tone = 0.6f; p.shape = 0.5f; p.pressure = 0.6f; p.noise = 0.15f; p.attack = 0.15f;
        p.damping = 0.4f; p.release = 0.6f; p.vibratoDepth = 0.05f;
        p.atToBreath = 0.4f; p.atToStrange = 0.3f; p.bodyMix = 0.2f; p.strange = 0.85f; });

    return list;
}

} // namespace filament
