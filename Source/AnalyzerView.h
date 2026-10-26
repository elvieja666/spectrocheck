#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Analyzer.h"
#include "Theme.h"

// ---------------------------------------------------------------------------
//  Eje de frecuencia compartido por el espectrograma y el perfil.
//  Log: 20 Hz hasta Nyquist. Lineal: 0 hasta Nyquist. Agudos: 10 kHz hasta
//  Nyquist (para mirar de cerca dónde corta).
// ---------------------------------------------------------------------------
struct FreqAxis
{
    enum Mode { logMode = 0, linMode = 1, highMode = 2 };
    Mode mode = linMode;
    double nyquist = 22050.0;

    double lo() const
    {
        if (mode == logMode)  return 20.0;
        if (mode == highMode) return juce::jmin (10000.0, nyquist * 0.5);
        return 0.0;
    }
    double hi() const { return nyquist; }

    // 0 = abajo, 1 = arriba
    double toNorm (double f) const
    {
        if (mode == logMode)
            return std::log (juce::jmax (f, lo()) / lo()) / std::log (hi() / lo());
        return (f - lo()) / (hi() - lo());
    }

    double fromNorm (double t) const
    {
        if (mode == logMode)
            return lo() * std::pow (hi() / lo(), t);
        return lo() + t * (hi() - lo());
    }
};

inline juce::String freqLabel (double f, bool precise = false)
{
    if (f < 1000.0) return juce::String (juce::roundToInt (f)) + (precise ? " Hz" : "");
    const double k = f / 1000.0;
    const int decimals = precise ? 2 : (std::abs (k - std::round (k)) < 0.01 ? 0 : 1);
    return juce::String (k, decimals) + (precise ? " kHz" : "k");
}

// ---------------------------------------------------------------------------
//  El panel principal: eje a la izquierda, espectrograma que corre de
//  derecha a izquierda, y a la derecha el "perfil" (espectro promedio y
//  picos de toda la medición) alineado con el mismo eje.
// ---------------------------------------------------------------------------
class AnalyzerView : public juce::Component
{
public:
    static constexpr float ceilingDb = -10.0f;

    AnalyzerView() { setOpaque (true); }

    // --- configuración -----------------------------------------------------
    void setAxis (FreqAxis::Mode mode, double nyquist)
    {
        axis.mode = mode;
        axis.nyquist = nyquist;
        rebuildRows();
        clearHistory();
    }

    void setBins (int bins, double hz, double secondsPerColumn)
    {
        numBins = bins;
        binHz = hz;
        columnSeconds = secondsPerColumn;
        rebuildRows();
        clearHistory();
    }

    void setColumnSeconds (double s) { columnSeconds = s; repaint(); }
    void setFloorDb (float f)        { floorDb = f; clearHistory(); }
    void setGuides (bool on)         { showGuides = on; repaint(); }
    void setDetector (const sc::CutoffDetector* d) { detector = d; }

    void setResult (const sc::CutoffResult& r)
    {
        result = r;
        repaint (profileArea.getUnion (spectroArea).expanded (2));
    }

    void clearHistory()
    {
        if (spectroArea.isEmpty()) return;
        image = juce::Image (juce::Image::RGB, juce::jmax (1, spectroArea.getWidth()), juce::jmax (1, spectroArea.getHeight()), true);
        writeX = 0;
        repaint();
    }

    // --- datos ---------------------------------------------------------------
    void addColumn (const float* db, int n)
    {
        if (! image.isValid() || n != numBins || rowBin0.empty()) return;

        const auto& lut = Theme::palette();
        const float span = ceilingDb - floorDb;
        juce::Image::BitmapData px (image, writeX, 0, 1, image.getHeight(), juce::Image::BitmapData::writeOnly);

        for (int y = 0; y < image.getHeight(); ++y)
        {
            const int b0 = rowBin0[(size_t) y], b1 = rowBin1[(size_t) y];
            float v;
            if (b1 > b0)        // varias bandas en un píxel: quedarse con la más fuerte
            {
                v = db[b0];
                for (int b = b0 + 1; b <= b1; ++b) v = juce::jmax (v, db[b]);
            }
            else                // menos de una banda por píxel: interpolar
            {
                const float f = rowFrac[(size_t) y];
                v = db[b0] + f * (db[juce::jmin (b0 + 1, n - 1)] - db[b0]);
            }
            const int idx = juce::jlimit (0, 255, (int) ((v - floorDb) / span * 255.0f));
            px.setPixelColour (0, y, juce::Colour (lut[(size_t) idx]));
        }

        writeX = (writeX + 1) % image.getWidth();
        dirty = true;
    }

    void flush()
    {
        if (dirty) { repaint (spectroArea); dirty = false; }
    }

    // --- dibujo --------------------------------------------------------------
    void resized() override
    {
        auto r = getLocalBounds();
        axisArea = r.removeFromLeft (52);
        profileArea = r.removeFromRight (juce::jlimit (110, 170, getWidth() / 7));
        r.removeFromRight (8);
        timeArea = r.removeFromBottom (18);
        profileArea.removeFromBottom (18);
        axisArea.removeFromBottom (18);
        spectroArea = r;
        rebuildRows();
        clearHistory();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::bg);
        paintSpectrogram (g);
        paintAxis (g);
        paintProfile (g);
        paintHover (g);
    }

    void mouseMove (const juce::MouseEvent& e) override { setHover (e.position.y); }
    void mouseDrag (const juce::MouseEvent& e) override { setHover (e.position.y); }
    void mouseExit (const juce::MouseEvent&) override   { hoverY = -1.0f; repaint(); }

private:
    float freqToY (double f) const
    {
        return (float) spectroArea.getY() + (float) spectroArea.getHeight() * (float) (1.0 - axis.toNorm (f));
    }

    double yToFreq (float y) const
    {
        return axis.fromNorm (1.0 - (y - (float) spectroArea.getY()) / (float) spectroArea.getHeight());
    }

    float dbToX (float db) const
    {
        const float t = juce::jlimit (0.0f, 1.0f, (db - floorDb) / (ceilingDb - floorDb));
        return (float) profileArea.getX() + 1.0f + t * (float) (profileArea.getWidth() - 2);
    }

    void setHover (float y)
    {
        hoverY = juce::jlimit ((float) spectroArea.getY(), (float) spectroArea.getBottom() - 1.0f, y);
        repaint();
    }

    void rebuildRows()
    {
        const int h = spectroArea.getHeight();
        rowBin0.assign ((size_t) juce::jmax (0, h), 0);
        rowBin1.assign ((size_t) juce::jmax (0, h), 0);
        rowFrac.assign ((size_t) juce::jmax (0, h), 0.0f);
        if (h <= 0 || numBins <= 1) return;

        for (int y = 0; y < h; ++y)
        {
            const double fTop = axis.fromNorm (1.0 - (double) y / h);
            const double fBot = axis.fromNorm (1.0 - (double) (y + 1) / h);
            const double bTop = fTop / binHz, bBot = fBot / binHz;
            if (bTop - bBot >= 1.0)
            {
                rowBin0[(size_t) y] = juce::jlimit (0, numBins - 1, (int) std::ceil (bBot));
                rowBin1[(size_t) y] = juce::jlimit (0, numBins - 1, (int) std::floor (bTop));
            }
            else
            {
                const double c = juce::jlimit (0.0, (double) numBins - 1.0, 0.5 * (bTop + bBot));
                rowBin0[(size_t) y] = rowBin1[(size_t) y] = (int) c;
                rowFrac[(size_t) y] = (float) (c - (int) c);
            }
            rowBin1[(size_t) y] = juce::jmax (rowBin0[(size_t) y], rowBin1[(size_t) y]);
        }
    }

    std::vector<double> gridFreqs() const
    {
        std::vector<double> out;
        if (axis.mode == FreqAxis::logMode)
        {
            for (double f : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0, 40000.0 })
                if (f < axis.hi() * 0.985) out.push_back (f);
            return out;
        }
        const double range = axis.hi() - axis.lo();
        const double maxLabels = juce::jmax (2.0, spectroArea.getHeight() / 30.0);
        double step = 500.0;
        for (double s : { 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
        {
            step = s;
            if (range / s <= maxLabels) break;
        }
        for (double f = std::ceil (axis.lo() / step) * step; f < axis.hi() * 0.985; f += step)
            out.push_back (f);
        return out;
    }

    void paintSpectrogram (juce::Graphics& g)
    {
        const auto a = spectroArea;
        if (image.isValid())
        {
            const int w = image.getWidth(), h = image.getHeight();
            const int older = w - writeX;   // columnas más viejas: desde writeX hasta el final
            g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
            g.drawImage (image, a.getX(), a.getY(), older, h, writeX, 0, older, h);
            if (writeX > 0)
                g.drawImage (image, a.getX() + older, a.getY(), writeX, h, 0, 0, writeX, h);
        }

        // grilla
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        for (double f : gridFreqs())
            g.drawHorizontalLine (juce::roundToInt (freqToY (f)), (float) a.getX(), (float) a.getRight());

        // guías de los cortes típicos
        if (showGuides)
        {
            const float dashes[] = { 4.0f, 4.0f };
            g.setFont (Theme::font (11.0f, 500));
            for (double f : { 16000.0, 19000.0, 20000.0, 22000.0 })
            {
                if (f >= axis.hi() * 0.995 || f <= axis.lo()) continue;
                const float y = freqToY (f);
                g.setColour (juce::Colours::white.withAlpha (0.35f));
                g.drawDashedLine ({ (float) a.getX(), y, (float) a.getRight() - 34.0f, y }, dashes, 2, 1.0f);
                g.setColour (juce::Colours::white.withAlpha (0.75f));
                g.drawText (freqLabel (f), a.getRight() - 32, (int) y - 7, 30, 14, juce::Justification::centredRight);
            }
        }

        // borde detectado y líneas fijas
        paintEdgeLine (g, a);
        paintToneLines (g, a, true);

        // tiempo
        g.setColour (Theme::faint);
        g.setFont (Theme::font (11.0f));
        const double total = columnSeconds * a.getWidth();
        g.drawText (juce::String::fromUTF8 ("\xe2\x88\x92") + juce::String (total, total < 10 ? 1 : 0) + " s",
                    timeArea.getX(), timeArea.getY() + 2, 80, 14, juce::Justification::centredLeft);
        g.drawText ("ahora", timeArea.getRight() - 80, timeArea.getY() + 2, 80, 14, juce::Justification::centredRight);

        g.setColour (Theme::line);
        g.drawRect (a.expanded (1), 1);
    }

    void paintEdgeLine (juce::Graphics& g, juce::Rectangle<int> a)
    {
        using K = sc::CutoffResult::Kind;
        if (result.kind == K::waiting || result.edgeHz <= axis.lo() || result.edgeHz >= axis.hi() * 0.995)
            return;
        const auto c = colourFor (result.kind);
        const float y = freqToY (result.edgeHz);
        g.setColour (c.withAlpha (0.9f));
        g.drawHorizontalLine (juce::roundToInt (y), (float) a.getX(), (float) a.getRight());
    }

    // Líneas fijas: una línea fina sobre el espectrograma con su frecuencia a la
    // izquierda, y en el perfil un punto en el nivel de la línea.
    void paintToneLines (juce::Graphics& g, juce::Rectangle<int> a, bool onSpectrogram)
    {
        for (const auto& line : result.lines)
        {
            if (line.hz <= axis.lo() || line.hz >= axis.hi()) continue;
            const auto c = line.aboveCutoff ? Theme::codec : Theme::tone;
            const float y = freqToY (line.hz);

            if (onSpectrogram)
            {
                const float dots[] = { 2.0f, 3.0f };
                g.setColour (c.withAlpha (0.7f));
                g.drawDashedLine ({ (float) a.getX() + 58.0f, y, (float) a.getRight(), y }, dots, 2, 1.0f);

                juce::Path tri;   // marca en el borde izquierdo
                tri.addTriangle ((float) a.getX(), y - 4.0f, (float) a.getX(), y + 4.0f, (float) a.getX() + 6.0f, y);
                g.setColour (c);
                g.fillPath (tri);

                const auto label = line.hz < 1000.0f ? juce::String (juce::roundToInt (line.hz)) + " Hz"
                                                     : juce::String (line.hz / 1000.0f, 2) + "k";
                auto tag = juce::Rectangle<float> ((float) a.getX() + 8.0f, y - 8.0f, 48.0f, 16.0f);
                g.setColour (Theme::bg.withAlpha (0.8f));
                g.fillRoundedRectangle (tag, 3.0f);
                g.setColour (c);
                g.setFont (Theme::font (11.0f, 600));
                g.drawText (label, tag, juce::Justification::centred);
            }
            else
            {
                const float x = dbToX (line.levelDb);
                g.setColour (c.withAlpha (0.5f));
                g.drawHorizontalLine (juce::roundToInt (y), (float) a.getX(), (float) a.getRight());
                g.setColour (c);
                g.fillEllipse (x - 3.5f, y - 3.5f, 7.0f, 7.0f);
            }
        }
    }

    void paintAxis (juce::Graphics& g)
    {
        g.setFont (Theme::font (11.5f));
        g.setColour (Theme::dim);
        for (double f : gridFreqs())
        {
            const float y = freqToY (f);
            g.drawText (freqLabel (f), axisArea.getX(), (int) y - 7, axisArea.getWidth() - 8, 14, juce::Justification::centredRight);
        }
        g.setColour (Theme::faint);
        g.drawText (freqLabel (axis.hi()), axisArea.getX(), spectroArea.getY() - 1, axisArea.getWidth() - 8, 14, juce::Justification::centredRight);
    }

    void paintProfile (juce::Graphics& g)
    {
        const auto p = profileArea;
        g.setColour (Theme::bar);
        g.fillRect (p);

        // grilla de dB
        g.setFont (Theme::font (10.5f));
        for (float db = std::ceil (floorDb / 20.0f) * 20.0f; db <= ceilingDb; db += 20.0f)
        {
            const float x = dbToX (db);
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.drawVerticalLine (juce::roundToInt (x), (float) p.getY(), (float) p.getBottom());
            g.setColour (Theme::faint);
            if (x > p.getX() + 8 && x < p.getRight() - 8)
                g.drawText (juce::String (juce::roundToInt (db)), (int) x - 14, p.getBottom() + 3, 28, 13, juce::Justification::centred);
        }
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        for (double f : gridFreqs())
            g.drawHorizontalLine (juce::roundToInt (freqToY (f)), (float) p.getX(), (float) p.getRight());

        if (detector != nullptr && detector->getSeconds() > 0.0 && detector->getNumBins() > 1)
        {
            const auto& peaks = detector->peaks();
            const auto& mean  = detector->smoothedMean();
            const double hz = detector->getBinHz();

            auto curve = [&] (const std::vector<float>& data, bool closed)
            {
                juce::Path path;
                const int top = spectroArea.getY(), bottom = spectroArea.getBottom();
                bool started = false;
                for (int y = bottom - 1; y >= top; --y)
                {
                    const double f0 = yToFreq ((float) y + 1.0f), f1 = yToFreq ((float) y);
                    int b0 = juce::jlimit (0, (int) data.size() - 1, (int) std::floor (f0 / hz));
                    int b1 = juce::jlimit (0, (int) data.size() - 1, (int) std::ceil (f1 / hz));
                    float v = data[(size_t) b0];
                    for (int b = b0 + 1; b <= b1; ++b) v = juce::jmax (v, data[(size_t) b]);
                    const float x = dbToX (v);
                    if (! started) { path.startNewSubPath (closed ? (float) p.getX() : x, (float) y + 1.0f); if (closed) path.lineTo (x, (float) y + 1.0f); started = true; }
                    else path.lineTo (x, (float) y + 0.5f);
                }
                if (closed && started) { path.lineTo ((float) p.getX(), (float) top); path.closeSubPath(); }
                return path;
            };

            if (! mean.empty())
            {
                auto area = curve (mean, true);
                g.setGradientFill (juce::ColourGradient (Theme::accent.withAlpha (0.10f), (float) p.getX(), 0.0f,
                                                         Theme::accent.withAlpha (0.35f), (float) p.getRight(), 0.0f, false));
                g.fillPath (area);
                g.setColour (Theme::accent);
                g.strokePath (curve (mean, false), juce::PathStrokeType (1.3f));
            }
            g.setColour (Theme::dim.withAlpha (0.55f));
            g.strokePath (curve (peaks, false), juce::PathStrokeType (1.0f));
        }

        paintEdgeLine (g, p);
        paintToneLines (g, p, false);

        // leyenda (con fondo, para que se lea encima de las curvas)
        auto legend = juce::Rectangle<int> (p.getX() + 6, p.getY() + 6, juce::jmin (96, p.getWidth() - 12), 34);
        g.setColour (Theme::bar.withAlpha (0.88f));
        g.fillRoundedRectangle (legend.toFloat(), 4.0f);
        auto row = [&] (int y, juce::Colour c, const char* name)
        {
            g.setColour (c);
            g.fillRect (legend.getX() + 7, y + 6, 12, 2);
            g.setColour (Theme::dim);
            g.drawText (juce::String::fromUTF8 (name), legend.getX() + 25, y, legend.getWidth() - 28, 14, juce::Justification::centredLeft);
        };
        g.setFont (Theme::font (10.5f, 500));
        row (legend.getY() + 3, Theme::accent, "Promedio");
        row (legend.getY() + 17, Theme::dim.withAlpha (0.7f), "Picos");
        g.setColour (Theme::line);
        g.drawRect (p.expanded (1), 1);
    }

    void paintHover (juce::Graphics& g)
    {
        if (hoverY < 0.0f) return;
        const double f = yToFreq (hoverY);
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.drawHorizontalLine (juce::roundToInt (hoverY), (float) spectroArea.getX(), (float) profileArea.getRight());

        juce::String text = freqLabel (f, true);
        if (detector != nullptr && detector->getSeconds() > 0.0)
            text << "   " << juce::String (detector->levelAt (f), 1) << " dB prom.";

        g.setFont (Theme::font (12.0f, 500));
        const int w = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), text) + 16;
        auto tag = juce::Rectangle<int> (spectroArea.getX() + 8, juce::roundToInt (hoverY) - 22, w, 18);
        if (tag.getY() < spectroArea.getY()) tag.setY (juce::roundToInt (hoverY) + 4);
        g.setColour (Theme::bg.withAlpha (0.85f));
        g.fillRoundedRectangle (tag.toFloat(), 4.0f);
        g.setColour (Theme::text);
        g.drawText (text, tag, juce::Justification::centred);
    }

public:
    static juce::Colour colourFor (sc::CutoffResult::Kind k)
    {
        using K = sc::CutoffResult::Kind;
        switch (k)
        {
            case K::codec:
            case K::resampled: return Theme::codec;
            case K::fullBand:  return Theme::good;
            case K::gradual:   return Theme::accent;
            case K::waiting:   break;
        }
        return Theme::neutral;
    }

private:
    FreqAxis axis;
    juce::Rectangle<int> axisArea, spectroArea, profileArea, timeArea;
    juce::Image image;
    int writeX = 0;
    bool dirty = false, showGuides = true;
    int numBins = 0;
    double binHz = 1.0, columnSeconds = 0.01;
    float floorDb = -120.0f, hoverY = -1.0f;
    std::vector<int> rowBin0, rowBin1;
    std::vector<float> rowFrac;
    const sc::CutoffDetector* detector = nullptr;
    sc::CutoffResult result;
};
