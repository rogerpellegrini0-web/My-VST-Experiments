#include "SixteenProcessor.h"
#include "SixteenEditor.h"

using namespace vocoder;

SixteenProcessor::SixteenProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",      juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Side-chain", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output",     juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SIXTEEN", createLayout())
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    raw.power = get ("power");
    raw.mic = get ("mic");
    raw.programLevel = get ("programLevel");
    raw.carrierLevel = get ("carrierLevel");
    raw.sampleHold = get ("sampleHold");
    raw.externalPatch = get ("externalPatch");
    raw.hfDirect = get ("hfDirect");
    raw.balance = get ("balance");
    raw.mode = get ("mode");
    raw.status = get ("status");
    raw.preFilter = get ("preFilter");
    raw.routing = get ("routing");
    for (int j = 0; j < kNumChannels; ++j)
        raw.patch[(size_t) j] = apvts.getRawParameterValue (patchId (j));
}

juce::AudioProcessorValueTreeState::ParameterLayout SixteenProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto onOff = [] (const char* off, const char* on) { return AudioParameterBoolAttributes().withStringFromValueFunction ([=] (bool v, int) { return String (v ? on : off); }); };

    l.add (std::make_unique<AudioParameterBool> (ParameterID { "power", 1 }, "Power", true, onOff ("Off", "On")));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "mic", 1 }, "Mic/Line", StringArray { "Line", "Mic" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "programLevel", 1 }, "Program Level", NormalisableRange<float> (0.0f, 10.0f), 5.0f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "carrierLevel", 1 }, "Carrier Level", NormalisableRange<float> (0.0f, 10.0f), 5.0f));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "sampleHold", 1 }, "Sample/Hold", false, onOff ("Out", "In")));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "externalPatch", 1 }, "External Patch", false, onOff ("Out", "In")));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "hfDirect", 1 }, "5080-15000 Hz", StringArray { "Switched", "Direct" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "balance", 1 }, "Balance", NormalisableRange<float> (-5.0f, 5.0f), 0.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                                                  {
                                                      if (std::abs (v) < 0.05f) return String ("0");
                                                      return (v < 0 ? "Hiss " : "Buzz ") + String (std::abs (v), 1);
                                                  })));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Mode", StringArray { "Hiss", "Hiss & Buzz", "Buzz" }, 1));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "status", 1 }, "Status (Vocoder)", true, onOff ("Out", "In")));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "preFilter", 1 }, "259 Hz Pre-Filter", true, onOff ("Off", "On")));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "routing", 1 }, "Inputs",
                                                   StringArray { "Carrier = Main, Program = Side-chain", "Program = Main, Carrier = Side-chain" }, 0));

    for (int j = 0; j < kNumChannels; ++j)
    {
        const auto label = String (kPanelEdgesHz[(size_t) j]) + "-" + String (kPanelEdgesHz[(size_t) j + 1]);
        l.add (std::make_unique<AudioParameterInt> (ParameterID { patchId (j), 1 }, "Patch to Synth " + String (j + 1) + " (" + label + ")", 0, kNumChannels, 0,
                                                    AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int)
                                                    {
                                                        return v == 0 ? String ("None") : "Analyzer " + String (v);
                                                    })));
    }
    return l;
}

void SixteenProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate);
    programBuf.assign ((size_t) samplesPerBlock, 0.0f);
    carrierBuf.assign ((size_t) samplesPerBlock, 0.0f);
    outBuf.assign ((size_t) samplesPerBlock, 0.0f);
}

bool SixteenProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    const auto in = layouts.getMainInputChannelSet();
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

vocoder::Params SixteenProcessor::readParams() const
{
    Params p;
    p.power = raw.power->load() > 0.5f;
    p.micInput = raw.mic->load() > 0.5f;
    p.programLevel = raw.programLevel->load();
    p.carrierLevel = raw.carrierLevel->load();
    p.sampleHold = raw.sampleHold->load() > 0.5f || sustainDown;
    p.externalPatch = raw.externalPatch->load() > 0.5f;
    p.hfDirect = raw.hfDirect->load() > 0.5f;
    p.balance = raw.balance->load();
    p.mode = (Mode) juce::jlimit (0, 2, (int) std::lround (raw.mode->load()));
    p.vocoderOn = raw.status->load() > 0.5f;
    p.preFilter = raw.preFilter->load() > 0.5f;
    for (int j = 0; j < kNumChannels; ++j)
        p.patch[(size_t) j] = (int) std::lround (raw.patch[(size_t) j]->load()) - 1;
    return p;
}

// Sum a bus to mono into dst (zeros if the bus has no channels).
static void mixToMono (const juce::AudioBuffer<float>& bus, float* dst, int n)
{
    const int chans = bus.getNumChannels();
    if (chans == 0) { std::fill (dst, dst + n, 0.0f); return; }
    const float g = 1.0f / (float) chans;
    juce::FloatVectorOperations::copyWithMultiply (dst, bus.getReadPointer (0), g, n);
    for (int c = 1; c < chans; ++c)
        juce::FloatVectorOperations::addWithMultiply (dst, bus.getReadPointer (c), g, n);
}

void SixteenProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // Sustain pedal = SAMPLE/HOLD foot switch
    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
        if (msg.isSustainPedalOn())  sustainDown = true;
        if (msg.isSustainPedalOff()) sustainDown = false;
    }
    midi.clear();
    lamps.footHold = sustainDown;

    const int n = buffer.getNumSamples();
    if ((int) outBuf.size() < n)
    {
        programBuf.resize ((size_t) n); carrierBuf.resize ((size_t) n); outBuf.resize ((size_t) n);
    }

    auto mainIn = getBusBuffer (buffer, true, 0);
    const bool hasSide = getBusCount (true) > 1 && getBus (true, 1)->isEnabled() && getBus (true, 1)->getNumberOfChannels() > 0;
    lamps.sideChainConnected = hasSide;

    const bool programOnMain = raw.routing->load() > 0.5f;
    float* mainDst = programOnMain ? programBuf.data() : carrierBuf.data();
    float* sideDst = programOnMain ? carrierBuf.data() : programBuf.data();
    mixToMono (mainIn, mainDst, n);
    if (hasSide)
        mixToMono (getBusBuffer (buffer, true, 1), sideDst, n);
    else
        std::fill (sideDst, sideDst + n, 0.0f);

    {
        auto peakOf = [n] (const std::vector<float>& v) { auto r = juce::FloatVectorOperations::findMinAndMax (v.data(), n); return std::max (-r.getStart(), r.getEnd()); };
        const float pIn = peakOf (programBuf), cIn = peakOf (carrierBuf);
        if (pIn > lamps.programInput.load()) lamps.programInput = pIn;
        if (cIn > lamps.carrierInput.load()) lamps.carrierInput = cIn;
    }

    engine.process (programBuf.data(), carrierBuf.data(), outBuf.data(), n, readParams());

    auto out = getBusBuffer (buffer, false, 0);
    for (int c = 0; c < out.getNumChannels(); ++c)
        juce::FloatVectorOperations::copy (out.getWritePointer (c), outBuf.data(), n);
    for (int c = out.getNumChannels(); c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, n);

    const auto& ind = engine.indicators();
    auto peakHold = [] (std::atomic<float>& a, float v) { if (v > a.load()) a = v; };
    peakHold (lamps.programPeak, ind.programPeak);
    peakHold (lamps.carrierPeak, ind.carrierPeak);
    peakHold (lamps.hiss, ind.hiss);
    peakHold (lamps.buzz, ind.buzz);
}

int SixteenProcessor::getPatch (int j) const
{
    return (int) std::lround (raw.patch[(size_t) j]->load()) - 1;
}

void SixteenProcessor::setPatch (int j, int source)
{
    if (auto* p = apvts.getParameter (patchId (j)))
    {
        const float norm = p->convertTo0to1 ((float) (source + 1));
        p->beginChangeGesture();
        p->setValueNotifyingHost (norm);
        p->endChangeGesture();
    }
}

void SixteenProcessor::applyPatchPreset (const vocoder::PatchPreset& preset)
{
    for (int j = 0; j < kNumChannels; ++j)
        setPatch (j, preset.patch[(size_t) j]);
}

void SixteenProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SixteenProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SixteenProcessor::createEditor() { return new SixteenEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SixteenProcessor(); }
