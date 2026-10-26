#include "PluginProcessor.h"
#include "PluginEditor.h"

SpectroCheckProcessor::SpectroCheckProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

void SpectroCheckProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate.store (sampleRate);
}

bool SpectroCheckProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

// El audio pasa sin tocarse: solo se copia a la cola para dibujarlo.
void SpectroCheckProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn = getTotalNumInputChannels();
    for (int ch = numIn; ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    if (numIn == 0) return;
    const float* l = buffer.getReadPointer (0);
    const float* r = numIn > 1 ? buffer.getReadPointer (1) : l;
    fifo.push (l, r, buffer.getNumSamples());
}

juce::AudioProcessorEditor* SpectroCheckProcessor::createEditor()
{
    return new SpectroCheckEditor (*this);
}

void SpectroCheckProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = settings.createXml())
        copyXmlToBinary (*xml, dest);
}

void SpectroCheckProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        auto loaded = juce::ValueTree::fromXml (*xml);
        if (loaded.hasType (settings.getType()))
            settings.copyPropertiesFrom (loaded, nullptr);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpectroCheckProcessor();
}
