#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "VocoderEngine.h"
#include "PatchPresets.h"

// Two audio inputs, as on the hardware:
//   main input       = CARRIER (insert the plugin on your synth / instrument track)
//   side-chain input = PROGRAM (route the voice to the plugin's side-chain in Cubase)
// The "Inputs" setting swaps them if you prefer to insert it on the vocal track.
class SixteenProcessor final : public juce::AudioProcessor
{
public:
    SixteenProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true; }    // sustain pedal = sample/hold foot switch
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static juce::String patchId (int synthInput) { return "patch" + juce::String (synthInput + 1); }

    // Patch cords (message thread). source = analyzer 0..15 or -1.
    int  getPatch (int synthInput) const;
    void setPatch (int synthInput, int source);
    void applyPatchPreset (const vocoder::PatchPreset& preset);

    juce::AudioProcessorValueTreeState apvts;

    // Lamps, written by the audio thread
    struct Lamps
    {
        std::atomic<float> programPeak { 0 }, carrierPeak { 0 }, hiss { 0 }, buzz { 0 };
        std::atomic<bool>  footHold { false };
        std::atomic<bool>  sideChainConnected { false };
    } lamps;

private:
    vocoder::Params readParams() const;

    vocoder::Engine engine;
    std::vector<float> programBuf, carrierBuf, outBuf;
    bool sustainDown = false;

    struct Raw
    {
        std::atomic<float>* power {}; std::atomic<float>* mic {};
        std::atomic<float>* programLevel {}; std::atomic<float>* carrierLevel {};
        std::atomic<float>* sampleHold {}; std::atomic<float>* externalPatch {};
        std::atomic<float>* hfDirect {}; std::atomic<float>* balance {};
        std::atomic<float>* mode {}; std::atomic<float>* status {};
        std::atomic<float>* preFilter {}; std::atomic<float>* routing {};
        std::array<std::atomic<float>*, vocoder::kNumChannels> patch {};
    } raw;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SixteenProcessor)
};
