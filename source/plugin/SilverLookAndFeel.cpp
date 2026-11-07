#include "SilverLookAndFeel.h"

namespace stemsep
{

SilverLookAndFeel::SilverLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, Silver::bgTop);
    setColour (juce::Label::textColourId, Silver::navy);
    setColour (juce::TextButton::textColourOffId, Silver::navy);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::TextButton::buttonColourId, Silver::metalLight);
    setColour (juce::TextButton::buttonOnColourId, Silver::navy);
    setColour (juce::ComboBox::textColourId, Silver::navy);
    setColour (juce::ComboBox::backgroundColourId, Silver::metalLight);
    setColour (juce::ComboBox::outlineColourId, Silver::panelEdge);
    setColour (juce::ComboBox::arrowColourId, Silver::navy);
    setColour (juce::PopupMenu::backgroundColourId, Silver::panel);
    setColour (juce::PopupMenu::textColourId, Silver::navy);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Silver::navy);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::Slider::textBoxTextColourId, Silver::navy);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::AlertWindow::backgroundColourId, Silver::panel);
    setColour (juce::AlertWindow::textColourId, Silver::navy);
    setColour (juce::AlertWindow::outlineColourId, Silver::panelEdge);
    setColour (juce::TooltipWindow::backgroundColourId, Silver::navy);
    setColour (juce::TooltipWindow::textColourId, juce::Colours::white);
}

juce::Font SilverLookAndFeel::uiFont (float height, bool bold)
{
   #if JUCE_WINDOWS
    const juce::String face ("Segoe UI");
   #else
    const juce::String face (juce::Font::getDefaultSansSerifFontName());
   #endif
    return juce::Font (juce::FontOptions (face, height, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font SilverLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return uiFont (juce::jmin (15.0f, (float) buttonHeight * 0.55f), true);
}

juce::Font SilverLookAndFeel::getComboBoxFont (juce::ComboBox&) { return uiFont (14.0f); }
juce::Font SilverLookAndFeel::getLabelFont (juce::Label& l) { return uiFont (juce::jmax (11.0f, (float) l.getHeight() * 0.62f)); }
juce::Font SilverLookAndFeel::getPopupMenuFont() { return uiFont (15.0f); }

void SilverLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                              bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    juce::Colour top = on ? Silver::navySoft : Silver::metalLight;
    juce::Colour bottom = on ? Silver::navy : Silver::metalDark;
    if (b.getProperties().contains ("accent") && on)
    {
        const auto acc = juce::Colour ((juce::uint32) (juce::int64) b.getProperties()["accent"]);
        top = acc.brighter (0.25f);
        bottom = acc.darker (0.15f);
    }
    if (over)  { top = top.brighter (0.06f); bottom = bottom.brighter (0.06f); }
    if (down)  std::swap (top, bottom);
    if (! b.isEnabled()) { top = top.withAlpha (0.5f); bottom = bottom.withAlpha (0.5f); }

    g.setGradientFill (juce::ColourGradient (top, r.getX(), r.getY(), bottom, r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (on ? Silver::navy : Silver::panelEdge);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
}

void SilverLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool down)
{
    drawButtonBackground (g, b, {}, over, down);
    g.setColour (b.getToggleState() ? juce::Colours::white : Silver::navy);
    g.setFont (uiFont ((float) b.getHeight() * 0.55f, true));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
}

void SilverLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                      int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) width, (float) height).reduced (0.5f);
    g.setGradientFill (juce::ColourGradient (Silver::metalLight, 0, 0, Silver::metalDark, 0, (float) height, false));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (box.hasKeyboardFocus (false) ? Silver::navySoft : Silver::panelEdge);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);

    juce::Path arrow;
    const float ax = (float) width - 16.0f, ay = (float) height * 0.5f;
    arrow.addTriangle (ax - 5.0f, ay - 3.0f, ax + 5.0f, ay - 3.0f, ax, ay + 4.0f);
    g.setColour (Silver::navy);
    g.fillPath (arrow);
}

void SilverLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float, float, juce::Slider::SliderStyle style,
                                          juce::Slider& s)
{
    if (style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, 0, 0, style, s);
        return;
    }
    const auto accent = s.findColour (juce::Slider::trackColourId);
    const float cx = (float) x + (float) width * 0.5f;
    auto track = juce::Rectangle<float> (cx - 3.0f, (float) y, 6.0f, (float) height);
    g.setColour (Silver::panelEdge.withAlpha (0.6f));
    g.fillRoundedRectangle (track, 3.0f);
    auto filled = track.withTop (sliderPos);
    g.setColour (accent.withAlpha (0.85f));
    g.fillRoundedRectangle (filled, 3.0f);

    // marca de 0 dB
    const auto zeroY = (float) s.getPositionOfValue (0.0);
    g.setColour (Silver::navy.withAlpha (0.45f));
    g.drawLine (cx - 12.0f, zeroY, cx + 12.0f, zeroY, 1.0f);

    // botao do fader (metalico)
    auto thumb = juce::Rectangle<float> (cx - 15.0f, sliderPos - 8.0f, 30.0f, 16.0f);
    g.setGradientFill (juce::ColourGradient (Silver::metalLight, 0, thumb.getY(), Silver::metalDark, 0, thumb.getBottom(), false));
    g.fillRoundedRectangle (thumb, 4.0f);
    g.setColour (Silver::navy.withAlpha (0.8f));
    g.drawRoundedRectangle (thumb, 4.0f, 1.0f);
    g.setColour (accent);
    g.fillRect (thumb.getX() + 5.0f, thumb.getCentreY() - 1.0f, thumb.getWidth() - 10.0f, 2.0f);
}

} // namespace stemsep
