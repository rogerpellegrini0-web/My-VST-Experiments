#pragma once

#include "PluginProcessor.h"

class FilamentLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    FilamentLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    juce::Colour accent { 0xffe0a64b };
};

class FilamentEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit FilamentEditor (FilamentProcessor&);
    ~FilamentEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::String paramId;
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    struct Section
    {
        juce::String title;
        std::vector<Knob*> knobs;
        juce::Rectangle<int> bounds;
    };

    void timerCallback() override;
    void updateForModel();
    Knob& knob (const juce::String& id);

    FilamentProcessor& processor;
    FilamentLookAndFeel lnf;

    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<Section> sections;

    juce::ComboBox exciterBox, resonatorBox;
    juce::Label exciterLabel { {}, "EXCITER" }, resonatorLabel { {}, "RESONATOR" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> exciterAttachment, resonatorAttachment;
    juce::MidiKeyboardComponent keyboard;

    int currentExciter = -1, currentResonator = -1;
    juce::String modelDescription;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FilamentEditor)
};
