#pragma once
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <cmath>
#include <memory>

// ---------------------------------------------------------------------------
//  Motor de análisis, sin nada de interfaz (así se puede probar solo).
//
//  SpectrumAnalyzer: toma muestras sueltas y cada "hop" calcula una FFT con
//  ventana Blackman-Harris. Entrega el nivel en dB de cada banda (bin),
//  calibrado para que un seno a 0 dBFS marque 0 dB.
//
//  CutoffDetector: promedia esos espectros mientras suena algo y busca hasta
//  qué frecuencia llega la señal y si la caída es abrupta (típico de MP3/AAC)
//  o gradual (típico de la grabación o la mezcla).
// ---------------------------------------------------------------------------
namespace sc
{
    constexpr float minDb = -160.0f;     // piso numérico: debajo de esto es "nada"

    // Textos con tildes y eñes: JUCE necesita que se los pasemos como UTF-8.
    inline juce::String txt (const char* s) { return juce::String::fromUTF8 (s); }

    inline float powerToDb (double p) { return (float) juce::jmax ((double) minDb, 10.0 * std::log10 (p + 1.0e-17)); }
    inline double dbToPower (float db) { return std::pow (10.0, db * 0.1); }

    //==========================================================================
    class SpectrumAnalyzer
    {
    public:
        void prepare (double newSampleRate, int newFftOrder, double hopSeconds)
        {
            sampleRate = newSampleRate;
            fftOrder   = newFftOrder;
            fftSize    = 1 << fftOrder;
            fft = std::make_unique<juce::dsp::FFT> (fftOrder);

            window.assign ((size_t) fftSize, 0.0f);
            juce::dsp::WindowingFunction<float>::fillWindowingTables (window.data(), (size_t) fftSize,
                juce::dsp::WindowingFunction<float>::blackmanHarris, false);
            double sum = 0.0;
            for (auto w : window) sum += w;
            ampScale = (float) (2.0 / sum);   // seno a 0 dBFS -> 0 dB en su bin

            ring.assign ((size_t) fftSize, 0.0f);
            work.assign ((size_t) fftSize * 2, 0.0f);
            levels.assign ((size_t) numBins(), minDb);
            writePos = 0;
            filled = 0;
            setHopSeconds (hopSeconds);
        }

        void setHopSeconds (double seconds)
        {
            hop = juce::jmax (16, (int) std::round (seconds * sampleRate));
            sinceLast = 0;
        }

        int    numBins() const     { return fftSize / 2 + 1; }
        int    getFftSize() const  { return fftSize; }
        double binHz() const       { return sampleRate / fftSize; }
        double getSampleRate() const { return sampleRate; }
        double frameSeconds() const { return hop / sampleRate; }

        // onFrame (const float* dB, int numBins, float frameRmsDb) se llama por cada FFT nueva.
        template <typename Fn>
        void push (const float* x, int n, Fn&& onFrame)
        {
            const int mask = fftSize - 1;
            for (int i = 0; i < n; ++i)
            {
                ring[(size_t) writePos] = x[i];
                writePos = (writePos + 1) & mask;
                if (filled < fftSize) ++filled;

                if (++sinceLast >= hop)
                {
                    sinceLast = 0;
                    if (filled == fftSize)
                    {
                        compute();
                        onFrame (levels.data(), numBins(), frameRmsDb);
                    }
                }
            }
        }

    private:
        void compute()
        {
            const int mask = fftSize - 1;
            double energy = 0.0;
            for (int i = 0; i < fftSize; ++i)
            {
                const float s = ring[(size_t) ((writePos + i) & mask)];
                energy += (double) s * s;
                work[(size_t) i] = s * window[(size_t) i];
            }
            std::fill (work.begin() + fftSize, work.end(), 0.0f);
            fft->performFrequencyOnlyForwardTransform (work.data(), true);

            for (int b = 0; b < numBins(); ++b)
            {
                const double a = (double) work[(size_t) b] * ampScale;
                levels[(size_t) b] = powerToDb (a * a);
            }
            frameRmsDb = powerToDb (energy / fftSize);
        }

        double sampleRate = 44100.0;
        int fftOrder = 13, fftSize = 8192, hop = 441, sinceLast = 0, writePos = 0, filled = 0;
        float ampScale = 1.0f, frameRmsDb = minDb;
        std::unique_ptr<juce::dsp::FFT> fft;
        std::vector<float> window, ring, work, levels;
    };

    //==========================================================================
    struct CutoffResult
    {
        enum class Kind { waiting, fullBand, codec, resampled, gradual };
        Kind kind = Kind::waiting;
        float edgeHz = 0.0f;      // hasta dónde llega la señal
        float dropDb = 0.0f;      // cuánto cae justo arriba del corte
        float seconds = 0.0f;     // segundos con señal medidos
        juce::String title, detail;
    };

    class CutoffDetector
    {
    public:
        static constexpr float gateDb = -65.0f;          // debajo de esto se considera silencio
        static constexpr double autoResetSilence = 2.0;   // segundos de silencio que reinician la medición
        static constexpr float abruptDropDb = 24.0f;      // caída mínima para llamarlo "corte abrupto"

        // Para decidir hasta dónde llega la señal no se usa el promedio sino el nivel que se
        // supera al menos el 20% del tiempo: así un clic suelto (play/stop, un salto en la
        // canción) no cuenta como "contenido", pero un platillo que aparece seguido sí.
        static constexpr double activeShare = 0.20;
        static constexpr int numBuckets = 64;             // histograma por banda, de a 2.5 dB
        static constexpr float bucketDb = 2.5f;

        void reset (int bins, double newBinHz, double newSampleRate)
        {
            numBins = bins;
            binHz = newBinHz;
            sampleRate = newSampleRate;
            sum.assign ((size_t) bins, 0.0);
            hist.assign ((size_t) bins * numBuckets, 0u);
            peak.assign ((size_t) bins, minDb);
            mean.assign ((size_t) bins, minDb);
            meanSmooth.assign ((size_t) bins, minDb);
            level.assign ((size_t) bins, minDb);
            smooth.assign ((size_t) bins, minDb);
            frames = 0;
            seconds = 0.0;
            silentSeconds = 0.0;
        }

        void addFrame (const float* db, int n, float frameRmsDb, double frameSeconds)
        {
            if (n != numBins) return;

            if (frameRmsDb < gateDb)
            {
                silentSeconds += frameSeconds;
                return;
            }

            // Después de un rato de silencio arranca una medición nueva (otra canción).
            if (silentSeconds >= autoResetSilence && frames > 0)
                reset (numBins, binHz, sampleRate);
            silentSeconds = 0.0;

            for (int b = 0; b < n; ++b)
            {
                sum[(size_t) b] += dbToPower (db[b]);
                peak[(size_t) b] = juce::jmax (peak[(size_t) b], db[b]);
                const int bucket = juce::jlimit (0, numBuckets - 1, (int) ((db[b] - minDb) / bucketDb));
                ++hist[(size_t) b * numBuckets + (size_t) bucket];
            }
            ++frames;
            seconds += frameSeconds;
        }

        double getSeconds() const { return seconds; }
        int    getNumBins() const { return numBins; }
        double getBinHz() const   { return binHz; }

        // Espectro promedio suavizado y picos máximos (en dB por bin) para dibujar.
        const std::vector<float>& smoothedMean() const { return meanSmooth; }
        const std::vector<float>& peaks() const        { return peak; }

        // Nivel promedio (suavizado) en una frecuencia, para la lectura del mouse.
        float levelAt (double hz) const
        {
            if (meanSmooth.empty() || frames == 0) return minDb;
            const double pos = juce::jlimit (0.0, (double) numBins - 1.001, hz / binHz);
            const int i = (int) pos;
            const float f = (float) (pos - i);
            return meanSmooth[(size_t) i] + f * (meanSmooth[(size_t) i + 1] - meanSmooth[(size_t) i]);
        }

        CutoffResult analyze()
        {
            CutoffResult r;
            r.seconds = (float) seconds;
            const double nyq = sampleRate * 0.5;

            if (frames == 0 || seconds < 1.0)
            {
                r.title  = txt ("Esperando señal");
                r.detail = txt ("Reproducí audio: hace falta al menos 1 segundo con sonido para medir.");
                return r;
            }

            // Promedio (para dibujar) y nivel "activo" (para medir), suavizados ±100 Hz.
            for (int b = 0; b < numBins; ++b)
            {
                mean[(size_t) b]  = powerToDb (sum[(size_t) b] / frames);
                level[(size_t) b] = activeLevel (b);
            }
            const int k = juce::jmax (1, (int) std::round (100.0 / binHz));
            smoothInto (mean, meanSmooth, k);
            smoothInto (level, smooth, k);

            const int bLo = binAt (200.0), bHi = numBins - 1;
            float ref = minDb;
            for (int b = bLo; b <= bHi; ++b) ref = juce::jmax (ref, smooth[(size_t) b]);

            if (ref < -90.0f)
            {
                r.title  = txt ("Señal muy baja");
                r.detail = txt ("Subí el volumen de lo que estás midiendo.");
                return r;
            }

            // La señal "llega" hasta el último punto que está a menos de 70 dB del máximo.
            const float threshold = juce::jmax (ref - 70.0f, -130.0f);
            int edgeBin = binAt (1000.0);
            for (int b = numBins - 1; b > edgeBin; --b)
                if (smooth[(size_t) b] > threshold) { edgeBin = b; break; }

            // Afinar sin suavizar (el suavizado corre el borde ~100 Hz para arriba).
            for (int b = edgeBin, stop = juce::jmax (0, edgeBin - 2 * k); b >= stop; --b)
                if (level[(size_t) b] > threshold) { edgeBin = b; break; }

            const double edgeHz = (edgeBin + 0.5) * binHz;
            r.edgeHz = (float) edgeHz;

            const float below = averageDb (edgeHz - 700.0, edgeHz - 200.0);
            const double aboveLo = edgeHz + 200.0, aboveHi = juce::jmin (edgeHz + 700.0, nyq);
            const bool hasAbove = (aboveHi - aboveLo) > 2.0 * binHz;
            const float above = hasAbove ? juce::jmax (averageDb (aboveLo, aboveHi), -150.0f) : below;
            r.dropDb = juce::jmax (0.0f, below - above);
            const bool abrupt = r.dropDb >= abruptDropDb;

            const auto kHz = [] (double hz) { return juce::String (hz / 1000.0, 1) + " kHz"; };
            const bool reachesTop = ! hasAbove || edgeHz >= nyq - juce::jmax (600.0, 0.03 * nyq);

            if (reachesTop)
            {
                r.kind   = CutoffResult::Kind::fullBand;
                r.title  = "Sin corte: llega hasta " + kHz (edgeHz);
                r.detail = txt ("Usa toda la banda disponible. No se ve el corte típico de MP3/AAC.");
            }
            else if (abrupt && nyq > 26000.0 && edgeHz >= 19000.0 && edgeHz <= 24500.0)
            {
                r.kind   = CutoffResult::Kind::resampled;
                r.title  = "Corte abrupto en " + kHz (edgeHz) + " (archivo a " + kHz (sampleRate) + ")";
                r.detail = txt ("Alta resolución \"inflada\": probablemente viene de una fuente a 44.1/48 kHz o de un archivo con pérdida.");
            }
            else if (abrupt && edgeHz >= 20900.0)
            {
                r.kind   = CutoffResult::Kind::fullBand;
                r.title  = "Corte en " + kHz (edgeHz);
                r.detail = txt ("Está muy arriba: suele ser el filtro de la conversión o del remuestreo, no un códec.");
            }
            else if (abrupt)
            {
                r.kind   = CutoffResult::Kind::codec;
                r.title  = "Corte abrupto en " + kHz (edgeHz);
                r.detail = codecHint (edgeHz) + " (orientativo: cada codificador corta en un lugar distinto).";
            }
            else
            {
                r.kind   = CutoffResult::Kind::gradual;
                r.title  = "Se apaga de a poco cerca de " + kHz (edgeHz);
                r.detail = txt ("Caída suave: parece la grabación o la mezcla, no un códec. Probá en una parte con más brillo.");
            }
            return r;
        }

        static juce::String codecHint (double hz)
        {
            if (hz < 11500.0) return "Calidad muy baja (64 kbps o menos)";
            if (hz < 15500.0) return txt ("Típico de MP3/AAC de 96 kbps o menos");
            if (hz < 17300.0) return txt ("Típico de MP3 de 128 kbps");
            if (hz < 18300.0) return txt ("Típico de MP3 de 160 kbps");
            if (hz < 19300.0) return txt ("Típico de MP3 de 192 kbps");
            if (hz < 20200.0) return txt ("Típico de MP3 de 256 kbps, AAC u Ogg de alta calidad");
            return txt ("Típico de MP3 de 320 kbps");
        }

    private:
        // Nivel que esta banda supera durante al menos el 20% del tiempo medido.
        float activeLevel (int b) const
        {
            const auto* h = hist.data() + (size_t) b * numBuckets;
            const double target = frames * activeShare;
            double above = 0.0;
            for (int i = numBuckets - 1; i >= 0; --i)
            {
                if (above + h[i] >= target)
                {
                    const double frac = h[i] > 0 ? (target - above) / h[i] : 0.0;   // dónde cae dentro del casillero
                    return minDb + bucketDb * (float) (i + 1.0 - frac);
                }
                above += h[i];
            }
            return minDb;
        }

        void smoothInto (const std::vector<float>& src, std::vector<float>& dst, int k) const
        {
            std::vector<double> pw ((size_t) numBins + 1, 0.0);   // suma acumulada de potencia
            for (int b = 0; b < numBins; ++b)
                pw[(size_t) b + 1] = pw[(size_t) b] + dbToPower (src[(size_t) b]);
            for (int b = 0; b < numBins; ++b)
            {
                const int a = juce::jmax (0, b - k), z = juce::jmin (numBins - 1, b + k);
                dst[(size_t) b] = powerToDb ((pw[(size_t) z + 1] - pw[(size_t) a]) / (z - a + 1));
            }
        }

        int binAt (double hz) const { return juce::jlimit (0, numBins - 1, (int) std::round (hz / binHz)); }

        float averageDb (double fromHz, double toHz) const
        {
            const int a = binAt (fromHz), z = binAt (toHz);
            if (z < a) return minDb;
            double p = 0.0;
            for (int b = a; b <= z; ++b) p += dbToPower (smooth[(size_t) b]);
            return powerToDb (p / (z - a + 1));
        }

        int numBins = 0;
        double binHz = 1.0, sampleRate = 44100.0;
        std::vector<double> sum;
        std::vector<juce::uint32> hist;
        std::vector<float> peak, mean, meanSmooth, level, smooth;
        long frames = 0;
        double seconds = 0.0, silentSeconds = 0.0;
    };
}
