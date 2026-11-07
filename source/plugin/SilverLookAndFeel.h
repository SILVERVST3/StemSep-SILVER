#pragma once
// Visual prateado da linha SILVER (texto azul-marinho).

#include <JuceHeader.h>

#define U8(text) juce::String::fromUTF8 (text)

namespace stemsep
{

namespace Silver
{
    const juce::Colour bgTop       { 0xffeef0f3 };
    const juce::Colour bgBottom    { 0xffc6ccd3 };
    const juce::Colour panel       { 0xfff6f7f9 };
    const juce::Colour panelEdge   { 0xffa9b0b8 };
    const juce::Colour navy        { 0xff0b2545 };
    const juce::Colour navySoft    { 0xff3a5276 };
    const juce::Colour metalLight  { 0xfffbfcfd };
    const juce::Colour metalDark   { 0xffcfd5dc };
    const juce::Colour good        { 0xff1e7d46 };
    const juce::Colour warn        { 0xffb9770e };
    const juce::Colour bad         { 0xffa93226 };

    // cores dos stems (indice do modelo: bateria, baixo, instrumentos, vocal)
    inline juce::Colour stemColour (int stem)
    {
        switch (stem)
        {
            case 0:  return juce::Colour (0xffb03a2e);   // Bateria
            case 1:  return juce::Colour (0xff1f5f99);   // Baixo
            case 3:  return juce::Colour (0xff7d3c98);   // Vocal
            default: return juce::Colour (0xff1e8449);   // Instrumentos
        }
    }
}

class SilverLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SilverLookAndFeel();

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getPopupMenuFont() override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isMouseOverButton, bool isButtonDown) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    static juce::Font uiFont (float height, bool bold = false);
};

} // namespace stemsep
