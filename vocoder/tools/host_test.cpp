// Loads the built Sixteen.vst3 through JUCE's VST3 hosting code (the same plugin binary
// and wrapper a DAW uses) and pushes audio through the main and side-chain inputs.
//   sixteen_host_test <path to Sixteen.vst3>
#include <juce_audio_processors/juce_audio_processors.h>
#include <cstdio>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::VST3PluginFormat fmt;
    juce::OwnedArray<juce::PluginDescription> descs;
    fmt.findAllTypesForFile (descs, argv[1]);
    if (descs.isEmpty()) { std::printf ("no plugin found\n"); return 1; }
    juce::String err;
    auto inst = fmt.createInstanceFromDescription (*descs[0], 48000, 512, err);
    if (! inst) { std::printf ("load failed: %s\n", err.toRawUTF8()); return 1; }

    std::printf ("buses: in %d, out %d\n", inst->getBusCount (true), inst->getBusCount (false));
    for (int b = 0; b < inst->getBusCount (true); ++b)
        std::printf ("  in bus %d '%s' %s enabled=%d\n", b, inst->getBus (true, b)->getName().toRawUTF8(),
                     inst->getBus (true, b)->getCurrentLayout().getDescription().toRawUTF8(), inst->getBus (true, b)->isEnabled() ? 1 : 0);

    for (int mode = 0; mode < 3; ++mode)
    {
        auto layout = inst->getBusesLayout();
        if (layout.inputBuses.size() > 1)
            layout.inputBuses.getReference (1) = mode == 0 ? juce::AudioChannelSet::stereo()
                                               : mode == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::disabled();
        const bool ok = inst->setBusesLayout (layout);
        inst->prepareToPlay (48000, 512);
        const int nIn = inst->getTotalNumInputChannels(), nOut = inst->getTotalNumOutputChannels();
        juce::AudioBuffer<float> buf (juce::jmax (nIn, nOut), 512);
        juce::MidiBuffer midi;
        double peak = 0;
        for (int blk = 0; blk < 200; ++blk)
        {
            for (int n = 0; n < 512; ++n)
            {
                const double t = (blk * 512 + n) / 48000.0;
                const float car = (float) (0.5 * (2 * std::fmod (t * 110, 1.0) - 1));
                const float voice = (float) (0.5 * std::sin (2 * 3.14159265 * 700 * t));
                for (int c = 0; c < buf.getNumChannels(); ++c)
                    buf.setSample (c, n, c < 2 ? car : voice);
            }
            inst->processBlock (buf, midi);
            if (blk > 20) peak = std::max (peak, (double) buf.getMagnitude (0, 0, 512));
        }
        std::printf ("side-chain %s (layout ok %d, in %d, out %d): output peak %.4f\n",
                     mode == 0 ? "stereo" : mode == 1 ? "mono" : "off", ok ? 1 : 0, nIn, nOut, peak);
        inst->releaseResources();
    }
    return 0;
}
