#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <juce_graphics/juce_graphics.h>

namespace syn
{
/** One thing on a library's front panel: a knob, the title, a line of text or the articulation menu. */
struct PanelControl
{
    enum Kind { volume, release, tone, chorus, delay, reverb, room, title, text, articulations };
    Kind kind = volume;
    juce::String label;   // a knob's name, or a text's words; the title shows the library name when empty
    juce::Rectangle<int> bounds;
    int textSize = 16;    // titles and texts

    bool isKnob() const { return kind <= room; }
    bool isEffect() const { return kind >= tone && kind <= room; }
    static juce::String nameOf (Kind);   // "Volume", "Title"...

    juce::ValueTree toValueTree() const;
    static PanelControl fromValueTree (const juce::ValueTree&);
};

/** The Decent Sampler front panel: its colours and where everything sits. With no controls of
    its own it uses the standard layout: the title on top, the knobs in a row along the bottom. */
struct Panel
{
    static constexpr int width = 812, height = 375;   // Decent Sampler's recommended panel size

    juce::Colour background { 0xFF22282E };   // under the picture, or the whole panel without one
    juce::Colour textColour { 0xFFFFFFFF };
    juce::Colour knobColour { 0xFFE8A33D };   // a knob's filled arc
    std::vector<PanelControl> controls;       // empty: the standard layout

    /** The controls written: these, or the standard layout for a library with or without effect
        knobs and an articulation menu. */
    std::vector<PanelControl> layout (bool effects, bool articulationMenu) const;
    static std::vector<PanelControl> standardLayout (bool effects, bool articulationMenu);
    /** Where the standard layout puts a control. */
    static PanelControl standard (PanelControl::Kind);

    juce::ValueTree toValueTree() const;
    static Panel fromValueTree (const juce::ValueTree&);
};
} // namespace syn
