#include "PluginEditor.h"

namespace
{
    const auto T = [] (const char* s) { return juce::String::fromUTF8 (s); };

    constexpr int topBarHeight = 58;
    constexpr int bottomBarHeight = 82;

    constexpr int fftOrders[]   = { 12, 13, 14 };
    constexpr double hops[]     = { 0.004, 0.010, 0.025 };   // segundos por columna
    constexpr float floors[]    = { -100.0f, -120.0f, -140.0f };
}

SpectroCheckEditor::SpectroCheckEditor (SpectroCheckProcessor& p)
    : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);

    setupCombo (scaleBox,   Settings::scale,   { "Log", "Lineal", "Agudos" }, 1,
                T ("Lineal reparte las frecuencias parejo y deja ver mejor el corte en los agudos. Agudos muestra solo de 10 kHz para arriba."));
    setupCombo (fftBox,     Settings::fft,     { "4096", "8192", "16384" }, 1,
                T ("Más grande = más detalle en frecuencia, menos detalle en el tiempo."));
    setupCombo (speedBox,   Settings::speed,   { T ("Rápida"), "Media", "Lenta" }, 1,
                T ("Qué tan rápido corre el espectrograma."));
    setupCombo (rangeBox,   Settings::range,   { "-100 dB", "-120 dB", "-140 dB" }, 1,
                T ("Nivel que se ve como negro. Más bajo = se ven detalles más débiles (y el ruido de fondo)."));
    setupCombo (channelBox, Settings::channel, { "L + R", "Izquierdo", "Derecho", "Lateral (S)" }, 0,
                T ("Lateral (S) es la diferencia entre canales: en muchos MP3 el estéreo corta más abajo que el centro."));

    auto label = [this] (juce::Label& l, const char* text, juce::Component& target)
    {
        l.setText (juce::String::fromUTF8 (text), juce::dontSendNotification);
        l.setFont (Theme::font (10.5f, 600, 0.06f));
        l.setColour (juce::Label::textColourId, Theme::dim);
        l.setJustificationType (juce::Justification::centredLeft);
        l.setBorderSize ({ 0, 1, 0, 0 });
        l.attachToComponent (&target, false);
        addAndMakeVisible (l);
    };
    label (scaleLabel, "ESCALA", scaleBox);
    label (fftLabel, "FFT", fftBox);
    label (speedLabel, "VELOCIDAD", speedBox);
    label (rangeLabel, "RANGO", rangeBox);
    label (channelLabel, "CANAL", channelBox);

    guidesButton.setClickingTogglesState (true);
    guidesButton.setToggleState ((bool) proc.settings.getProperty (Settings::guides, true), juce::dontSendNotification);
    guidesButton.setTooltip (T ("Líneas en 16, 19, 20 y 22 kHz, donde suelen cortar los MP3 según la calidad."));
    guidesButton.onClick = [this]
    {
        proc.settings.setProperty (Settings::guides, guidesButton.getToggleState(), nullptr);
        view.setGuides (guidesButton.getToggleState());
    };
    addAndMakeVisible (guidesButton);

    profileButton.setClickingTogglesState (true);
    profileButton.setToggleState ((bool) proc.settings.getProperty (Settings::profile, true), juce::dontSendNotification);
    profileButton.setTooltip (T ("Muestra u oculta el panel de promedio y picos de la derecha."));
    profileButton.onClick = [this]
    {
        proc.settings.setProperty (Settings::profile, profileButton.getToggleState(), nullptr);
        view.setShowProfile (profileButton.getToggleState());
    };
    addAndMakeVisible (profileButton);

    freezeButton.setClickingTogglesState (true);
    freezeButton.setTooltip (T ("Detiene la imagen para mirarla tranquilo. La medición también se pausa."));
    addAndMakeVisible (freezeButton);

    resetButton.setTooltip (T ("Empieza una medición nueva. También se reinicia sola después de 2 segundos de silencio."));
    resetButton.onClick = [this] { resetMeasurement(); };
    addAndMakeVisible (resetButton);

    view.setDetector (&detector);
    view.setGuides (guidesButton.getToggleState());
    view.setShowProfile (profileButton.getToggleState());
    addAndMakeVisible (view);

    bufL.resize (8192);
    bufR.resize (8192);
    mono.resize (8192);

    setResizable (true, true);
    setResizeLimits (980, 480, 2400, 1500);
    setSize ((int) proc.settings.getProperty (Settings::width, 1040),
             (int) proc.settings.getProperty (Settings::height, 600));

    proc.fifo.clear();
    seenPlayStarts = proc.playStarts.load();
    configure();
    startTimerHz (60);
}

SpectroCheckEditor::~SpectroCheckEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

int SpectroCheckEditor::setting (const juce::Identifier& id, int def) const
{
    return (int) proc.settings.getProperty (id, def);
}

double SpectroCheckEditor::hopSeconds() const
{
    return hops[juce::jlimit (0, 2, setting (Settings::speed, 1))];
}

void SpectroCheckEditor::setupCombo (juce::ComboBox& box, const juce::Identifier& id, const juce::StringArray& items,
                                     int defaultIndex, const juce::String& tooltip)
{
    box.addItemList (items, 1);
    box.setSelectedItemIndex (juce::jlimit (0, items.size() - 1, setting (id, defaultIndex)), juce::dontSendNotification);
    box.setTooltip (tooltip);
    box.onChange = [this, &box, id]
    {
        proc.settings.setProperty (id, box.getSelectedItemIndex(), nullptr);
        if (id == Settings::speed)
        {
            analyzer.setHopSeconds (hopSeconds());
            view.setColumnSeconds (analyzer.frameSeconds());
        }
        else if (id == Settings::scale)
            view.setAxis ((FreqAxis::Mode) box.getSelectedItemIndex(), analyzer.getSampleRate() * 0.5);
        else if (id == Settings::range)
            view.setFloorDb (floors[box.getSelectedItemIndex()]);
        else
            configure();   // FFT o canal: empieza una medición nueva
    };
    addAndMakeVisible (box);
}

void SpectroCheckEditor::configure()
{
    configuredRate = proc.currentSampleRate.load();
    analyzer.prepare (configuredRate, fftOrders[juce::jlimit (0, 2, setting (Settings::fft, 1))], hopSeconds());
    detector.reset (analyzer.numBins(), analyzer.binHz(), configuredRate);

    view.setAxis ((FreqAxis::Mode) juce::jlimit (0, 2, setting (Settings::scale, 1)), configuredRate * 0.5);
    view.setFloorDb (floors[juce::jlimit (0, 2, setting (Settings::range, 1))]);
    view.setBins (analyzer.numBins(), analyzer.binHz(), analyzer.frameSeconds());
    updateVerdict();
}

void SpectroCheckEditor::resetMeasurement (const juce::String& note)
{
    detector.reset (analyzer.numBins(), analyzer.binHz(), analyzer.getSampleRate());
    detector.setResetNote (note);
    updateVerdict();
}

void SpectroCheckEditor::timerCallback()
{
    if (std::abs (proc.currentSampleRate.load() - configuredRate) > 0.5)
        configure();

    // Le dieron Play en Logic: cada reproducción es una medición nueva.
    if (const int starts = proc.playStarts.load(); starts != seenPlayStarts)
    {
        seenPlayStarts = starts;
        resetMeasurement (T ("Le diste Play: medición nueva."));
    }

    const bool frozen = freezeButton.getToggleState();
    const int channel = channelBox.getSelectedItemIndex();

    for (;;)
    {
        const int n = proc.fifo.pop (bufL.data(), bufR.data(), (int) bufL.size());
        if (n <= 0 || frozen) { if (n <= 0) break; continue; }

        detector.pushStereo (bufL.data(), bufR.data(), n);   // huecos: siempre con L y R

        for (int i = 0; i < n; ++i)
        {
            const float l = bufL[(size_t) i], r = bufR[(size_t) i];
            switch (channel)
            {
                case 1:  mono[(size_t) i] = l; break;
                case 2:  mono[(size_t) i] = r; break;
                case 3:  mono[(size_t) i] = 0.5f * (l - r); break;
                default: mono[(size_t) i] = 0.5f * (l + r); break;
            }
        }

        analyzer.push (mono.data(), n, [this] (const float* db, int bins, float rms)
        {
            view.addColumn (db, bins, detector.takeHoleStripValue());
            detector.addFrame (db, bins, rms, analyzer.frameSeconds());
        });
    }

    view.flush();

    if (++ticks % 12 == 0 && ! frozen)   // 5 veces por segundo
        updateVerdict();
}

void SpectroCheckEditor::updateVerdict()
{
    verdict = detector.analyze();
    view.setResult (verdict);
    repaint (getLocalBounds().removeFromBottom (bottomBarHeight));
}

void SpectroCheckEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::bg);

    auto top = getLocalBounds().removeFromTop (topBarHeight);
    g.setColour (Theme::bar);
    g.fillRect (top);
    g.setColour (Theme::line);
    g.drawHorizontalLine (top.getBottom() - 1, 0.0f, (float) getWidth());

    g.setColour (Theme::text);
    g.setFont (Theme::font (17.0f, 600, -0.01f));
    g.drawText ("SpectroCheck", 18, 12, 160, 20, juce::Justification::centredLeft);
    g.setColour (Theme::faint);
    g.setFont (Theme::font (11.0f, 500));
    g.drawText (juce::String ("v") + JucePlugin_VersionString, 136, 15, 60, 16, juce::Justification::centredLeft);
    g.setColour (Theme::faint);
    g.setFont (Theme::font (11.0f));
    g.drawText (T ("¿Hasta dónde llega el audio?"), 18, 32, 190, 14, juce::Justification::centredLeft);

    // --- barra de abajo: el veredicto ---------------------------------------
    auto bottom = getLocalBounds().removeFromBottom (bottomBarHeight);
    g.setColour (Theme::bar);
    g.fillRect (bottom);
    g.setColour (Theme::line);
    g.drawHorizontalLine (bottom.getY(), 0.0f, (float) getWidth());

    auto info = bottom.reduced (18, 0);
    info.removeFromRight (resetButton.getWidth() + 12);
    const int y0 = bottom.getY();

    // datos de la medición a la derecha
    juce::String stats;
    stats << juce::String (analyzer.getSampleRate() / 1000.0, 1) << " kHz  " << T ("·") << "  FFT " << analyzer.getFftSize()
          << " (" << juce::String (analyzer.binHz(), 1) << " Hz)";
    juce::String measured = T ("Medido: ") + juce::String (juce::roundToInt (verdict.seconds)) + " s";
    if (verdict.kind != sc::CutoffResult::Kind::waiting && verdict.dropDb > 0.5f)
        measured << "  " << T ("·") << T ("  caída ") << juce::String (juce::roundToInt (verdict.dropDb)) << " dB";

    auto statsArea = info.removeFromRight (330);
    auto linesRow = info.withHeight (18).withY (y0 + 56);   // tercera fila de la izquierda
    g.setFont (Theme::font (12.0f));
    g.setColour (Theme::dim);
    g.drawText (measured, statsArea.withHeight (18).withY (y0 + 13), juce::Justification::centredRight);

    if (verdict.holesText.isNotEmpty())
    {
        const float score = juce::jmax (verdict.holesMid, verdict.holesSide);
        g.setColour (verdict.holesMeasured && score >= sc::CutoffDetector::holesStrongPct ? Theme::codec
                     : verdict.holesMeasured && score >= sc::CutoffDetector::holesSomePct ? Theme::text : Theme::dim);
        g.setFont (Theme::font (12.0f, 500));
        g.drawFittedText (verdict.holesText, statsArea.withHeight (18).withY (y0 + 35), juce::Justification::centredRight, 1, 0.85f);
    }

    g.setColour (Theme::faint);
    g.setFont (Theme::font (11.5f));
    g.drawText (stats, statsArea.withHeight (16).withY (y0 + 57), juce::Justification::centredRight);

    // veredicto
    const auto c = AnalyzerView::colourFor (verdict.kind);
    g.setColour (c);
    g.fillEllipse ((float) info.getX(), (float) y0 + 19.0f, 10.0f, 10.0f);
    if (verdict.kind != sc::CutoffResult::Kind::waiting)
    {
        g.setColour (c.withAlpha (0.25f));
        g.drawEllipse ((float) info.getX() - 3.0f, (float) y0 + 16.0f, 16.0f, 16.0f, 1.5f);
    }

    auto text = info.withTrimmedLeft (22).withTrimmedRight (12);
    g.setColour (verdict.kind == sc::CutoffResult::Kind::waiting ? Theme::text.withAlpha (0.7f) : Theme::text);
    g.setFont (Theme::font (16.0f, 600));
    g.drawText (verdict.title, text.withHeight (22).withY (y0 + 13), juce::Justification::centredLeft, true);
    g.setColour (Theme::dim);
    g.setFont (Theme::font (12.5f));
    g.drawFittedText (verdict.detail, text.withHeight (18).withY (y0 + 35), juce::Justification::centredLeft, 1, 0.85f);

    // líneas fijas
    if (verdict.linesText.isNotEmpty())
    {
        bool anyAbove = false;
        for (const auto& line : verdict.lines) anyAbove = anyAbove || line.aboveCutoff;
        const auto lc = anyAbove ? Theme::codec : (verdict.lines.empty() ? Theme::faint : Theme::tone);

        g.setColour (lc);
        g.fillRect ((float) linesRow.getX() + 1.0f, (float) linesRow.getCentreY() - 1.0f, 8.0f, 2.0f);
        g.setFont (Theme::font (12.5f, verdict.lines.empty() ? 400 : 500));
        g.drawFittedText (verdict.linesText, linesRow.withTrimmedLeft (22), juce::Justification::centredLeft, 1, 0.85f);
    }
}

void SpectroCheckEditor::resized()
{
    proc.settings.setProperty (Settings::width, getWidth(), nullptr);
    proc.settings.setProperty (Settings::height, getHeight(), nullptr);

    auto r = getLocalBounds();
    auto top = r.removeFromTop (topBarHeight);
    auto bottom = r.removeFromBottom (bottomBarHeight);

    // controles alineados a la derecha de la barra de arriba
    auto controls = top.reduced (16, 0);
    controls.removeFromLeft (196);
    const int comboY = 24, comboH = 26;

    auto place = [&] (juce::Component& c, int w)
    {
        auto slot = controls.removeFromRight (w);
        c.setBounds (slot.getX(), comboY, w, comboH);
        controls.removeFromRight (8);
    };
    place (freezeButton, 84);
    place (profileButton, 86);
    place (guidesButton, 66);
    controls.removeFromRight (6);
    place (channelBox, 106);
    place (rangeBox, 84);
    place (speedBox, 86);
    place (fftBox, 76);
    place (scaleBox, 86);

    resetButton.setBounds (bottom.getRight() - 16 - 92, bottom.getCentreY() - 14, 92, 28);

    view.setBounds (r.reduced (12, 10).withTrimmedLeft (-4));
}
