#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "BinaryData.h"

// ---------------------------------------------------------------------------
//  Estilo visual: todo oscuro (como un analizador de estudio), azul como
//  color principal, naranja para "hay corte de códec", verde para "banda
//  completa". Letra Inter incluida en el plugin.
// ---------------------------------------------------------------------------
namespace Theme
{
    const juce::Colour bg        { 0xff0c0e11 };   // fondo general
    const juce::Colour bar       { 0xff14171c };   // barras de arriba y abajo
    const juce::Colour control   { 0xff1d2128 };   // menús y botones
    const juce::Colour controlHi { 0xff262b33 };   // botón con el mouse encima
    const juce::Colour line      { 0xff2a2f37 };   // bordes y grilla
    const juce::Colour text      { 0xffdadde2 };
    const juce::Colour dim       { 0xff8a9099 };
    const juce::Colour faint     { 0xff565c66 };
    const juce::Colour accent    { 0xff5aa9ff };   // azul
    const juce::Colour codec     { 0xffff9a3c };   // naranja: corte de MP3/AAC
    const juce::Colour good      { 0xff4fd18b };   // verde: banda completa
    const juce::Colour neutral   { 0xff9aa3ad };   // gris: esperando / gradual
    const juce::Colour tone      { 0xff3fd5c8 };   // turquesa: líneas fijas

    inline juce::Typeface::Ptr typeface (int weight)
    {
        static juce::Typeface::Ptr regular  = juce::Typeface::createSystemTypefaceFor (BinaryData::InterRegular_otf,  BinaryData::InterRegular_otfSize);
        static juce::Typeface::Ptr medium   = juce::Typeface::createSystemTypefaceFor (BinaryData::InterMedium_otf,   BinaryData::InterMedium_otfSize);
        static juce::Typeface::Ptr semibold = juce::Typeface::createSystemTypefaceFor (BinaryData::InterSemiBold_otf, BinaryData::InterSemiBold_otfSize);
        if (weight >= 600) return semibold;
        if (weight >= 500) return medium;
        return regular;
    }

    inline juce::Font font (float height, int weight = 400, float kerning = 0.0f)
    {
        return juce::Font (juce::FontOptions (typeface (weight)).withHeight (height).withKerningFactor (kerning));
    }

    // Colores del espectrograma: negro -> azul profundo -> violeta -> rojo -> naranja -> amarillo -> blanco.
    inline const std::array<juce::PixelARGB, 256>& palette()
    {
        static const auto table = []
        {
            const std::pair<float, juce::uint32> stops[] = {
                { 0.00f, 0xff000000 }, { 0.16f, 0xff0a0b3a }, { 0.36f, 0xff4b0f8c },
                { 0.55f, 0xffb8226b }, { 0.72f, 0xfff0592a }, { 0.88f, 0xfffbd04a }, { 1.00f, 0xffffffff } };
            std::array<juce::PixelARGB, 256> t {};
            for (int i = 0; i < 256; ++i)
            {
                const float x = i / 255.0f;
                int s = 0;
                while (s < 5 && x > stops[s + 1].first) ++s;
                const float f = (x - stops[s].first) / (stops[s + 1].first - stops[s].first);
                const auto c = juce::Colour (stops[s].second).interpolatedWith (juce::Colour (stops[s + 1].second), f);
                t[(size_t) i] = c.getPixelARGB();
            }
            return t;
        }();
        return table;
    }

    // Menús y botones planos y oscuros.
    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel()
        {
            setColour (juce::ComboBox::backgroundColourId, control);
            setColour (juce::ComboBox::textColourId, text);
            setColour (juce::ComboBox::outlineColourId, line);
            setColour (juce::ComboBox::arrowColourId, dim);
            setColour (juce::PopupMenu::backgroundColourId, control);
            setColour (juce::PopupMenu::textColourId, text);
            setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.25f));
            setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
            setColour (juce::TextButton::buttonColourId, control);
            setColour (juce::TextButton::buttonOnColourId, accent.withAlpha (0.22f));
            setColour (juce::TextButton::textColourOffId, text);
            setColour (juce::TextButton::textColourOnId, accent);
            setColour (juce::TooltipWindow::backgroundColourId, control);
            setColour (juce::TooltipWindow::textColourId, text);
            setColour (juce::TooltipWindow::outlineColourId, line);
        }

        juce::Font getComboBoxFont (juce::ComboBox&) override        { return font (13.0f, 500); }
        juce::Font getPopupMenuFont() override                       { return font (13.5f); }
        juce::Font getTextButtonFont (juce::TextButton&, int) override { return font (13.0f, 500); }

        void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
        {
            auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
            g.setColour (box.isMouseOver (true) ? controlHi : control);
            g.fillRoundedRectangle (r, 5.0f);
            g.setColour (box.hasKeyboardFocus (true) ? accent.withAlpha (0.7f) : line);
            g.drawRoundedRectangle (r, 5.0f, 1.0f);

            juce::Path arrow;
            const float cx = (float) w - 12.0f, cy = (float) h * 0.5f;
            arrow.startNewSubPath (cx - 3.5f, cy - 1.5f);
            arrow.lineTo (cx, cy + 2.0f);
            arrow.lineTo (cx + 3.5f, cy - 1.5f);
            g.setColour (dim);
            g.strokePath (arrow, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
        {
            label.setBounds (2, 0, box.getWidth() - 20, box.getHeight());
            label.setFont (getComboBoxFont (box));
        }

        void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
        {
            auto r = b.getLocalBounds().toFloat().reduced (0.5f);
            auto c = b.getToggleState() ? accent.withAlpha (0.18f) : (over ? controlHi : control);
            if (down) c = c.brighter (0.1f);
            g.setColour (c);
            g.fillRoundedRectangle (r, 5.0f);
            g.setColour (b.getToggleState() ? accent.withAlpha (0.6f) : (b.hasKeyboardFocus (false) ? accent.withAlpha (0.7f) : line));
            g.drawRoundedRectangle (r, 5.0f, 1.0f);
        }
    };
}
