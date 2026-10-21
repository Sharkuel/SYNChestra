#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

/** The logo's colours, black, cyan and white, with square corners on buttons and drop-down lists. */
class SquareLook : public juce::LookAndFeel_V4
{
public:
    static inline const juce::Colour cyan { 0xff4fd6ff };   // as in the logo's "S" and "estra"

    SquareLook()
        : LookAndFeel_V4 ({ 0xff0b0d10,     // window background
                            0xff171b20,     // buttons, text boxes, lists
                            0xff14181c,     // menus
                            0xff2e3b44,     // outlines
                            0xfff2f5f7,     // text
                            cyan,           // sliders and ticks
                            0xff000000,     // text on the highlight
                            cyan,           // highlight
                            0xfff2f5f7 })   // menu text
    {
        setColour (juce::TextEditor::highlightColourId, cyan.withAlpha (0.3f));
        setColour (juce::TextEditor::focusedOutlineColourId, cyan);
        setColour (juce::ToggleButton::tickColourId, cyan);
        setColour (juce::ComboBox::arrowColourId, cyan);
        setColour (juce::TableHeaderComponent::backgroundColourId, juce::Colour (0xff1d2329));
        setColour (juce::TableHeaderComponent::textColourId, cyan);
        setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff12161a));
        setColour (juce::TextButton::buttonOnColourId, cyan.withAlpha (0.45f));
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                               bool highlighted, bool down) override
    {
        const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        auto colour = backgroundColour.withMultipliedSaturation (button.hasKeyboardFocus (true) ? 1.3f : 0.9f)
                          .withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f);
        if (down || highlighted)
            colour = colour.contrasting (down ? 0.2f : 0.05f);
        g.setColour (colour);
        g.fillRect (bounds);
        g.setColour (button.findColour (juce::ComboBox::outlineColourId));
        g.drawRect (bounds, 1.0f);
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
    {
        const juce::Rectangle<int> bounds (0, 0, width, height);
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRect (bounds);
        g.setColour (box.findColour (juce::ComboBox::outlineColourId));
        g.drawRect (bounds.toFloat().reduced (0.5f), 1.0f);

        const juce::Rectangle<int> arrowZone (width - 30, 0, 20, height);
        juce::Path path;
        path.startNewSubPath ((float) arrowZone.getX() + 3.0f, (float) arrowZone.getCentreY() - 2.0f);
        path.lineTo ((float) arrowZone.getCentreX(), (float) arrowZone.getCentreY() + 3.0f);
        path.lineTo ((float) arrowZone.getRight() - 3.0f, (float) arrowZone.getCentreY() - 2.0f);
        g.setColour (box.findColour (juce::ComboBox::arrowColourId).withAlpha (box.isEnabled() ? 0.9f : 0.2f));
        g.strokePath (path, juce::PathStrokeType (2.0f));
    }
};
