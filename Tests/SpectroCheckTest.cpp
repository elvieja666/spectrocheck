// ---------------------------------------------------------------------------
//  Pruebas automáticas del detector de corte. GitHub las corre antes de
//  publicar: si alguna falla, no sale una versión nueva.
// ---------------------------------------------------------------------------
#include "Analyzer.h"
#include <cstdio>
#include <random>

using Kind = sc::CutoffResult::Kind;

static const char* kindName (Kind k)
{
    switch (k)
    {
        case Kind::waiting:   return "esperando";
        case Kind::fullBand:  return "banda completa";
        case Kind::codec:     return "corte de códec";
        case Kind::resampled: return "remuestreado";
        case Kind::gradual:   return "gradual";
    }
    return "?";
}

// Música falsa: muchos senos con fase al azar y espectro tipo "rosa" hasta maxHz,
// más ruido de dither a -96 dBFS por encima de todo (como un WAV de 16 bits).
static std::vector<float> fakeMusic (double sr, double maxHz, double seconds, bool dither, unsigned seed, double minHz = 40.0)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<double> uni (0.0, 1.0);
    const int n = (int) (sr * seconds);
    std::vector<double> out ((size_t) n, 0.0);

    const int numSines = 700;
    for (int s = 0; s < numSines; ++s)
    {
        const double f = minHz + uni (rng) * (maxHz - minHz);
        const double amp = 0.012 * std::sqrt (1000.0 / juce::jmax (f, 200.0));
        const double inc = juce::MathConstants<double>::twoPi * f / sr;
        // oscilador por rotación (más rápido que sin() en cada muestra)
        double c = std::cos (inc), si = std::sin (inc);
        double ph = uni (rng) * juce::MathConstants<double>::twoPi;
        double x = std::cos (ph), y = std::sin (ph);
        for (int i = 0; i < n; ++i)
        {
            out[(size_t) i] += amp * y;
            const double nx = x * c - y * si;
            y = x * si + y * c;
            x = nx;
        }
    }

    std::vector<float> f32 ((size_t) n);
    std::uniform_real_distribution<double> d (-1.0, 1.0);
    for (int i = 0; i < n; ++i)
        f32[(size_t) i] = (float) (out[(size_t) i] + (dither ? (d (rng) + d (rng)) * 1.5e-5 : 0.0));
    return f32;
}

// Ruido blanco filtrado con 8 pasabajos de 1 polo (cae 48 dB por octava, sin "pared").
static std::vector<float> darkNoise (double sr, double cornerHz, double seconds)
{
    std::mt19937 rng (7);
    std::normal_distribution<double> g (0.0, 0.2);
    const int n = (int) (sr * seconds);
    std::vector<float> out ((size_t) n);
    const double a = std::exp (-juce::MathConstants<double>::twoPi * cornerHz / sr);
    double st[8] = {};
    for (int i = 0; i < n; ++i)
    {
        double v = g (rng);
        for (auto& s : st) { s = (1.0 - a) * v + a * s; v = s; }
        out[(size_t) i] = (float) (v * 4.0);
    }
    return out;
}

// MP3 con corte en 16 kHz, pero que arranca y para de golpe cada 3 segundos
// (como cuando le das play y stop en Logic): cada arranque es un clic con agudos.
static std::vector<float> withClicks (double sr)
{
    std::vector<float> out;
    for (int i = 0; i < 4; ++i)
    {
        auto part = fakeMusic (sr, 16000, 3.0, false, 20u + (unsigned) i);
        for (auto& v : part) v *= 2.5f;          // fuerte, para que el clic sea bien marcado
        out.insert (out.end(), part.begin(), part.end());
        out.insert (out.end(), (size_t) (sr * 0.5), 0.0f);
    }
    return out;
}

// WAV de banda completa donde lo que pasa de 12 kHz (platillos) aparece solo el 35% del tiempo.
static std::vector<float> sparseAir (double sr)
{
    auto body = fakeMusic (sr, 12000, 6.0, true, 30);
    auto air  = fakeMusic (sr, 22000, 6.0, false, 31, 12000);
    const int chunk = (int) (sr * 0.1);
    for (size_t i = 0; i < body.size(); ++i)
    {
        const bool on = ((i / (size_t) chunk) % 20) < 7;   // 7 de cada 20 pedazos de 100 ms
        body[i] += on ? air[i] * 0.5f : 0.0f;
    }
    return body;
}

static sc::CutoffResult measure (const std::vector<float>& x, double sr, int fftOrder)
{
    sc::SpectrumAnalyzer an;
    an.prepare (sr, fftOrder, 0.01);
    sc::CutoffDetector det;
    det.reset (an.numBins(), an.binHz(), sr);
    an.push (x.data(), (int) x.size(), [&] (const float* db, int n, float rms)
    {
        det.addFrame (db, n, rms, an.frameSeconds());
    });
    return det.analyze();
}

static int failures = 0;

static void check (const char* name, const sc::CutoffResult& r, bool ok)
{
    std::printf ("%s %-34s -> %-15s borde %6.0f Hz  caída %5.1f dB  | %s\n",
                 ok ? "OK   " : "FALLA", name, kindName (r.kind), r.edgeHz, r.dropDb, r.title.toRawUTF8());
    if (! ok) ++failures;
}

int main()
{
    for (int order : { 12, 13, 14 })
    {
        std::printf ("\n--- FFT %d ---\n", 1 << order);

        auto r1 = measure (fakeMusic (44100, 16000, 6, true, 1), 44100, order);
        check ("MP3 128 (corte 16 kHz, 44.1)", r1, r1.kind == Kind::codec && r1.edgeHz > 15700 && r1.edgeHz < 16400);

        auto r2 = measure (fakeMusic (44100, 19500, 6, true, 2), 44100, order);
        check ("MP3 256 (corte 19.5 kHz, 44.1)", r2, r2.kind == Kind::codec && r2.edgeHz > 19200 && r2.edgeHz < 19900);

        auto r3 = measure (fakeMusic (44100, 22000, 6, true, 3), 44100, order);
        check ("WAV banda completa (44.1)", r3, r3.kind == Kind::fullBand);

        auto r4 = measure (fakeMusic (48000, 16000, 6, false, 4), 48000, order);
        check ("Corte 16 kHz sin dither (48)", r4, r4.kind == Kind::codec && r4.edgeHz > 15700 && r4.edgeHz < 16400);

        auto r5 = measure (darkNoise (44100, 2000, 6), 44100, order);
        check ("Grabación oscura (caída suave)", r5, r5.kind == Kind::gradual || r5.kind == Kind::fullBand);

        auto r6 = measure (fakeMusic (96000, 22000, 4, true, 5), 96000, order);
        check ("96 kHz hecho desde 44.1", r6, r6.kind == Kind::resampled);

        auto r8 = measure (withClicks (44100), 44100, order);
        check ("Corte 16 kHz con clics de play/stop", r8, r8.kind == Kind::codec && r8.edgeHz > 15700 && r8.edgeHz < 16400);

        auto r9 = measure (sparseAir (44100), 44100, order);
        check ("Banda completa, agudos de a ratos", r9, r9.kind == Kind::fullBand);

        std::vector<float> silence (44100 * 3, 0.0f);
        auto r7 = measure (silence, 44100, order);
        check ("Silencio", r7, r7.kind == Kind::waiting);
    }

    std::printf ("\n%s\n", failures == 0 ? "Todas las pruebas OK" : "HAY PRUEBAS QUE FALLARON");
    return failures == 0 ? 0 : 1;
}
