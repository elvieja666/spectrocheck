#pragma once
#include "PluginProcessor.h"
#include "AnalyzerView.h"

class SpectroCheckEditor : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit SpectroCheckEditor (SpectroCheckProcessor&);
    ~SpectroCheckEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void configure();                    // rearma el análisis con los ajustes actuales
    void resetMeasurement (const juce::String& note = {});
    void updateVerdict();
    void setupCombo (juce::ComboBox&, const juce::Identifier&, const juce::StringArray&, int defaultIndex, const juce::String& tooltip);
    int  setting (const juce::Identifier& id, int def) const;
    double hopSeconds() const;

    SpectroCheckProcessor& proc;
    Theme::LookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    juce::ComboBox scaleBox, fftBox, speedBox, rangeBox, channelBox;
    juce::Label scaleLabel, fftLabel, speedLabel, rangeLabel, channelLabel;
    juce::TextButton guidesButton { juce::String::fromUTF8 ("Guías") }, profileButton { "Promedio" },
                     freezeButton { "Congelar" }, resetButton { "Reiniciar" };
    AnalyzerView view;

    sc::SpectrumAnalyzer analyzer;
    sc::CutoffDetector detector;
    sc::CutoffResult verdict;
    double configuredRate = 0.0;
    int seenPlayStarts = 0;
    int ticks = 0;
    std::vector<float> bufL, bufR, mono;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectroCheckEditor)
};
