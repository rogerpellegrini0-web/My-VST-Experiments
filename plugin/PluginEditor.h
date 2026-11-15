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
        std::vector<std::pair<juce::Label*, juce::Component*>> extras;   // non-knob controls, first cells
        int cells() const { return (int) (knobs.size() + extras.size()); }
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
    void refreshPresetDisplay (bool force);
    juce::ComboBox presetBox;
    juce::TextButton prevPreset { juce::String::fromUTF8 ("\xe2\x97\x80") }, nextPreset { juce::String::fromUTF8 ("\xe2\x96\xb6") };
    juce::Label presetLabel { {}, "PRESET" };
    int shownPreset = -1; bool shownModified = false;

    juce::ComboBox symKeyBox, symTuningBox;
    juce::Label symKeyLabel { {}, "Key" }, symTuningLabel { {}, "Tuning" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> symKeyAttachment, symTuningAttachment;

    juce::ComboBox modeBox;
    juce::Label modeLabel { {}, "Mode" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment;
    juce::MidiKeyboardComponent keyboard;

    int currentExciter = -1, currentResonator = -1, currentMode = -1;
    juce::String modelDescription;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FilamentEditor)
};
