#include "PluginEditor.h"

namespace
{
    const juce::Colour kBackground { 0xff15171c };
    const juce::Colour kPanel      { 0xff1e2129 };
    const juce::Colour kText       { 0xffd9dde6 };
    const juce::Colour kDim        { 0xff7d8494 };

    const juce::Colour kExciterAccent[] = {
        juce::Colour (0xffe08a4b),   // pluck: orange
        juce::Colour (0xffd8644f),   // bow: rosin red
        juce::Colour (0xff57b8a0),   // reed: teal
        juce::Colour (0xffe6c25a),   // lips: brass
        juce::Colour (0xff7d9be8),   // jet: blue
    };

    const char* kExciterDescriptions[] = {
        "Pluck: shaped pick burst",
        "Bow: stick-slip friction junction",
        "Reed: pressure-controlled reed valve",
        "Lips: mass-spring lip valve, solved per sample",
        "Air Jet: jet delay with cubic edge nonlinearity",
    };

    const char* kResonatorDescriptions[] = {
        "String: two waveguide segments with loss + stiffness",
        "Cylindrical Tube: closed at the driver, odd harmonics",
        "Conical Tube: driven near the narrow end, all harmonics",
        "Open Tube: open at both ends, all harmonics",
        "Horn: flared bore whose modes the lips select between",
    };

    // Per-exciter knob labels; an empty label means "not used".
    struct ExciterLabels { const char* id; const char* names[5]; };
    const ExciterLabels kExciterLabels[] = {
        //                Pluck          Bow            Reed            Lips           Air Jet
        { "tone",     { "Hardness",   "Bow Speed",   "Aperture",     "Lip Tension", "Jet Reflect" } },
        { "shape",    { "",           "Rosin",       "Reed Stiff",   "Lip Opening", "Jet Length" } },
        { "pressure", { "",           "Bow Force",   "Breath",       "Breath",      "Breath" } },
        { "noise",    { "Pick Noise", "Bow Noise",   "Breath Noise", "Breath Noise","Breath Noise" } },
        { "attack",   { "",           "Attack",      "Attack",       "Attack",      "Attack" } },
    };

    juce::String positionLabel (int exciter, int resonator)
    {
        const bool twoSegment = resonator == 0 || resonator == 2;
        if (exciter == 0) return "Pluck Pos";
        if (! twoSegment) return {};
        return exciter == 1 ? "Bow Pos" : "Drive Pos";
    }
}

//==============================================================================
FilamentLookAndFeel::FilamentLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, kText);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, kText);
    setColour (juce::ComboBox::backgroundColourId, kPanel);
    setColour (juce::ComboBox::outlineColourId, kDim.withAlpha (0.4f));
    setColour (juce::ComboBox::textColourId, kText);
    setColour (juce::ComboBox::arrowColourId, kText);
    setColour (juce::PopupMenu::backgroundColourId, kPanel);
    setColour (juce::PopupMenu::textColourId, kText);
}

void FilamentLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                            float startAngle, float endAngle, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (6.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto  centre = bounds.getCentre();
    const float angle  = startAngle + pos * (endAngle - startAngle);
    const float alpha  = s.isEnabled() ? 1.0f : 0.3f;
    const float track  = juce::jmax (2.5f, radius * 0.14f);

    juce::Path bg;
    bg.addCentredArc (centre.x, centre.y, radius - track, radius - track, 0.0f, startAngle, endAngle, true);
    g.setColour (kDim.withAlpha (0.35f * alpha));
    g.strokePath (bg, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path val;
    val.addCentredArc (centre.x, centre.y, radius - track, radius - track, 0.0f, startAngle, angle, true);
    g.setColour (accent.withAlpha (alpha));
    g.strokePath (val, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float inner = radius - track * 2.2f;
    g.setColour (kPanel.brighter (0.15f).withAlpha (alpha));
    g.fillEllipse (centre.x - inner, centre.y - inner, inner * 2.0f, inner * 2.0f);

    juce::Path pointer;
    pointer.addRoundedRectangle (-1.5f, -inner, 3.0f, inner * 0.55f, 1.5f);
    g.setColour (kText.withAlpha (alpha));
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
}

//==============================================================================
FilamentEditor::FilamentEditor (FilamentProcessor& p)
    : AudioProcessorEditor (p), processor (p),
      keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel (&lnf);

    auto make = [this] (const char* id, const char* text) -> Knob*
    {
        auto k = std::make_unique<Knob>();
        k->paramId = id;
        k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 16);
        k->slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        k->slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        k->slider.setColour (juce::Slider::textBoxHighlightColourId, kDim.withAlpha (0.4f));
        k->label.setText (text, juce::dontSendNotification);
        k->label.setJustificationType (juce::Justification::centred);
        k->label.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        addAndMakeVisible (k->slider);
        addAndMakeVisible (k->label);
        k->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, id, k->slider);
        knobs.push_back (std::move (k));
        return knobs.back().get();
    };

    sections.push_back ({ "EXCITER",   { make ("tone", "Tone"), make ("shape", "Shape"), make ("pressure", "Pressure"),
                                         make ("noise", "Noise"), make ("attack", "Attack") }, {} });
    sections.push_back ({ "RESONATOR", { make ("position", "Position"), make ("decay", "Decay"), make ("damping", "Damping"),
                                         make ("stiffness", "Stiffness"), make ("release", "Release") }, {} });
    sections.push_back ({ "MODULATION",{ make ("vibRate", "Vib Rate"), make ("vibDepth", "Vib Depth"),
                                         make ("bendRange", "Bend"), make ("velSens", "Vel Sens") }, {} });
    sections.push_back ({ "BODY / OUT",{ make ("bodyMix", "Body Mix"), make ("bodySize", "Body Size"),
                                         make ("level", "Level") }, {} });

    exciterBox.addItemList (FilamentProcessor::exciterNames(), 1);
    resonatorBox.addItemList (FilamentProcessor::resonatorNames(), 1);
    for (auto* l : { &exciterLabel, &resonatorLabel })
    {
        l->setFont (juce::FontOptions (11.5f, juce::Font::bold));
        l->setColour (juce::Label::textColourId, kDim);
        l->setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (*l);
    }
    addAndMakeVisible (exciterBox);
    addAndMakeVisible (resonatorBox);
    exciterAttachment   = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, "exciter", exciterBox);
    resonatorAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, "resonator", resonatorBox);

    keyboard.setAvailableRange (24, 107);   // C1..B7: 49 white keys
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, lnf.accent.withAlpha (0.6f));
    addAndMakeVisible (keyboard);

    setSize (900, 510);
    updateForModel();
    startTimerHz (15);
}

FilamentEditor::~FilamentEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

FilamentEditor::Knob& FilamentEditor::knob (const juce::String& id)
{
    for (auto& k : knobs)
        if (k->paramId == id) return *k;
    jassertfalse;
    return *knobs.front();
}

void FilamentEditor::timerCallback()
{
    const int e = (int) processor.apvts.getRawParameterValue ("exciter")->load();
    const int r = (int) processor.apvts.getRawParameterValue ("resonator")->load();
    if (e != currentExciter || r != currentResonator) updateForModel();
}

void FilamentEditor::updateForModel()
{
    currentExciter   = juce::jlimit (0, 4, (int) processor.apvts.getRawParameterValue ("exciter")->load());
    currentResonator = juce::jlimit (0, 4, (int) processor.apvts.getRawParameterValue ("resonator")->load());
    lnf.accent = kExciterAccent[currentExciter];
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, lnf.accent.withAlpha (0.6f));
    modelDescription = juce::String (kExciterDescriptions[currentExciter])
                     + juce::String::fromUTF8 ("   \xe2\x86\x92   ") + kResonatorDescriptions[currentResonator];

    auto setLabel = [this] (const char* id, const juce::String& name)
    {
        auto& k = knob (id);
        k.slider.setEnabled (name.isNotEmpty());
        k.label.setText (name.isNotEmpty() ? name : juce::String ("-"), juce::dontSendNotification);
        k.label.setAlpha (name.isNotEmpty() ? 1.0f : 0.35f);
    };

    for (auto& set : kExciterLabels)
        setLabel (set.id, set.names[currentExciter]);

    const bool drivenTube = currentExciter != 0 && currentResonator != 0;
    setLabel ("position", positionLabel (currentExciter, currentResonator));
    setLabel ("decay", drivenTube ? "Resonance" : "Decay");
    repaint();
}

void FilamentEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);

    // header
    g.setColour (lnf.accent);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText ("FILAMENT", 20, 12, 200, 32, juce::Justification::centredLeft);
    g.setColour (kDim);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("waveguide physical modeling synth", 162, 20, 260, 20, juce::Justification::centredLeft);
    g.drawText (modelDescription, 20, 62, getWidth() - 40, 18, juce::Justification::centredLeft);

    for (auto& s : sections)
    {
        g.setColour (kPanel);
        g.fillRoundedRectangle (s.bounds.toFloat(), 8.0f);
        g.setColour (kDim);
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText (s.title, s.bounds.getX() + 12, s.bounds.getY() + 6, 200, 16, juce::Justification::centredLeft);
    }
}

void FilamentEditor::resized()
{
    auto area = getLocalBounds().reduced (16, 0);
    auto header = area.removeFromTop (86);
    auto selectors = header.removeFromRight (440).withTrimmedTop (8).withHeight (52);
    auto excArea = selectors.removeFromLeft (215);
    selectors.removeFromLeft (10);
    exciterLabel.setBounds (excArea.removeFromTop (18));
    exciterBox.setBounds (excArea.withHeight (28));
    resonatorLabel.setBounds (selectors.removeFromTop (18));
    resonatorBox.setBounds (selectors.withHeight (28));

    keyboard.setBounds (getLocalBounds().removeFromBottom (70).reduced (16, 6));
    keyboard.setKeyWidth ((float) keyboard.getWidth() / 49.0f);
    area.removeFromBottom (78);

    const int gap = 10;
    auto row1 = area.removeFromTop ((area.getHeight() - gap) / 2);
    area.removeFromTop (gap);
    auto row2 = area;

    auto layoutRow = [gap] (juce::Rectangle<int> row, Section& a, Section& b)
    {
        const int total = (int) (a.knobs.size() + b.knobs.size());
        const int wA = (row.getWidth() - gap) * (int) a.knobs.size() / total;
        a.bounds = row.removeFromLeft (wA);
        row.removeFromLeft (gap);
        b.bounds = row;
    };
    layoutRow (row1, sections[0], sections[1]);
    layoutRow (row2, sections[2], sections[3]);

    for (auto& s : sections)
    {
        auto inner = s.bounds.reduced (8).withTrimmedTop (16);
        const int w = inner.getWidth() / (int) s.knobs.size();
        for (auto* k : s.knobs)
        {
            auto cell = inner.removeFromLeft (w);
            k->label.setBounds (cell.removeFromTop (18));
            k->slider.setBounds (cell.reduced (4, 0));
        }
    }
}
