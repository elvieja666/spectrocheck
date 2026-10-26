#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <vector>

// ---------------------------------------------------------------------------
//  Cola sin bloqueos para pasar el audio (izquierdo y derecho) del hilo de
//  audio a la ventana. Si la ventana está cerrada y la cola se llena, las
//  muestras nuevas simplemente se descartan: el audio nunca se traba.
// ---------------------------------------------------------------------------
class StereoFifo
{
public:
    static constexpr int capacity = 1 << 17;   // ~3 s a 44.1 kHz

    StereoFifo() : fifo (capacity), left ((size_t) capacity), right ((size_t) capacity) {}

    void push (const float* l, const float* r, int n)
    {
        int s1, n1, s2, n2;
        fifo.prepareToWrite (n, s1, n1, s2, n2);
        if (n1 > 0) { std::copy (l, l + n1, left.begin() + s1);  std::copy (r, r + n1, right.begin() + s1); }
        if (n2 > 0) { std::copy (l + n1, l + n1 + n2, left.begin() + s2);  std::copy (r + n1, r + n1 + n2, right.begin() + s2); }
        fifo.finishedWrite (n1 + n2);
    }

    int pop (float* l, float* r, int maxN)
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (maxN, s1, n1, s2, n2);
        if (n1 > 0) { std::copy_n (left.begin() + s1, n1, l);       std::copy_n (right.begin() + s1, n1, r); }
        if (n2 > 0) { std::copy_n (left.begin() + s2, n2, l + n1);  std::copy_n (right.begin() + s2, n2, r + n1); }
        fifo.finishedRead (n1 + n2);
        return n1 + n2;
    }

    void clear() { fifo.reset(); }

private:
    juce::AbstractFifo fifo;
    std::vector<float> left, right;
};

// ---------------------------------------------------------------------------
//  Ajustes de la ventana (se guardan con la sesión de Logic).
// ---------------------------------------------------------------------------
namespace Settings
{
    inline const juce::Identifier scale   { "scale" };    // 0 Log, 1 Lineal, 2 Agudos
    inline const juce::Identifier fft     { "fft" };      // 0 4096, 1 8192, 2 16384
    inline const juce::Identifier speed   { "speed" };    // 0 Rápida, 1 Media, 2 Lenta
    inline const juce::Identifier range   { "range" };    // 0 -100, 1 -120, 2 -140 dB
    inline const juce::Identifier channel { "channel" };  // 0 L+R, 1 Izq, 2 Der, 3 Lateral
    inline const juce::Identifier guides  { "guides" };   // líneas en 16/19/20/22 kHz
    inline const juce::Identifier width   { "width" };
    inline const juce::Identifier height  { "height" };
}

class SpectroCheckProcessor : public juce::AudioProcessor
{
public:
    SpectroCheckProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    StereoFifo fifo;
    std::atomic<double> currentSampleRate { 44100.0 };
    juce::ValueTree settings { "SpectroCheckSettings" };

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectroCheckProcessor)
};
