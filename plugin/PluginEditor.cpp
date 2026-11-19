#include "PluginEditor.h"

namespace
{
    const juce::Colour kBackground { 0xff15171c };
    const juce::Colour kPanel      { 0xff1e2129 };
    const juce::Colour kText       { 0xffd9dde6 };
    const juce::Colour kDim        { 0xff7d8494 };
    const juce::Colour kStrangeAccent { 0xffb67cf2 };   // the strange knob is always violet

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
        { "bite",     { "",           "Bite",        "Tongue",       "Tongue",      "Chiff" } },
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
    const auto custom = s.getProperties()["accent"];
    g.setColour ((custom.isVoid() ? accent : juce::Colour ((juce::uint32) (int) custom)).withAlpha (alpha));
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
        k->label.setText (juce::String::fromUTF8 (text), juce::dontSendNotification);
        k->label.setJustificationType (juce::Justification::centred);
        k->label.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        addAndMakeVisible (k->slider);
        addAndMakeVisible (k->label);
        k->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, id, k->slider);
        knobs.push_back (std::move (k));
        return knobs.back().get();
    };

    sections.push_back ({ "EXCITER",   { make ("tone", "Tone"), make ("shape", "Shape"), make ("pressure", "Pressure"),
                                         make ("noise", "Noise"), make ("attack", "Attack"), make ("bite", "Bite") }, {} });
    sections.push_back ({ "RESONATOR", { make ("position", "Position"), make ("decay", "Decay"), make ("damping", "Damping"),
                                         make ("stiffness", "Stiffness"), make ("buzz", "Buzz"), make ("release", "Release") }, {} });
    sections.push_back ({ "EXPRESSION", { make ("atBreath", "AT \xe2\x86\x92 Breath"), make ("wheelBreath", "Wheel \xe2\x86\x92 Breath"),
                                          make ("exprCurve", "Curve"), make ("growl", "Growl"), make ("atStrange", "AT \xe2\x86\x92 Strange"),
                                          make ("wheelVib", "Wheel \xe2\x86\x92 Vib") }, {} });
    sections.push_back ({ "PLAY",       { make ("glide", "Glide"), make ("bendRange", "Bend"), make ("velSens", "Vel Sens"),
                                          make ("velBite", "Vel \xe2\x86\x92 Bite") }, {} });
    sections.back().extras = { { &modeLabel, &modeBox } };
    sections.push_back ({ "VIBRATO",    { make ("vibRate", "Rate"), make ("vibDepth", "Depth") }, {} });
    sections.push_back ({ "STRANGE",    { make ("strange", "Strange") }, {} });
    {
        auto& k = knob ("strange");
        k.slider.getProperties().set ("accent", (int) kStrangeAccent.getARGB());
        k.label.setColour (juce::Label::textColourId, kStrangeAccent);
    }
    sections.push_back ({ "SYMPATHETIC", { make ("symMix", "Mix"), make ("symDecay", "Decay") }, {} });
    sections.back().extras = { { &symKeyLabel, &symKeyBox }, { &symTuningLabel, &symTuningBox } };
    symKeyBox.addItemList ({ "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 1);
    symTuningBox.addItemList ({ "Chromatic", "Drone", "Major", "Minor", "Pentatonic" }, 1);
    for (auto* l : { &symKeyLabel, &symTuningLabel })
    {
        l->setFont (juce::FontOptions (13.0f, juce::Font::bold));
        l->setJustificationType (juce::Justification::centred);
        addAndMakeVisible (*l);
    }
    addAndMakeVisible (symKeyBox);
    addAndMakeVisible (symTuningBox);
    symKeyAttachment    = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, "symKey", symKeyBox);
    symTuningAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, "symTuning", symTuningBox);

    sections.push_back ({ "BODY / OUT", { make ("bodyMix", "Body Mix"), make ("bodySize", "Body Size"),
                                          make ("level", "Level") }, {} });

    modeBox.addItemList ({ "Poly", "Mono", "Legato" }, 1);
    addAndMakeVisible (modeBox);
    modeLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    modeLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (modeLabel);
    modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, "voiceMode", modeBox);

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

    // presets: grouped by category, with previous / next buttons
    {
        const auto& presets = processor.getPresets();
        juce::String lastCategory;
        for (int i = 0; i < (int) presets.size(); ++i)
        {
            if (lastCategory != presets[(size_t) i].category)
            {
                lastCategory = presets[(size_t) i].category;
                presetBox.addSectionHeading (lastCategory);
            }
            presetBox.addItem (presets[(size_t) i].name, i + 1);
        }
        presetBox.onChange = [this]
        {
            const int id = presetBox.getSelectedId();
            if (id > 0) { processor.applyPreset (id - 1); refreshPresetDisplay (true); }
        };
        auto step = [this] (int delta)
        {
            const int n = (int) processor.getPresets().size();
            processor.applyPreset ((processor.getPresetIndex() + delta + n) % n);
            refreshPresetDisplay (true);
        };
        prevPreset.onClick = [step] { step (-1); };
        nextPreset.onClick = [step] { step (+1); };
        for (auto* b : { &prevPreset, &nextPreset })
        {
            b->setColour (juce::TextButton::buttonColourId, kPanel);
            b->setColour (juce::TextButton::textColourOffId, kText);
            addAndMakeVisible (*b);
        }
        presetLabel.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        presetLabel.setColour (juce::Label::textColourId, kDim);
        addAndMakeVisible (presetLabel);
        addAndMakeVisible (presetBox);
        refreshPresetDisplay (true);
    }

    setSize (900, 660);
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

void FilamentEditor::refreshPresetDisplay (bool force)
{
    const int index = processor.getPresetIndex();
    const bool modified = processor.isPresetModified();
    if (! force && index == shownPreset && modified == shownModified) return;
    shownPreset = index; shownModified = modified;
    if (modified)
        presetBox.setText (juce::String (processor.getPresets()[(size_t) index].name) + " *", juce::dontSendNotification);
    else
        presetBox.setSelectedId (index + 1, juce::dontSendNotification);
}

void FilamentEditor::timerCallback()
{
    refreshPresetDisplay (false);
    const int e = (int) processor.apvts.getRawParameterValue ("exciter")->load();
    const int r = (int) processor.apvts.getRawParameterValue ("resonator")->load();
    const int m = (int) processor.apvts.getRawParameterValue ("voiceMode")->load();
    if (e != currentExciter || r != currentResonator || m != currentMode) updateForModel();
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

    // breath expression and growl only act on sustained exciters; glide only in mono modes
    currentMode = (int) processor.apvts.getRawParameterValue ("voiceMode")->load();
    const bool sustained = currentExciter != 0;
    setLabel ("atBreath",    sustained ? juce::String::fromUTF8 ("AT \xe2\x86\x92 Breath") : juce::String());
    setLabel ("wheelBreath", sustained ? juce::String::fromUTF8 ("Wheel \xe2\x86\x92 Breath") : juce::String());
    setLabel ("exprCurve",   sustained ? juce::String ("Curve") : juce::String());
    setLabel ("growl",       sustained ? juce::String ("Growl") : juce::String());
    setLabel ("glide",       currentMode != 0 ? juce::String ("Glide") : juce::String());
    setLabel ("velBite",     sustained ? juce::String::fromUTF8 ("Vel \xe2\x86\x92 Bite") : juce::String());
    repaint();
}

void FilamentEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);

    // header
    // "ROBOTMAKERS FILAMENT", stacked to fit beside the preset bar
    g.setColour (kText);
    g.setFont (juce::FontOptions (15.5f, juce::Font::bold));
    g.drawFittedText ("ROBOTMAKERS", 20, 9, 136, 18, juce::Justification::centredLeft, 1, 0.8f);
    g.setColour (lnf.accent);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawFittedText ("FILAMENT", 20, 25, 136, 30, juce::Justification::centredLeft, 1, 0.8f);
    g.setColour (kDim);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (modelDescription, 20, 62, getWidth() - 40, 18, juce::Justification::centredLeft);

    for (auto& s : sections)
    {
        g.setColour (s.title == "STRANGE" ? kPanel.interpolatedWith (kStrangeAccent, 0.08f) : kPanel);
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
    {
        auto pr = juce::Rectangle<int> (162, 8, 268, 52);
        presetLabel.setBounds (pr.removeFromTop (18));
        auto row = pr.withHeight (28);
        prevPreset.setBounds (row.removeFromLeft (28));
        nextPreset.setBounds (row.removeFromRight (28));
        presetBox.setBounds (row.reduced (4, 0));
    }
    exciterBox.setBounds (excArea.withHeight (28));
    resonatorLabel.setBounds (selectors.removeFromTop (18));
    resonatorBox.setBounds (selectors.withHeight (28));

    keyboard.setBounds (getLocalBounds().removeFromBottom (70).reduced (16, 6));
    keyboard.setKeyWidth ((float) keyboard.getWidth() / 49.0f);
    area.removeFromBottom (78);

    const int gap = 10;
    const int rowH = (area.getHeight() - 2 * gap) / 3;
    auto row1 = area.removeFromTop (rowH);
    area.removeFromTop (gap);
    auto row2 = area.removeFromTop (rowH);
    area.removeFromTop (gap);
    auto row3 = area;

    auto layoutRow = [gap] (juce::Rectangle<int> row, std::initializer_list<Section*> secs)
    {
        int total = 0;
        for (auto* sec : secs) total += sec->cells();
        const int usable = row.getWidth() - gap * ((int) secs.size() - 1);
        int i = 0;
        for (auto* sec : secs)
        {
            const bool last = ++i == (int) secs.size();
            sec->bounds = last ? row : row.removeFromLeft (usable * sec->cells() / total);
            if (! last) row.removeFromLeft (gap);
        }
    };
    layoutRow (row1, { &sections[0], &sections[1] });
    layoutRow (row2, { &sections[2], &sections[3] });
    layoutRow (row3, { &sections[4], &sections[5], &sections[6], &sections[7] });

    for (auto& s : sections)
    {
        auto inner = s.bounds.reduced (8).withTrimmedTop (16);
        const int w = inner.getWidth() / s.cells();
        for (auto& [label, comp] : s.extras)
        {
            auto cell = inner.removeFromLeft (w);
            label->setBounds (cell.removeFromTop (18));
            comp->setBounds (cell.withSizeKeepingCentre (cell.getWidth() - 8, 28).translated (0, -10));
        }
        for (auto* k : s.knobs)
        {
            auto cell = inner.removeFromLeft (w);
            k->label.setBounds (cell.removeFromTop (18));
            k->slider.setBounds (cell.reduced (4, 0));
        }
    }
}
