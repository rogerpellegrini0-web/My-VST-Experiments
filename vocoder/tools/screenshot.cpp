// Renders the plugin editor to a PNG without a DAW (for checking the panel artwork):
//   sixteen_screenshot out.png [scale]
#include <juce_audio_utils/juce_audio_utils.h>
#include "SixteenProcessor.h"
#include <cstdio>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    std::unique_ptr<juce::AudioProcessor> proc (createPluginFilter());

    if (juce::String (argv[1]) == "--process")
    {
        // Smoke test of the bus handling: carrier on the main input, a 1 kHz "voice" on the side-chain.
        for (int sc : { 2, 1, 0 })
        {
            auto layout = proc->getBusesLayout();
            layout.inputBuses.getReference (1) = sc == 2 ? juce::AudioChannelSet::stereo()
                                               : sc == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::disabled();
            const bool ok = proc->setBusesLayout (layout);
            proc->prepareToPlay (48000, 512);
            juce::AudioBuffer<float> buf (proc->getTotalNumInputChannels(), 512);
            juce::MidiBuffer midi;
            double peak = 0;
            for (int blk = 0; blk < 100; ++blk)
            {
                for (int n = 0; n < 512; ++n)
                {
                    const double t = (blk * 512 + n) / 48000.0;
                    const float car = (float) (0.3 * (2 * std::fmod (t * 110, 1.0) - 1));
                    const float voice = (float) (0.5 * std::sin (2 * 3.14159265 * 1000 * t));
                    for (int c = 0; c < buf.getNumChannels(); ++c)
                        buf.setSample (c, n, c < 2 ? car : voice);
                }
                proc->processBlock (buf, midi);
                peak = std::max (peak, (double) buf.getMagnitude (0, 0, 512));
            }
            std::printf ("side-chain channels %d (layout ok %d): input channels %d, output peak %.3f\n", sc, ok ? 1 : 0,
                         proc->getTotalNumInputChannels(), peak);
        }
        return 0;
    }

    auto* sixteen = dynamic_cast<SixteenProcessor*> (proc.get());
    if (argc > 3 && sixteen != nullptr)   // demo state: a patch preset loaded
    {
        sixteen->applyPatchPreset (vocoder::patchPresets()[(size_t) juce::String (argv[3]).getIntValue()]);
        sixteen->apvts.getParameter ("externalPatch")->setValueNotifyingHost (1.0f);
        sixteen->lamps.buzz = 1.0f;
        sixteen->lamps.programPeak = 1.2f;
        sixteen->lamps.carrierPeak = 0.3f;
        sixteen->lamps.sideChainConnected = true;
    }
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditor());
    const float scale = argc > 2 ? juce::String (argv[2]).getFloatValue() : 1.0f;
    if (auto* t = dynamic_cast<juce::Timer*> (ed.get())) t->stopTimer();
    // let the lamps pick up the demo values once
    if (auto* t = dynamic_cast<juce::Timer*> (ed.get())) t->timerCallback();
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, scale);
    juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat().writeImageToStream (img, os);
    return 0;
}
