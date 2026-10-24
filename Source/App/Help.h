#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

/** The colour Help mode outlines controls with, and its button turns when on. */
inline const juce::Colour helpColour { 0xff4fd6ff };

/** What a control does, in a call-out bubble: a bold title over a paragraph. */
class HelpBubble : public juce::Component
{
public:
    HelpBubble (const juce::String& title, const juce::String& body)
    {
        text.append (title + "\n", juce::FontOptions (16.0f, juce::Font::bold), juce::Colours::white);
        text.append (body, juce::FontOptions (14.0f), juce::Colours::white.withAlpha (0.85f));
        text.setLineSpacing (3.0f);
        layout.createLayout (text, (float) width);
        setSize (width, (int) std::ceil (layout.getHeight()));
    }
    void paint (juce::Graphics& g) override { layout.draw (g, getLocalBounds().toFloat()); }

private:
    static constexpr int width = 360;
    juce::AttributedString text;
    juce::TextLayout layout;
};

/** Lies over the window while Help is on: outlines what can be explained and takes the clicks. */
class HelpOverlay : public juce::Component
{
public:
    std::function<void (juce::Point<int>)> onClick;
    std::vector<juce::Rectangle<int>> spots;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.12f));
        g.setColour (helpColour);
        for (const auto& r : spots)
            g.drawRect (r.expanded (2), 2);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (onClick)
            onClick (e.getPosition());
    }
};
