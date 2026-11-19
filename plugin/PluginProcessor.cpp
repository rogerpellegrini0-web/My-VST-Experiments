#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    using APF = juce::AudioParameterFloat;

    juce::NormalisableRange<float> skewed (float lo, float hi, float centre)
    {
        juce::NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    }

    juce::AudioParameterFloatAttributes percent()
    {
        return juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; });
    }

    juce::AudioParameterFloatAttributes seconds()
    {
        return juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int)
            {
                return v < 1.0f ? juce::String (juce::roundToInt (v * 1000.0f)) + " ms"
                                : juce::String (v, 2) + " s";
            });
    }

    juce::AudioParameterFloatAttributes withSuffix (const char* suffix, int decimals)
    {
        return juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [suffix, decimals] (float v, int) { return juce::String (v, decimals) + suffix; });
    }
}

juce::StringArray FilamentProcessor::exciterNames()
{
    return { "Pluck", "Bow", "Reed", "Lips", "Air Jet" };
}

juce::StringArray FilamentProcessor::resonatorNames()
{
    return { "String", "Cylindrical Tube", "Conical Tube", "Open Tube", "Horn" };
}

juce::AudioProcessorValueTreeState::ParameterLayout FilamentProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    auto id = [] (const char* s) { return juce::ParameterID { s, 1 }; };

    l.add (std::make_unique<juce::AudioParameterChoice> (id ("exciter"),   "Exciter",   exciterNames(), 0));
    l.add (std::make_unique<juce::AudioParameterChoice> (id ("resonator"), "Resonator", resonatorNames(), 0));

    l.add (std::make_unique<APF> (id ("tone"),      "Tone",      juce::NormalisableRange<float> (0, 1), 0.5f, percent()));
    l.add (std::make_unique<APF> (id ("shape"),     "Shape",     juce::NormalisableRange<float> (0, 1), 0.5f, percent()));
    l.add (std::make_unique<APF> (id ("pressure"),  "Pressure",  juce::NormalisableRange<float> (0, 1), 0.5f, percent()));
    l.add (std::make_unique<APF> (id ("noise"),     "Noise",     juce::NormalisableRange<float> (0, 1), 0.2f, percent()));
    l.add (std::make_unique<APF> (id ("bite"),      "Bite",      juce::NormalisableRange<float> (0, 1), 0.5f, percent()));
    l.add (std::make_unique<APF> (id ("attack"),    "Attack",    skewed (0.001f, 2.0f, 0.15f), 0.05f, seconds()));

    l.add (std::make_unique<APF> (id ("position"),  "Position",  juce::NormalisableRange<float> (0, 1), 0.5f, percent()));
    l.add (std::make_unique<APF> (id ("decay"),     "Decay",     skewed (0.1f, 30.0f, 3.0f), 4.0f, seconds()));
    l.add (std::make_unique<APF> (id ("damping"),   "Damping",   juce::NormalisableRange<float> (0, 1), 0.5f, percent()));
    l.add (std::make_unique<APF> (id ("stiffness"), "Stiffness", juce::NormalisableRange<float> (0, 1), 0.1f, percent()));
    l.add (std::make_unique<APF> (id ("release"),   "Release",   skewed (0.01f, 5.0f, 0.5f), 0.3f, seconds()));

    l.add (std::make_unique<APF> (id ("vibRate"),   "Vibrato Rate",  juce::NormalisableRange<float> (0.5f, 10.0f), 5.0f, withSuffix (" Hz", 1)));
    l.add (std::make_unique<APF> (id ("vibDepth"),  "Vibrato Depth", juce::NormalisableRange<float> (0, 1), 0.15f, percent()));
    l.add (std::make_unique<APF> (id ("bendRange"), "Bend Range",    juce::NormalisableRange<float> (0, 24, 1), 2.0f, withSuffix (" st", 0)));
    l.add (std::make_unique<APF> (id ("velSens"),   "Velocity Sens", juce::NormalisableRange<float> (0, 1), 0.7f, percent()));

    l.add (std::make_unique<APF> (id ("velBite"),   "Velocity to Bite", juce::NormalisableRange<float> (0, 1), 0.0f, percent()));
    l.add (std::make_unique<juce::AudioParameterChoice> (id ("voiceMode"), "Voice Mode", juce::StringArray { "Poly", "Mono", "Legato" }, 0));
    l.add (std::make_unique<APF> (id ("glide"),     "Glide",     skewed (0.0f, 2.0f, 0.25f), 0.0f, seconds()));

    l.add (std::make_unique<APF> (id ("atBreath"),    "AT to Breath",    juce::NormalisableRange<float> (0, 1), 0.0f, percent()));
    l.add (std::make_unique<APF> (id ("wheelBreath"), "Wheel to Breath", juce::NormalisableRange<float> (0, 1), 0.0f, percent()));
    l.add (std::make_unique<APF> (id ("wheelVib"),    "Wheel to Vibrato",juce::NormalisableRange<float> (0, 1), 1.0f, percent()));
    l.add (std::make_unique<APF> (id ("exprCurve"),   "Expression Curve",juce::NormalisableRange<float> (-1, 1), 0.0f,
                                  juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                                  {
                                      if (std::abs (v) < 0.02f) return juce::String ("Linear");
                                      return (v > 0 ? juce::String ("Light ") : juce::String ("Firm ")) + juce::String (juce::roundToInt (std::abs (v) * 100.0f));
                                  })));
    l.add (std::make_unique<APF> (id ("growl"),       "Growl",           juce::NormalisableRange<float> (0, 1), 0.0f, percent()));
    l.add (std::make_unique<APF> (id ("atStrange"),   "AT to Strange",   juce::NormalisableRange<float> (0, 1), 0.0f, percent()));
    l.add (std::make_unique<APF> (id ("strange"),     "Strange",         juce::NormalisableRange<float> (0, 1), 0.0f, percent()));

    l.add (std::make_unique<APF> (id ("buzz"),        "Buzz",            juce::NormalisableRange<float> (0, 1), 0.0f, percent()));
    l.add (std::make_unique<APF> (id ("symMix"),      "Sympathetic Mix", juce::NormalisableRange<float> (0, 1), 0.0f, percent()));
    l.add (std::make_unique<APF> (id ("symDecay"),    "Sympathetic Decay", skewed (0.5f, 20.0f, 5.0f), 6.0f, seconds()));
    l.add (std::make_unique<juce::AudioParameterChoice> (id ("symKey"), "Sympathetic Key",
        juce::StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0));
    l.add (std::make_unique<juce::AudioParameterChoice> (id ("symTuning"), "Sympathetic Tuning",
        juce::StringArray { "Chromatic", "Drone", "Major", "Minor", "Pentatonic" }, 1));

    l.add (std::make_unique<APF> (id ("bodyMix"),   "Body Mix",  juce::NormalisableRange<float> (0, 1), 0.4f, percent()));
    l.add (std::make_unique<APF> (id ("bodySize"),  "Body Size", juce::NormalisableRange<float> (0, 1), 0.5f, percent()));
    l.add (std::make_unique<APF> (id ("level"),     "Level",     juce::NormalisableRange<float> (-36.0f, 6.0f), -6.0f, withSuffix (" dB", 1)));
    return l;
}

FilamentProcessor::FilamentProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "FilamentState", createLayout())
{
    auto p = [this] (const char* name) { return apvts.getRawParameterValue (name); };
    raw.exciter   = p ("exciter");
    raw.resonator = p ("resonator");
    raw.tone      = p ("tone");
    raw.shape     = p ("shape");
    raw.position  = p ("position");
    raw.pressure  = p ("pressure");
    raw.noise     = p ("noise");
    raw.attack    = p ("attack");
    raw.bite      = p ("bite");
    raw.decay     = p ("decay");
    raw.damping   = p ("damping");
    raw.stiffness = p ("stiffness");
    raw.release   = p ("release");
    raw.vibRate   = p ("vibRate");
    raw.vibDepth  = p ("vibDepth");
    raw.bendRange = p ("bendRange");
    raw.velSens   = p ("velSens");
    raw.voiceMode = p ("voiceMode");
    raw.velBite   = p ("velBite");
    raw.glide     = p ("glide");
    raw.atBreath  = p ("atBreath");
    raw.wheelBreath = p ("wheelBreath");
    raw.wheelVib  = p ("wheelVib");
    raw.exprCurve = p ("exprCurve");
    raw.growl     = p ("growl");
    raw.atStrange = p ("atStrange");
    raw.strange   = p ("strange");
    raw.buzz      = p ("buzz");
    raw.symMix    = p ("symMix");
    raw.symDecay  = p ("symDecay");
    raw.symKey    = p ("symKey");
    raw.symTuning = p ("symTuning");
    raw.bodyMix   = p ("bodyMix");
    raw.bodySize  = p ("bodySize");
    raw.level     = p ("level");

    for (auto* param : getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (param))
            apvts.addParameterListener (withId->paramID, this);
    apvts.state.setProperty ("presetName", "Init", nullptr);
}

FilamentProcessor::~FilamentProcessor()
{
    for (auto* param : getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (param))
            apvts.removeParameterListener (withId->paramID, this);
}

//==============================================================================
void FilamentProcessor::parameterChanged (const juce::String&, float)
{
    if (! applyingPreset)
        presetModified = true;
}

void FilamentProcessor::setParam (const char* id, float value)
{
    if (auto* param = apvts.getParameter (id))
        param->setValueNotifyingHost (param->convertTo0to1 (value));
}

int FilamentProcessor::getPresetIndex() const
{
    // stored by name, so adding presets in later versions doesn't relabel old projects
    const auto name = apvts.state.getProperty ("presetName").toString();
    for (int i = 0; i < (int) presets.size(); ++i)
        if (name == presets[(size_t) i].name) return i;
    return 0;
}

void FilamentProcessor::applyPreset (int index)
{
    if (index < 0 || index >= (int) presets.size()) return;
    const auto& p = presets[(size_t) index].params;

    applyingPreset = true;
    setParam ("exciter",     (float) (int) p.exciter);
    setParam ("resonator",   (float) (int) p.resonator);
    setParam ("tone",        p.tone);
    setParam ("shape",       p.shape);
    setParam ("pressure",    p.pressure);
    setParam ("noise",       p.noise);
    setParam ("attack",      p.attack);
    setParam ("bite",        p.bite);
    setParam ("position",    p.position);
    setParam ("decay",       p.decay);
    setParam ("damping",     p.damping);
    setParam ("stiffness",   p.stiffness);
    setParam ("release",     p.release);
    setParam ("vibRate",     p.vibratoRate);
    setParam ("vibDepth",    p.vibratoDepth);
    setParam ("bendRange",   p.bendRange);
    setParam ("velSens",     p.velocitySens);
    setParam ("voiceMode",   (float) (int) p.voiceMode);
    setParam ("glide",       p.glide);
    setParam ("velBite",     p.velToBite);
    setParam ("atBreath",    p.atToBreath);
    setParam ("wheelBreath", p.wheelToBreath);
    setParam ("wheelVib",    p.wheelToVibrato);
    setParam ("exprCurve",   p.exprCurve);
    setParam ("growl",       p.growl);
    setParam ("atStrange",   p.atToStrange);
    setParam ("strange",     p.strange);
    setParam ("buzz",        p.buzz);
    setParam ("symMix",      p.symMix);
    setParam ("symDecay",    p.symDecay);
    setParam ("symKey",      (float) p.symKey);
    setParam ("symTuning",   (float) p.symTuning);
    setParam ("bodyMix",     p.bodyMix);
    setParam ("bodySize",    p.bodySize);
    setParam ("level",       p.levelDb);
    applyingPreset = false;

    apvts.state.setProperty ("presetName", juce::String (presets[(size_t) index].name), nullptr);
    presetModified = false;
    currentPreset = index;
    updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withProgramChanged (true));
}

const juce::String FilamentProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, (int) presets.size()) ? juce::String (presets[(size_t) index].name)
                                                                  : juce::String();
}

// Called by the host for a program change, possibly on the audio thread. The preset is
// applied later on the message thread. Selecting the preset that is already loaded does
// nothing: hosts re-send the saved program when a project is reopened, and that must not
// wipe the user's edits.
void FilamentProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) presets.size())) return;
    if (index == currentPreset.load() && pendingProgram.load() < 0) return;
    pendingProgram = index;
    triggerAsyncUpdate();
}

void FilamentProcessor::handleAsyncUpdate()
{
    const int index = pendingProgram.exchange (-1);
    if (index >= 0 && index != currentPreset.load())
        applyPreset (index);
}

bool FilamentProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return layouts.getMainInputChannelSet().isDisabled()
        && (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono());
}

void FilamentProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.setParams (readParams());
    engine.prepare (sampleRate, samplesPerBlock);
    scratch.setSize (1, samplesPerBlock);
    keyboardState.reset();
}

filament::Params FilamentProcessor::readParams() const
{
    filament::Params p;
    p.exciter      = (filament::Exciter)   juce::jlimit (0, filament::kNumExciters - 1,   (int) raw.exciter->load());
    p.resonator    = (filament::Resonator) juce::jlimit (0, filament::kNumResonators - 1, (int) raw.resonator->load());
    p.tone         = raw.tone->load();
    p.shape        = raw.shape->load();
    p.position     = raw.position->load();
    p.pressure     = raw.pressure->load();
    p.noise        = raw.noise->load();
    p.attack       = raw.attack->load();
    p.bite         = raw.bite->load();
    p.decay        = raw.decay->load();
    p.damping      = raw.damping->load();
    p.stiffness    = raw.stiffness->load();
    p.release      = raw.release->load();
    p.vibratoRate  = raw.vibRate->load();
    p.vibratoDepth = raw.vibDepth->load();
    p.bendRange    = raw.bendRange->load();
    p.velocitySens = raw.velSens->load();
    p.voiceMode    = (filament::VoiceMode) juce::jlimit (0, 2, (int) raw.voiceMode->load());
    p.glide        = raw.glide->load();
    p.velToBite    = raw.velBite->load();
    p.atToBreath   = raw.atBreath->load();
    p.wheelToBreath  = raw.wheelBreath->load();
    p.wheelToVibrato = raw.wheelVib->load();
    p.exprCurve    = raw.exprCurve->load();
    p.growl        = raw.growl->load();
    p.atToStrange  = raw.atStrange->load();
    p.strange      = raw.strange->load();
    p.buzz         = raw.buzz->load();
    p.symMix       = raw.symMix->load();
    p.symDecay     = raw.symDecay->load();
    p.symKey       = juce::jlimit (0, 11, (int) raw.symKey->load());
    p.symTuning    = juce::jlimit (0, 4, (int) raw.symTuning->load());
    p.bodyMix      = raw.bodyMix->load();
    p.bodySize     = raw.bodySize->load();
    p.levelDb      = raw.level->load();
    return p;
}

void FilamentProcessor::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn())                   engine.noteOn (m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff())             engine.noteOff (m.getNoteNumber());
    else if (m.isPitchWheel())          engine.setPitchBend ((float) (m.getPitchWheelValue() - 8192) / 8192.0f);
    else if (m.isSustainPedalOn())      engine.setSustain (true);
    else if (m.isSustainPedalOff())     engine.setSustain (false);
    else if (m.isControllerOfType (1)) engine.setModWheel ((float) m.getControllerValue() / 127.0f);
    else if (m.isControllerOfType (2)) engine.setBreath ((float) m.getControllerValue() / 127.0f);
    else if (m.isChannelPressure())     engine.setChannelPressure ((float) m.getChannelPressureValue() / 127.0f);
    else if (m.isAftertouch())          engine.setPolyPressure (m.getNoteNumber(), (float) m.getAfterTouchValue() / 127.0f);
    else if (m.isAllNotesOff())         engine.allNotesOff();
    else if (m.isAllSoundOff())         engine.reset();
    else if (m.isProgramChange())       setCurrentProgram (m.getProgramChangeNumber());   // raw MIDI (standalone, some hosts)
}

void FilamentProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();

    keyboardState.processNextMidiBuffer (midi, 0, n, true);
    engine.setParams (readParams());

    if (scratch.getNumSamples() < n)
        scratch.setSize (1, n, false, false, true);

    float* left  = buffer.getWritePointer (0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : scratch.getWritePointer (0);

    int pos = 0;
    for (const auto meta : midi)
    {
        const int t = juce::jlimit (0, n, meta.samplePosition);
        if (t > pos)
        {
            engine.render (left + pos, right + pos, t - pos);
            pos = t;
        }
        handleMidi (meta.getMessage());
    }
    if (pos < n)
        engine.render (left + pos, right + pos, n - pos);

    if (buffer.getNumChannels() == 1)   // mono host bus: fold down
        for (int i = 0; i < n; ++i) left[i] = 0.5f * (left[i] + right[i]);
}

juce::AudioProcessorEditor* FilamentProcessor::createEditor()
{
    return new FilamentEditor (*this);
}

void FilamentProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    apvts.state.setProperty ("presetModified", presetModified.load(), nullptr);
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void FilamentProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            // a project being restored wins over a program change still waiting to be applied
            pendingProgram = -1;
            cancelPendingUpdate();
            migrateFromV1 (*xml);
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            // projects from before presets existed: show as a modified Init
            presetModified = (bool) apvts.state.getProperty ("presetModified", true);
            // version 0.5 stored the preset's position; its list had no buzz/sympathetic presets
            if (! apvts.state.hasProperty ("presetName") && apvts.state.hasProperty ("presetIndex"))
            {
                static const char* v05[] = { "Init", "Nylon Guitar", "Steel Guitar", "Concert Harp", "Hammered Dulcimer",
                    "Cello", "Violin", "Clarinet", "Alto Sax", "Oboe", "Trumpet", "French Horn", "Flute", "Pan Pipe",
                    "Bowed Cone", "Reed Fiddle", "Plucked Horn", "Brass Strings", "Air Clarinet", "Bell Garden",
                    "Gong Bloom", "Underwater Plates", "Glass Bow", "Bowed Metal", "E-Bow Drone", "Brass Scream",
                    "Sax Growler", "Broken Bugle", "Chaos Choir", "Buzz Reed Drone" };
                const int i = juce::jlimit (0, 29, (int) apvts.state.getProperty ("presetIndex"));
                apvts.state.setProperty ("presetName", juce::String (v05[i]), nullptr);
            }
            currentPreset = getPresetIndex();
            updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withProgramChanged (true));
        }
}

// Version 0.1 had a single "model" choice (plucked string, bowed string, reed, flute) and
// used "position" for reed stiffness / jet length. Translate such projects to the
// exciter + resonator layout so they still sound the same.
void FilamentProcessor::migrateFromV1 (juce::XmlElement& xml)
{
    auto* model = xml.getChildByAttribute ("id", "model");
    if (model == nullptr || xml.getChildByAttribute ("id", "exciter") != nullptr)
        return;

    const int m = juce::jlimit (0, 3, juce::roundToInt (model->getDoubleAttribute ("value")));
    const int exciter[]   = { 0, 1, 2, 4 };   // pluck, bow, reed, jet
    const int resonator[] = { 0, 0, 1, 3 };   // string, string, cylindrical, open

    auto add = [&xml] (const char* id, double value)
    {
        auto* e = xml.createNewChildElement ("PARAM");
        e->setAttribute ("id", id);
        e->setAttribute ("value", value);
    };
    add ("exciter", exciter[m]);
    add ("resonator", resonator[m]);

    if (m >= 2)   // position used to be reed stiffness / jet length
        if (auto* pos = xml.getChildByAttribute ("id", "position"))
        {
            add ("shape", pos->getDoubleAttribute ("value", 0.5));
            pos->setAttribute ("value", 0.5);
        }

    xml.removeChildElement (model, true);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FilamentProcessor();
}
