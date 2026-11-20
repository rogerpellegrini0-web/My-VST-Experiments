#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SixteenProcessor.h"

namespace sixteen
{
// A control bound to one plugin parameter (host automation and undo handled by
// juce::ParameterAttachment).
class ParamControl : public juce::Component
{
public:
    ParamControl (juce::RangedAudioParameter& p);
    float value() const noexcept { return current; }    // in the parameter's real range

protected:
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    float current = 0;
};

// Black knurled knob with a brushed metal cap and a pointer.
class Knob final : public ParamControl
{
public:
    using ParamControl::ParamControl;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    static float angleFor (float norm) { return juce::jmap (norm, -2.36f, 2.36f); }

private:
    float dragStartNorm = 0;
};

// Three-position rotary selector (MODE)
class ModeSelector final : public ParamControl
{
public:
    using ParamControl::ParamControl;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    static float angleFor (int pos) { return (float) (pos - 1) * 0.87f; }
};

// Two-position switches in the styles found on the panel
class Toggle final : public ParamControl
{
public:
    enum class Style { Paddle, RockerVertical, RockerHorizontal, Power };
    Toggle (juce::RangedAudioParameter& p, Style s) : ParamControl (p), style (s) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool isOn() const noexcept { return current > 0.5f; }

private:
    Style style;
};

// The whole front panel, drawn at 1200 x 455 and scaled by the editor.
class Panel final : public juce::Component, public juce::TooltipClient
{
public:
    static constexpr int kWidth = 1200, kHeight = 455;

    explicit Panel (SixteenProcessor&);
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override {}

    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

    void updateLamps();

    static juce::Point<float> jackPos (int row, int index);   // row 0 = analyzer outputs, 1 = synthesizer inputs

private:
    SixteenProcessor& proc;

    Knob programLevel, carrierLevel, balance;
    ModeSelector mode;
    Toggle mic, sampleHold, externalPatch, hfDirect, status, power;

    // lamp brightness 0..1
    float lProgOver = 0, lCarOver = 0, lHiss = 0, lBuzz = 0;

    struct JackHit { int row = -1, index = -1; bool valid() const { return row >= 0; } };
    JackHit hitJack (juce::Point<float>) const;
    JackHit hover;
    int dragSource = -1;           // analyzer output being patched
    int dragFromSynth = -1;        // synth input whose cord was picked up
    juce::Point<float> dragPos;
    bool dragging = false;
    juce::Rectangle<float> setupButton { 18, 392, 80, 34 };

    void showSetupMenu();
    void drawCable (juce::Graphics&, juce::Point<float> a, juce::Point<float> b, juce::Colour, bool plugA, bool plugB) const;
    static juce::Colour cableColour (int source);
};
} // namespace sixteen

class SixteenEditor final : public juce::AudioProcessorEditor, public juce::Timer
{
public:
    explicit SixteenEditor (SixteenProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    sixteen::Panel panel;
    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SixteenEditor)
};
