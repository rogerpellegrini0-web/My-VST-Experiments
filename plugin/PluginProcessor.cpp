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
    raw.decay     = p ("decay");
    raw.damping   = p ("damping");
    raw.stiffness = p ("stiffness");
    raw.release   = p ("release");
    raw.vibRate   = p ("vibRate");
    raw.vibDepth  = p ("vibDepth");
    raw.bendRange = p ("bendRange");
    raw.velSens   = p ("velSens");
    raw.bodyMix   = p ("bodyMix");
    raw.bodySize  = p ("bodySize");
    raw.level     = p ("level");
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
    p.decay        = raw.decay->load();
    p.damping      = raw.damping->load();
    p.stiffness    = raw.stiffness->load();
    p.release      = raw.release->load();
    p.vibratoRate  = raw.vibRate->load();
    p.vibratoDepth = raw.vibDepth->load();
    p.bendRange    = raw.bendRange->load();
    p.velocitySens = raw.velSens->load();
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
    else if (m.isAllNotesOff())         engine.allNotesOff();
    else if (m.isAllSoundOff())         engine.reset();
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
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void FilamentProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            migrateFromV1 (*xml);
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
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
