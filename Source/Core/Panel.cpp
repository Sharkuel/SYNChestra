#include "Panel.h"

namespace syn
{
juce::String PanelControl::nameOf (Kind kind)
{
    static const char* names[] = { "Volume", "Release", "Tone", "Chorus", "Delay", "Reverb", "Room",
                                   "Attack", "Pan", "Tune", "Title", "Text", "Articulations" };
    return names[(int) kind];
}

juce::String PanelControl::nameOf (Style style)
{
    static const char* names[] = { "Knob", "Vertical fader", "Horizontal fader" };
    return names[(int) style];
}

juce::String PanelControl::decentSamplerStyle() const
{
    if (! isKnob())
        return {};
    return style == verticalFader ? "linear_vertical" : style == horizontalFader ? "linear_horizontal" : juce::String();
}

void PanelControl::setStyle (Style newStyle)
{
    style = newStyle;
    const auto centre = bounds.getCentre();
    // Room for the label above the track, as Decent Sampler draws it.
    bounds.setSize (style == verticalFader ? 60 : style == horizontalFader ? 200 : 90,
                    style == verticalFader ? 180 : style == horizontalFader ? 60 : 90);
    bounds.setCentre (centre);
}

juce::ValueTree PanelControl::toValueTree() const
{
    juce::ValueTree t ("Control");
    t.setProperty ("kind", nameOf (kind), nullptr);
    t.setProperty ("label", label, nullptr);
    t.setProperty ("style", nameOf (style), nullptr);
    t.setProperty ("bounds", bounds.toString(), nullptr);
    t.setProperty ("textSize", textSize, nullptr);
    return t;
}

PanelControl PanelControl::fromValueTree (const juce::ValueTree& t)
{
    PanelControl c;
    const auto kind = t.getProperty ("kind").toString();
    for (int k = volume; k <= articulations; ++k)
        if (nameOf ((Kind) k) == kind)
            c.kind = (Kind) k;
    c.label = t.getProperty ("label", c.label);
    const auto style = t.getProperty ("style").toString();
    for (int st = knob; st <= horizontalFader; ++st)
        if (nameOf ((Style) st) == style)
            c.style = (Style) st;
    c.bounds = juce::Rectangle<int>::fromString (t.getProperty ("bounds").toString());
    c.textSize = t.getProperty ("textSize", c.textSize);
    return c;
}

PanelControl Panel::standard (PanelControl::Kind kind)
{
    PanelControl c;
    c.kind = kind;
    if (c.isKnob())
    {
        // Knobs sit in a row just above Decent Sampler's keyboard, so a background picture shows
        // above them.
        c.label = PanelControl::nameOf (kind);
        c.bounds = { 20 + 110 * (int) kind, keyboardTop - 100, 90, 90 };
        if (kind >= PanelControl::attack)   // not in the standard layout: a row above, on the right
            c.bounds.setPosition (width - 130 - 110 * (PanelControl::tune - kind), keyboardTop - 195);
    }
    else if (kind == PanelControl::title)
    {
        c.bounds = { 20, topBarBottom + 10, width - 280, 40 };
        c.textSize = 28;
    }
    else if (kind == PanelControl::articulations)
    {
        c.bounds = { width - 230, topBarBottom + 15, 210, 30 };
    }
    else
    {
        c.label = "Text";
        c.bounds = { 20, topBarBottom + 60, 300, 30 };
    }
    return c;
}

std::vector<PanelControl> Panel::standardLayout (bool effects, bool articulationMenu)
{
    std::vector<PanelControl> layout;
    auto title = standard (PanelControl::title);
    if (! articulationMenu)
        title.bounds.setWidth (width - 40);
    layout.push_back (title);
    layout.push_back (standard (PanelControl::volume));
    layout.push_back (standard (PanelControl::release));
    if (effects)
        for (auto k : { PanelControl::tone, PanelControl::chorus, PanelControl::delay, PanelControl::reverb, PanelControl::room })
            layout.push_back (standard (k));
    if (articulationMenu)
        layout.push_back (standard (PanelControl::articulations));
    return layout;
}

juce::Rectangle<int> Panel::freeSpot (const std::vector<PanelControl>& controls, juce::Rectangle<int> wanted)
{
    const auto isFree = [&controls] (juce::Rectangle<int> r)
    {
        return r.getY() >= topBarBottom && r.getBottom() <= keyboardTop && r.getRight() <= width
               && std::none_of (controls.begin(), controls.end(), [r] (const auto& c) { return c.bounds.intersects (r); });
    };
    if (isFree (wanted))
        return wanted;
    for (int y = keyboardTop - 10 - wanted.getHeight(); y >= topBarBottom; y -= 10)
        for (int x = 20; x + wanted.getWidth() <= width - 20; x += 10)
            if (isFree (wanted.withPosition (x, y)))
                return wanted.withPosition (x, y);
    return wanted;
}

std::vector<PanelControl> Panel::layout (bool effects, bool articulationMenu) const
{
    if (controls.empty())
        return standardLayout (effects, articulationMenu);
    std::vector<PanelControl> layout;
    for (const auto& c : controls)
        if ((effects || ! c.isEffect()) && (articulationMenu || c.kind != PanelControl::articulations))
            layout.push_back (c);
    // A library that gained articulations after the panel was laid out still gets its menu.
    if (articulationMenu && std::none_of (layout.begin(), layout.end(), [] (const auto& c) { return c.kind == PanelControl::articulations; }))
        layout.push_back (standard (PanelControl::articulations));
    return layout;
}

juce::ValueTree Panel::toValueTree() const
{
    juce::ValueTree t ("Panel");
    t.setProperty ("background", background.toString(), nullptr);
    t.setProperty ("textColour", textColour.toString(), nullptr);
    t.setProperty ("knobColour", knobColour.toString(), nullptr);
    t.setProperty ("floeVignette", floeVignette.toString(), nullptr);
    t.setProperty ("floeTint", floeTint.toString(), nullptr);
    for (const auto& c : controls)
        t.appendChild (c.toValueTree(), nullptr);
    return t;
}

Panel Panel::fromValueTree (const juce::ValueTree& t)
{
    Panel p;
    if (! t.isValid())
        return p;
    p.background = juce::Colour::fromString (t.getProperty ("background", p.background.toString()).toString());
    p.textColour = juce::Colour::fromString (t.getProperty ("textColour", p.textColour.toString()).toString());
    p.knobColour = juce::Colour::fromString (t.getProperty ("knobColour", p.knobColour.toString()).toString());
    p.floeVignette = juce::Colour::fromString (t.getProperty ("floeVignette", p.floeVignette.toString()).toString());
    p.floeTint = juce::Colour::fromString (t.getProperty ("floeTint", p.floeTint.toString()).toString());
    for (const auto& child : t)
        if (child.hasType ("Control"))
            p.controls.push_back (PanelControl::fromValueTree (child));
    return p;
}
} // namespace syn
