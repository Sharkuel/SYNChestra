#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <set>

/** The on-screen keyboard: plays the plugin, shows the selected row's keys (amber) and the keys
    "Find notes" heard (a green bar), and offers a menu on right-click to set notes from it. */
class NoteKeyboard : public juce::MidiKeyboardComponent, public juce::SettableTooltipClient
{
public:
    explicit NoteKeyboard (juce::MidiKeyboardState& keyState)
        : MidiKeyboardComponent (keyState, horizontalKeyboard)
    {
        setAvailableRange (0, 127);
        setScrollButtonsVisible (false);
        setOctaveForMiddleC (4);   // 60 is C4, as in the table
    }

    std::set<int> sounding;           // from the last "Find notes"
    juce::Range<int> rowKeys { -1, -1 };   // the selected row's lowest and highest, inclusive
    std::function<void (int note)> onKeyMenu;

    void showFound (std::set<int> notes)
    {
        sounding = std::move (notes);
        repaint();
    }

    void showRow (juce::Range<int> keys)
    {
        rowKeys = keys;
        repaint();
    }

    /** Fits all 128 keys in the width. */
    void resized() override
    {
        setKeyWidth (juce::jmax (4.0f, (float) getWidth() / 75.0f));
        MidiKeyboardComponent::resized();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            const auto note = getNoteAndVelocityAtPosition (e.position).note;
            if (note >= 0 && onKeyMenu)
                onKeyMenu (note);
            return;
        }
        MidiKeyboardComponent::mouseDown (e);
    }

protected:
    void drawWhiteNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver,
                        juce::Colour lineColour, juce::Colour textColour) override
    {
        MidiKeyboardComponent::drawWhiteNote (note, g, area, isDown, isOver, lineColour, textColour);
        mark (note, g, area);
    }

    void drawBlackNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver,
                        juce::Colour fill) override
    {
        MidiKeyboardComponent::drawBlackNote (note, g, area, isDown, isOver, fill);
        mark (note, g, area);
    }

private:
    void mark (int note, juce::Graphics& g, juce::Rectangle<float> area)
    {
        if (rowKeys.getStart() >= 0 && note >= rowKeys.getStart() && note <= rowKeys.getEnd())
        {
            g.setColour (juce::Colour (0xffffb74d).withAlpha (0.45f));
            g.fillRect (area.reduced (1.0f, 0.0f));
        }
        if (sounding.count (note) > 0)
        {
            g.setColour (juce::Colour (0xff43a047));
            g.fillRect (area.reduced (1.0f, 0.0f).removeFromBottom (juce::jmin (6.0f, area.getHeight() * 0.12f)));
        }
    }
};
