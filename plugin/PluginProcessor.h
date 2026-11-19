#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "Engine.h"
#include "Presets.h"

class FilamentProcessor final : public juce::AudioProcessor,
                                private juce::AudioProcessorValueTreeState::Listener,
                                private juce::AsyncUpdater
{
public:
    FilamentProcessor();
    ~FilamentProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }   // sympathetic strings ring on

    // The factory presets are published as programs, so MIDI Program Change (which Cubase
    // turns into "select program N" for VST3 instruments) picks presets.
    int getNumPrograms() override { return (int) presets.size(); }
    int getCurrentProgram() override { return currentPreset.load(); }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static juce::StringArray exciterNames();
    static juce::StringArray resonatorNames();

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;   // on-screen keyboard

    // Factory presets (message thread only)
    const std::vector<filament::Preset>& getPresets() const noexcept { return presets; }
    void applyPreset (int index);
    int  getPresetIndex() const;
    bool isPresetModified() const noexcept { return presetModified.load(); }

private:
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void setParam (const char* id, float value);

    std::vector<filament::Preset> presets = filament::factoryPresets();
    std::atomic<bool> presetModified { false };
    std::atomic<int>  currentPreset { 0 };      // readable from any thread
    std::atomic<int>  pendingProgram { -1 };    // program change waiting for the message thread
    void handleAsyncUpdate() override;
    bool applyingPreset = false;

    filament::Params readParams() const;
    void handleMidi (const juce::MidiMessage& m);
    static void migrateFromV1 (juce::XmlElement& xml);

    filament::Engine engine;
    juce::AudioBuffer<float> scratch;   // second channel when the host gives us mono out

    struct Raw
    {
        std::atomic<float>* exciter {};
        std::atomic<float>* resonator {};
        std::atomic<float>* tone {};
        std::atomic<float>* shape {};
        std::atomic<float>* position {};
        std::atomic<float>* pressure {};
        std::atomic<float>* noise {};
        std::atomic<float>* attack {};
        std::atomic<float>* bite {};
        std::atomic<float>* decay {};
        std::atomic<float>* damping {};
        std::atomic<float>* stiffness {};
        std::atomic<float>* release {};
        std::atomic<float>* vibRate {};
        std::atomic<float>* vibDepth {};
        std::atomic<float>* bendRange {};
        std::atomic<float>* velSens {};
        std::atomic<float>* voiceMode {};
        std::atomic<float>* velBite {};
        std::atomic<float>* glide {};
        std::atomic<float>* atBreath {};
        std::atomic<float>* wheelBreath {};
        std::atomic<float>* wheelVib {};
        std::atomic<float>* exprCurve {};
        std::atomic<float>* growl {};
        std::atomic<float>* atStrange {};
        std::atomic<float>* strange {};
        std::atomic<float>* buzz {};
        std::atomic<float>* symMix {};
        std::atomic<float>* symDecay {};
        std::atomic<float>* symKey {};
        std::atomic<float>* symTuning {};
        std::atomic<float>* bodyMix {};
        std::atomic<float>* bodySize {};
        std::atomic<float>* level {};
    } raw;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FilamentProcessor)
};
