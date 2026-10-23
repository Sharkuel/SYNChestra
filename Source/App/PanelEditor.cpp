#include "PanelEditor.h"

namespace
{
constexpr int sideWidth = 230, toolbarHeight = 40, margin = 10, grid = 5;

/** Draws one control as Decent Sampler shows it, near enough to place it by. */
void drawControl (juce::Graphics& g, const syn::PanelControl& c, const syn::Panel& panel,
                  const juce::String& libraryName, const juce::String& firstArticulation)
{
    const auto r = c.bounds.toFloat();
    if (c.isKnob() && c.style != syn::PanelControl::knob)
    {
        // A fader: its name on top, then the track, filled up to its value, and a thumb.
        const float labelHeight = 18.0f;
        g.setColour (panel.textColour);
        g.setFont (juce::FontOptions (14.0f));
        g.drawFittedText (c.label, r.withHeight (labelHeight).toNearestInt(), juce::Justification::centred, 1);
        const auto area = r.withTrimmedTop (labelHeight).reduced (8.0f);
        const bool vertical = c.style == syn::PanelControl::verticalFader;
        const auto track = vertical ? area.withSizeKeepingCentre (6.0f, area.getHeight())
                                    : area.withSizeKeepingCentre (area.getWidth(), 6.0f);
        g.setColour (juce::Colours::grey.withAlpha (0.6f));
        g.fillRoundedRectangle (track, 3.0f);
        const auto filled = vertical ? track.withTrimmedTop (track.getHeight() * 0.3f) : track.withWidth (track.getWidth() * 0.7f);
        g.setColour (panel.knobColour);
        g.fillRoundedRectangle (filled, 3.0f);
        const auto thumbAt = vertical ? juce::Point<float> (track.getCentreX(), filled.getY())
                                      : juce::Point<float> (filled.getRight(), track.getCentreY());
        g.setColour (juce::Colours::white);
        g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (thumbAt));
    }
    else if (c.isKnob())
    {
        const float labelHeight = 18.0f;
        const auto dial = r.withTrimmedBottom (labelHeight).reduced (6.0f);
        const float side = juce::jmin (dial.getWidth(), dial.getHeight());
        const auto circle = dial.withSizeKeepingCentre (side, side);
        const float start = juce::MathConstants<float>::pi * 1.25f, end = juce::MathConstants<float>::pi * 2.75f;
        const float at = start + 0.7f * (end - start);
        juce::Path track, value;
        track.addCentredArc (circle.getCentreX(), circle.getCentreY(), side / 2 - 3, side / 2 - 3, 0, start, end, true);
        value.addCentredArc (circle.getCentreX(), circle.getCentreY(), side / 2 - 3, side / 2 - 3, 0, start, at, true);
        g.setColour (juce::Colours::grey.withAlpha (0.6f));
        g.strokePath (track, juce::PathStrokeType (4.0f));
        g.setColour (panel.knobColour);
        g.strokePath (value, juce::PathStrokeType (4.0f));
        g.setColour (panel.textColour);
        g.setFont (juce::FontOptions (14.0f));
        g.drawFittedText (c.label, r.withTop (r.getBottom() - labelHeight).toNearestInt(), juce::Justification::centred, 1);
    }
    else if (c.kind == syn::PanelControl::articulations)
    {
        g.setColour (juce::Colour (0xFF3A4048));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (15.0f));
        g.drawFittedText (firstArticulation + "  \xe2\x96\xbe", r.reduced (8, 0).toNearestInt(),
                          juce::Justification::centredLeft, 1);
    }
    else
    {
        g.setColour (panel.textColour);
        g.setFont (juce::FontOptions ((float) c.textSize));
        const auto text = c.kind == syn::PanelControl::title && c.label.isEmpty() ? libraryName : c.label;
        g.drawFittedText (text, r.toNearestInt(), juce::Justification::centredLeft, 1);
    }
}
} // namespace

/** The panel at full size. Click to select, drag to move, right-click to remove. */
class PanelEditor::Canvas final : public juce::Component
{
public:
    explicit Canvas (PanelEditor& e) : editor (e) {}

    void paint (juce::Graphics& g) override
    {
        const auto& map = editor.map;
        g.fillAll (map.panel.background);
        if (editor.background.isValid())
            g.drawImage (editor.background, getLocalBounds().toFloat(), juce::RectanglePlacement::fillDestination);
        juce::String firstArticulation = "Articulation";
        for (const auto& group : map.groups)
            if (group.enabled && group.keyswitch.isNotEmpty())
            {
                firstArticulation = group.name;
                break;
            }
        const auto& list = editor.map.panel.controls.empty() ? editor.shown : editor.map.panel.controls;
        for (int i = 0; i < (int) list.size(); ++i)
        {
            if (! editor.visible (list[(size_t) i]))
                continue;
            drawControl (g, list[(size_t) i], map.panel, map.libraryName, firstArticulation);
            if (i == editor.selected)
            {
                g.setColour (juce::Colours::deepskyblue);
                g.drawRect (list[(size_t) i].bounds.expanded (2), 2);
            }
        }
        drawCovered (g);
    }

    /** Decent Sampler's top bar and keyboard, which cover the top and bottom of the panel. Shown
        see-through, so what's under them can still be found and dragged out. */
    void drawCovered (juce::Graphics& g)
    {
        const auto bar = getLocalBounds().removeFromTop (syn::Panel::topBarBottom).toFloat();
        g.setColour (juce::Colour (0xFF16181C).withAlpha (0.75f));
        g.fillRect (bar);
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("Decent Sampler's top bar covers this", bar, juce::Justification::centred);

        auto keys = getLocalBounds().withTop (syn::Panel::keyboardTop).toFloat();
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.fillRect (keys);
        const float keyWidth = keys.getWidth() / 52.0f;   // a little over seven octaves
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        for (int k = 1; k < 52; ++k)
            g.fillRect (keys.getX() + k * keyWidth, keys.getY(), 1.0f, keys.getHeight());
        for (int k = 0; k < 52; ++k)
            if (const int inOctave = k % 7; inOctave != 2 && inOctave != 6)
                g.fillRect (keys.getX() + (k + 0.7f) * keyWidth, keys.getY(), keyWidth * 0.6f, keys.getHeight() * 0.6f);
        g.setColour (juce::Colours::black.withAlpha (0.8f));
        g.drawText ("Decent Sampler's keyboard covers this", keys.removeFromBottom (keys.getHeight() * 0.4f),
                    juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto& list = editor.map.panel.controls.empty() ? editor.shown : editor.map.panel.controls;
        int hit = -1;
        for (int i = (int) list.size(); --i >= 0;)   // the topmost first
            if (editor.visible (list[(size_t) i]) && list[(size_t) i].bounds.expanded (3).contains (e.getPosition()))
            {
                hit = i;
                break;
            }
        editor.select (hit);
        if (hit < 0)
            return;
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu menu;
            menu.addItem ("Remove " + syn::PanelControl::nameOf (list[(size_t) hit].kind), [this, hit]
            {
                editor.controls().erase (editor.controls().begin() + hit);
                editor.select (-1);
                editor.changed();
            });
            menu.showMenuAsync ({});
            return;
        }
        // A fader's far end resizes it; anywhere else moves it.
        const auto& c = list[(size_t) hit];
        const auto b = c.bounds;
        resizing = c.isKnob()
                   && ((c.style == syn::PanelControl::verticalFader && e.y > b.getBottom() - 8)
                       || (c.style == syn::PanelControl::horizontalFader && e.x > b.getRight() - 8));
        startBounds = b;
        dragStart = b.getPosition();
        dragging = true;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging || editor.selected < 0 || e.getDistanceFromDragStart() < 2)
            return;
        auto& c = editor.controls()[(size_t) editor.selected];
        if (resizing)
        {
            const auto snap = [] (int v) { return juce::roundToInt (v / (float) grid) * grid; };
            if (c.style == syn::PanelControl::verticalFader)
                c.bounds.setHeight (juce::jlimit (80, syn::Panel::height - c.bounds.getY(),
                                                  snap (startBounds.getHeight() + e.getDistanceFromDragStartY())));
            else
                c.bounds.setWidth (juce::jlimit (100, syn::Panel::width - c.bounds.getX(),
                                                 snap (startBounds.getWidth() + e.getDistanceFromDragStartX())));
            moved = true;
            editor.showSelection();
            repaint();
            return;
        }
        auto at = dragStart + e.getOffsetFromDragStart();
        at = { juce::roundToInt (at.x / (float) grid) * grid, juce::roundToInt (at.y / (float) grid) * grid };
        c.bounds.setPosition (juce::jlimit (0, syn::Panel::width - c.bounds.getWidth(), at.x),
                              juce::jlimit (0, syn::Panel::height - c.bounds.getHeight(), at.y));
        moved = true;
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (moved)
            editor.changed();
        dragging = moved = resizing = false;
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        // The resize cursor over a fader's far end.
        const auto& list = editor.map.panel.controls.empty() ? editor.shown : editor.map.panel.controls;
        auto shape = juce::MouseCursor::NormalCursor;
        for (const auto& c : list)
            if (editor.visible (c) && c.isKnob() && c.bounds.contains (e.getPosition()))
            {
                if (c.style == syn::PanelControl::verticalFader && e.y > c.bounds.getBottom() - 8)
                    shape = juce::MouseCursor::UpDownResizeCursor;
                else if (c.style == syn::PanelControl::horizontalFader && e.x > c.bounds.getRight() - 8)
                    shape = juce::MouseCursor::LeftRightResizeCursor;
            }
        setMouseCursor (shape);
    }

private:
    PanelEditor& editor;
    juce::Point<int> dragStart;
    juce::Rectangle<int> startBounds;
    bool dragging = false, moved = false, resizing = false;
};

/** Floe's window, roughly, as a library's look changes it: its background picture filling the
    middle under the vignette, the layer panels showing it blurred under the tint, and the icon
    on each layer's instrument. Floe draws its own controls; these are only outlines to judge by. */
class PanelEditor::FloeView final : public juce::Component
{
public:
    explicit FloeView (PanelEditor& e) : editor (e) {}

    void paint (juce::Graphics& g) override
    {
        const auto& map = editor.map;
        g.fillAll (juce::Colour (0xff111214));
        // Floe's window is about 16:9.
        auto window = getLocalBounds().toFloat().withSizeKeepingCentre (getHeight() * 16.0f / 9.0f, (float) getHeight());
        g.setColour (juce::Colour (0xff1c1d20));
        g.fillRect (window);

        auto mid = window;
        const auto top = mid.removeFromTop (window.getHeight() * 0.08f);
        const auto bottom = mid.removeFromBottom (window.getHeight() * 0.13f);

        // The middle: the picture filling it, then the vignette over it.
        if (editor.background.isValid())
            g.drawImage (editor.background, mid, juce::RectanglePlacement::fillDestination);
        else
            g.setColour (juce::Colour (0xff26282c)), g.fillRect (mid);
        const auto vignette = map.panel.floeVignette;
        if (! vignette.isTransparent())
        {
            const auto centre = mid.getCentre();
            const float inner = 0.2f * juce::jmin (mid.getWidth(), mid.getHeight());
            const float outer = centre.getDistanceFrom (mid.getTopLeft());
            juce::ColourGradient gradient (vignette.withAlpha (0.0f), centre, vignette, centre.translated (outer, 0.0f), true);
            gradient.addColour (inner / outer, vignette.withAlpha (0.0f));
            g.setGradientFill (gradient);
            g.fillRect (mid);
        }

        // The tabs, then three layer panels: the picture blurred, darkened as Floe does, and tinted.
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        auto tabs = mid.removeFromTop (mid.getHeight() * 0.1f).withSizeKeepingCentre (220.0f, 18.0f);
        for (const auto* name : { "PERFORM", "LAYERS", "EFFECTS" })
            g.drawText (name, tabs.removeFromLeft (73.0f), juce::Justification::centred);
        auto panels = mid.reduced (mid.getWidth() * 0.02f, mid.getHeight() * 0.04f);
        const float gap = panels.getWidth() * 0.015f;
        const float panelWidth = (panels.getWidth() - 2.0f * gap) / 3.0f;
        const auto names = instrumentNames();
        for (int layer = 0; layer < 3; ++layer)
        {
            const auto r = panels.removeFromLeft (panelWidth);
            panels.removeFromLeft (gap);
            juce::Path shape;
            shape.addRoundedRectangle (r, 6.0f);
            {
                juce::Graphics::ScopedSaveState state (g);
                g.reduceClipRegion (shape);
                if (blurred.isValid())
                {
                    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
                    g.drawImage (blurred, mid.getUnion (r), juce::RectanglePlacement::fillDestination);
                }
                g.fillAll (juce::Colours::black.withAlpha (0.35f));
                if (! map.panel.floeTint.isTransparent())
                    g.fillAll (map.panel.floeTint);
            }
            g.setColour (juce::Colours::white.withAlpha (0.15f));
            g.strokePath (shape, juce::PathStrokeType (1.0f));

            // The layer's instrument, with the library's icon beside its name.
            auto header = r.reduced (8.0f).removeFromTop (22.0f);
            g.setColour (juce::Colours::black.withAlpha (0.3f));
            g.fillRoundedRectangle (header, 3.0f);
            auto iconArea = header.removeFromLeft (22.0f).reduced (3.0f);
            if (editor.icon.isValid())
                g.drawImage (editor.icon, iconArea, juce::RectanglePlacement::fillDestination);
            else
                g.setColour (juce::Colours::white.withAlpha (0.25f)), g.drawRect (iconArea);
            g.setColour (juce::Colours::white.withAlpha (layer < names.size() ? 0.9f : 0.4f));
            g.setFont (juce::FontOptions (12.0f));
            g.drawText (layer < names.size() ? names[layer] : juce::String ("Empty layer"), header.reduced (4.0f, 0.0f),
                        juce::Justification::centredLeft, true);

            // Floe's own knobs, as faint outlines.
            g.setColour (juce::Colours::white.withAlpha (0.18f));
            auto knobs = r.reduced (12.0f).withTrimmedTop (36.0f);
            for (int row = 0; row < 3; ++row)
            {
                auto line = knobs.removeFromTop (knobs.getHeight() / (float) (3 - row));
                for (int k = 0; k < 3; ++k)
                    g.drawEllipse (line.removeFromLeft (line.getWidth() / (float) (3 - k)).withSizeKeepingCentre (26.0f, 26.0f), 1.5f);
            }
        }

        // The top bar, with the library's name, and the keyboard along the bottom.
        g.setColour (juce::Colour (0xff1c1d20));
        g.fillRect (top);
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText ("FLOE", top.reduced (10.0f, 0.0f), juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (map.libraryName, top, juce::Justification::centred);
        g.setColour (juce::Colour (0xff1c1d20));
        g.fillRect (bottom);
        auto keys = bottom.reduced (8.0f, 6.0f);
        const float keyWidth = keys.getWidth() / 52.0f;
        for (int k = 0; k < 52; ++k)
        {
            g.setColour (juce::Colour (0xffe8e8e8));
            g.fillRect (keys.getX() + k * keyWidth + 0.5f, keys.getY(), keyWidth - 1.0f, keys.getHeight());
            if (const int n = k % 7; n != 2 && n != 6 && k < 51)
            {
                g.setColour (juce::Colour (0xff202020));
                g.fillRect (keys.getX() + (k + 0.7f) * keyWidth, keys.getY(), keyWidth * 0.6f, keys.getHeight() * 0.6f);
            }
        }
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("Floe, roughly", window.reduced (6.0f).removeFromBottom (14.0f).withTrimmedBottom (0.0f).translated (0.0f, -bottom.getHeight()),
                    juce::Justification::bottomRight);
    }

    /** Remakes the blurred copy of the background, after it changed. */
    void update()
    {
        blurred = editor.background.isValid()
                      ? editor.background.rescaled (juce::jmax (1, editor.background.getWidth() / 24),
                                                    juce::jmax (1, editor.background.getHeight() / 24),
                                                    juce::Graphics::highResamplingQuality)
                      : juce::Image();
        repaint();
    }

private:
    /** The instruments Floe lists for the library: a row each, or a sound each. */
    juce::StringArray instrumentNames() const
    {
        juce::StringArray names;
        for (const auto& g : editor.map.groups)
            if (g.enabled)
                names.addIfNotAlreadyThere (editor.map.soundOf (g) != nullptr ? editor.map.soundOf (g)->name : g.name);
        return names;
    }

    PanelEditor& editor;
    juce::Image blurred;
};

PanelEditor::PanelEditor (syn::SampleMap& m, std::function<void()> changeCallback)
    : map (m), onChange (std::move (changeCallback)), canvas (std::make_unique<Canvas> (*this)),
      floeView (std::make_unique<FloeView> (*this))
{
    for (auto* c : std::initializer_list<juce::Component*> { canvas.get(), &knobsButton, &addButton, &backgroundButton, &coloursButton, &floeButton,
                                                            &resetButton, &selectedName, &labelCaption, &labelText,
                                                            &sizeCaption, &size, &removeButton, &help,
                                                            &styleCaption, &style, &dsViewButton, &floeViewButton })
        addAndMakeVisible (c);
    addChildComponent (*floeView);
    addChildComponent (floeHelp);
    for (auto* b : { &dsViewButton, &floeViewButton })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1);
    }
    dsViewButton.setToggleState (true, juce::dontSendNotification);
    dsViewButton.onClick = [this] { showFloeView (false); };
    floeViewButton.onClick = [this] { showFloeView (true); };
    dsViewButton.setTooltip ("Shows the Decent Sampler panel, to lay out.");
    floeViewButton.setTooltip ("Shows roughly how Floe looks with this library: its picture, icon, vignette and tint.");
    floeHelp.setText ("Floe always shows its own controls, so a Floe library only changes its look: the "
                      "background picture fills the middle of Floe's window, the vignette darkens its edges, "
                      "the panels show it blurred under the tint, and the icon marks the library's instruments. "
                      "Pick them with Background... and Floe.... Floe draws its own panels, so this view "
                      "is only a guide.",
                      juce::dontSendNotification);
    floeHelp.setJustificationType (juce::Justification::topLeft);
    floeHelp.setColour (juce::Label::textColourId, juce::Colours::grey);
    for (int st = syn::PanelControl::knob; st <= syn::PanelControl::horizontalFader; ++st)
        style.addItem (syn::PanelControl::nameOf ((syn::PanelControl::Style) st), st + 1);
    style.onChange = [this]
    {
        if (selected < 0 || style.getSelectedId() == 0)
            return;
        auto& c = controls()[(size_t) selected];
        c.setStyle ((syn::PanelControl::Style) (style.getSelectedId() - 1));
        c.bounds.setPosition (juce::jlimit (0, juce::jmax (0, syn::Panel::width - c.bounds.getWidth()), c.bounds.getX()),
                              juce::jlimit (0, juce::jmax (0, syn::Panel::height - c.bounds.getHeight()), c.bounds.getY()));
        showSelection();
        changed();
    };

    knobsButton.onClick = [this] { showKnobPicker(); };
    knobsButton.setTooltip ("Ticks which knobs the panel has: Volume, Release, Attack, Pan, Tune and the effects.");
    addButton.onClick = [this] { showAddMenu(); };
    addButton.setTooltip ("Adds the title, a line of text, or the articulation menu.");
    backgroundButton.onClick = [this] { chooseBackground(); };
    coloursButton.onClick = [this] { showColourMenu(); };
    floeButton.onClick = [this] { showFloeMenu(); };
    floeButton.setTooltip ("Floe libraries can't change Floe's panel, only its look: the background picture, an icon "
                           "in Floe's library list, a vignette darkening the picture's edges and a tint over "
                           "Floe's panels. Start with very see-through colours.");
    resetButton.onClick = [this] { resetLayout(); };
    resetButton.setTooltip ("Puts everything back where SYNChestra puts it: the title on top, the knobs along the bottom.");
    removeButton.onClick = [this]
    {
        if (selected < 0)
            return;
        controls().erase (controls().begin() + selected);
        select (-1);
        changed();
    };

    selectedName.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    labelText.onTextChange = [this]
    {
        if (selected < 0)
            return;
        controls()[(size_t) selected].label = labelText.getText();
        changed();
    };
    size.onValueChange = [this]
    {
        if (selected < 0)
            return;
        auto& c = controls()[(size_t) selected];
        const int v = (int) size.getValue();
        if (c.isKnob() && c.style == syn::PanelControl::verticalFader)
            c.bounds.setHeight (v);
        else if (c.isKnob() && c.style == syn::PanelControl::horizontalFader)
            c.bounds.setWidth (v);
        else if (c.isKnob())
            c.bounds.setSize (v, v);
        else if (c.kind == syn::PanelControl::articulations)
            c.bounds.setWidth (v);
        else
        {
            c.textSize = v;
            c.bounds.setHeight (juce::roundToInt (v * 1.5));
        }
        c.bounds.setPosition (juce::jlimit (0, juce::jmax (0, syn::Panel::width - c.bounds.getWidth()), c.bounds.getX()),
                              juce::jlimit (0, juce::jmax (0, syn::Panel::height - c.bounds.getHeight()), c.bounds.getY()));
        changed();
    };
    help.setText ("Drag things to move them. Click one to rename or resize it, or make a knob a fader; drag a "
                  "fader's far end to lengthen it. Right-click to remove one. Keep things off the shaded "
                  "strips: Decent Sampler's top bar and keyboard cover them. "
                  "SFZ players show no panel: they list the Volume, Release, Attack and Pan knobs by the "
                  "names given here.",
                  juce::dontSendNotification);
    help.setJustificationType (juce::Justification::topLeft);
    help.setColour (juce::Label::textColourId, juce::Colours::grey);

    refresh();
    setSize (margin * 3 + syn::Panel::width + sideWidth, margin * 3 + toolbarHeight + syn::Panel::height);
}

PanelEditor::~PanelEditor() = default;

bool PanelEditor::showsArticulations() const
{
    int count = 0;
    for (const auto& g : map.groups)
        if (g.enabled && g.keyswitch.isNotEmpty())
            ++count;
    return count > 1;
}

bool PanelEditor::visible (const syn::PanelControl& c) const
{
    return (map.effects || ! c.isEffect()) && (showsArticulations() || c.kind != syn::PanelControl::articulations);
}

std::vector<syn::PanelControl>& PanelEditor::controls()
{
    if (map.panel.controls.empty())
        map.panel.controls = shown;
    return map.panel.controls;
}

void PanelEditor::refresh()
{
    shown = syn::Panel::standardLayout (map.effects, showsArticulations());
    if (map.backgroundImage != backgroundLoaded)
    {
        backgroundLoaded = map.backgroundImage;
        background = map.backgroundImage.isEmpty() ? juce::Image()
                                                   : juce::ImageFileFormat::loadFrom (juce::File (map.backgroundImage));
        floeView->update();
    }
    if (map.floeIcon != iconLoaded)
    {
        iconLoaded = map.floeIcon;
        icon = map.floeIcon.isEmpty() ? juce::Image() : juce::ImageFileFormat::loadFrom (juce::File (map.floeIcon));
    }
    backgroundButton.setButtonText (map.backgroundImage.isEmpty() ? "Background..."
                                                                  : "Background: " + juce::File (map.backgroundImage).getFileName());
    backgroundButton.setTooltip (map.backgroundImage.isEmpty()
                                     ? "Pick a picture for the panel. Best at 812 x 375 pixels; others are scaled and "
                                       "cropped to fit."
                                     : map.backgroundImage);
    const auto& list = map.panel.controls.empty() ? shown : map.panel.controls;
    if (selected >= (int) list.size())
        selected = -1;
    showSelection();
    canvas->repaint();
    floeView->repaint();
}

void PanelEditor::changed()
{
    if (onChange)
        onChange();
    canvas->repaint();
    floeView->repaint();
}

void PanelEditor::showFloeView (bool floe)
{
    (floe ? floeViewButton : dsViewButton).setToggleState (true, juce::dontSendNotification);
    canvas->setVisible (! floe);
    floeView->setVisible (floe);
    floeHelp.setVisible (floe);
    for (auto* c : std::initializer_list<juce::Component*> { &knobsButton, &addButton, &coloursButton, &resetButton })
        c->setEnabled (! floe);
    selectedName.setVisible (! floe);
    help.setVisible (! floe);
    if (floe)
        for (auto* c : std::initializer_list<juce::Component*> { &labelCaption, &labelText, &sizeCaption, &size,
                                                                &removeButton, &styleCaption, &style })
            c->setVisible (false);
    else
        showSelection();
}

void PanelEditor::select (int index)
{
    selected = index;
    showSelection();
    canvas->repaint();
}

void PanelEditor::showSelection()
{
    const auto& list = map.panel.controls.empty() ? shown : map.panel.controls;
    const bool any = juce::isPositiveAndBelow (selected, (int) list.size());
    for (auto* c : std::initializer_list<juce::Component*> { &labelCaption, &labelText, &sizeCaption, &size, &removeButton,
                                                            &styleCaption, &style })
        c->setVisible (any);
    if (! any)
    {
        selectedName.setText ("Click something on the panel", juce::dontSendNotification);
        return;
    }
    const auto& c = list[(size_t) selected];
    selectedName.setText (syn::PanelControl::nameOf (c.kind), juce::dontSendNotification);
    const bool hasLabel = c.kind != syn::PanelControl::articulations;
    labelCaption.setVisible (hasLabel);
    labelText.setVisible (hasLabel);
    labelText.setTextToShowWhenEmpty (c.kind == syn::PanelControl::title ? map.libraryName : juce::String(),
                                      juce::Colours::grey);
    labelText.setText (c.kind == syn::PanelControl::title && c.label.isEmpty() ? map.libraryName : c.label,
                       juce::dontSendNotification);
    styleCaption.setVisible (c.isKnob());
    style.setVisible (c.isKnob());
    style.setSelectedId ((int) c.style + 1, juce::dontSendNotification);
    if (c.isKnob() && c.style != syn::PanelControl::knob)
    {
        const bool vertical = c.style == syn::PanelControl::verticalFader;
        sizeCaption.setText ("Length", juce::dontSendNotification);
        size.setRange (vertical ? 80 : 100, vertical ? syn::Panel::height : 600, 5);
        size.setValue (vertical ? c.bounds.getHeight() : c.bounds.getWidth(), juce::dontSendNotification);
    }
    else if (c.isKnob())
    {
        sizeCaption.setText ("Size", juce::dontSendNotification);
        size.setRange (50, 160, 5);
        size.setValue (c.bounds.getWidth(), juce::dontSendNotification);
    }
    else if (c.kind == syn::PanelControl::articulations)
    {
        sizeCaption.setText ("Width", juce::dontSendNotification);
        size.setRange (120, 400, 5);
        size.setValue (c.bounds.getWidth(), juce::dontSendNotification);
    }
    else
    {
        sizeCaption.setText ("Text size", juce::dontSendNotification);
        size.setRange (10, 60, 1);
        size.setValue (c.textSize, juce::dontSendNotification);
    }
}

void PanelEditor::showAddMenu()
{
    const auto& list = map.panel.controls.empty() ? shown : map.panel.controls;
    auto has = [&list] (syn::PanelControl::Kind kind)
    {
        return std::any_of (list.begin(), list.end(), [kind] (const auto& c) { return c.kind == kind; });
    };
    juce::PopupMenu menu;
    menu.addItem ("Knobs...", [this] { showKnobPicker(); });
    menu.addSeparator();
    if (! has (syn::PanelControl::title))
        menu.addItem ("Title", [this]
        {
            controls().push_back (syn::Panel::standard (syn::PanelControl::title));
            select ((int) controls().size() - 1);
            changed();
        });
    menu.addItem ("Text", [this]
    {
        controls().push_back (syn::Panel::standard (syn::PanelControl::text));
        select ((int) controls().size() - 1);
        changed();
    });
    if (! has (syn::PanelControl::articulations))
        menu.addItem ("Articulation menu" + juce::String (showsArticulations() ? "" : " (needs 2 rows with keyswitches)"),
                      showsArticulations(), false, [this]
        {
            controls().push_back (syn::Panel::standard (syn::PanelControl::articulations));
            select ((int) controls().size() - 1);
            changed();
        });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (addButton));
}

bool PanelEditor::shows (syn::PanelControl::Kind kind) const
{
    const auto& list = map.panel.controls.empty() ? shown : map.panel.controls;
    return std::any_of (list.begin(), list.end(), [this, kind] (const auto& c) { return c.kind == kind && visible (c); });
}

void PanelEditor::setShown (syn::PanelControl::Kind kind, bool on)
{
    auto& list = controls();
    const auto isKind = [kind] (const auto& c) { return c.kind == kind; };
    if (on)
    {
        const bool effect = kind >= syn::PanelControl::tone && kind <= syn::PanelControl::room;
        if (effect && ! map.effects)
            map.effects = true;   // the effects are written once a knob uses one
        if (std::none_of (list.begin(), list.end(), isKind))
        {
            auto c = syn::Panel::standard (kind);
            c.bounds = syn::Panel::freeSpot (list, c.bounds);
            list.push_back (c);
        }
    }
    else
    {
        list.erase (std::remove_if (list.begin(), list.end(), isKind), list.end());
    }
    selected = -1;
    showSelection();
    changed();
}

void PanelEditor::showKnobPicker()
{
    /** A tick box per knob: ticking one puts it on the panel in a free spot, unticking removes it. */
    struct Picker final : public juce::Component
    {
        explicit Picker (PanelEditor& e) : editor (e)
        {
            for (int k = syn::PanelControl::volume; k <= syn::PanelControl::tune; ++k)
            {
                const auto kind = (syn::PanelControl::Kind) k;
                auto& box = boxes.emplace_back (std::make_unique<juce::ToggleButton> (syn::PanelControl::nameOf (kind)));
                box->onClick = [this, kind, b = box.get()]
                {
                    editor.setShown (kind, b->getToggleState());
                    update();
                };
                addAndMakeVisible (*box);
            }
            heading.setText ("Knobs on the panel", juce::dontSendNotification);
            effectsNote.setText ("Tone, Chorus, Delay, Reverb and Room are Decent Sampler's effects; they start off.",
                                 juce::dontSendNotification);
            effectsNote.setColour (juce::Label::textColourId, juce::Colours::grey);
            effectsNote.setFont (juce::FontOptions (12.0f));
            addAndMakeVisible (heading);
            addAndMakeVisible (effectsNote);
            update();
            setSize (300, 40 + 26 * 5 + 50);
        }

        void update()
        {
            for (int k = 0; k < (int) boxes.size(); ++k)
                boxes[(size_t) k]->setToggleState (editor.shows ((syn::PanelControl::Kind) k), juce::dontSendNotification);
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (10);
            heading.setBounds (area.removeFromTop (24));
            effectsNote.setBounds (area.removeFromBottom (36));
            // Two columns: the amp's knobs, then the effects.
            const syn::PanelControl::Kind columns[2][5] = {
                { syn::PanelControl::volume, syn::PanelControl::release, syn::PanelControl::attack,
                  syn::PanelControl::pan, syn::PanelControl::tune },
                { syn::PanelControl::tone, syn::PanelControl::chorus, syn::PanelControl::delay,
                  syn::PanelControl::reverb, syn::PanelControl::room } };
            const int columnWidth = area.getWidth() / 2;
            for (int col = 0; col < 2; ++col)
            {
                auto column = area.withX (area.getX() + col * columnWidth).withWidth (columnWidth);
                for (const auto kind : columns[col])
                    boxes[(size_t) kind]->setBounds (column.removeFromTop (26));
            }
        }

        PanelEditor& editor;
        std::vector<std::unique_ptr<juce::ToggleButton>> boxes;
        juce::Label heading, effectsNote;
    };
    showFloeView (false);
    // Inside this window, as the colour picker: a separate popup can hide behind it on Linux.
    juce::CallOutBox::launchAsynchronously (std::make_unique<Picker> (*this), getLocalArea (&knobsButton, knobsButton.getLocalBounds()),
                                            this);
}

void PanelEditor::showColourMenu()
{
    juce::PopupMenu menu;
    menu.addItem ("Background colour...", [this] { chooseColour (map.panel.background, coloursButton); });
    menu.addItem ("Text colour...", [this] { chooseColour (map.panel.textColour, coloursButton); });
    menu.addItem ("Knob colour...", [this] { chooseColour (map.panel.knobColour, coloursButton); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (coloursButton));
}

void PanelEditor::showFloeMenu()
{
    showFloeView (true);
    juce::PopupMenu menu;
    menu.addItem (map.floeIcon.isEmpty() ? "Icon..." : "Icon: " + juce::File (map.floeIcon).getFileName() + "...",
                  [this] { chooseFloeIcon(); });
    if (map.floeIcon.isNotEmpty())
        menu.addItem ("No icon", [this]
        {
            map.floeIcon = {};
            refresh();
            changed();
        });
    menu.addSeparator();
    menu.addItem ("Vignette colour...", [this] { chooseColour (map.panel.floeVignette, floeButton, true); });
    menu.addItem ("Panel tint colour...", [this] { chooseColour (map.panel.floeTint, floeButton, true); });
    menu.addItem ("No vignette or tint", ! map.panel.floeVignette.isTransparent() || ! map.panel.floeTint.isTransparent(),
                  false, [this]
    {
        map.panel.floeVignette = map.panel.floeTint = juce::Colour (0x00000000);
        changed();
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (floeButton));
}

void PanelEditor::chooseFloeIcon()
{
    chooser = std::make_unique<juce::FileChooser> ("Pick an icon for the library in Floe (square works best)",
                                                   juce::File::getSpecialLocation (juce::File::userPicturesDirectory),
                                                   "*.png;*.jpg;*.jpeg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file == juce::File())
            return;
        if (! juce::ImageFileFormat::loadFrom (file).isValid())
            return juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Icon",
                                                           "Can't read " + file.getFileName() + " as a picture.");
        map.floeIcon = file.getFullPathName();
        refresh();
        changed();
    });
}

void PanelEditor::chooseColour (juce::Colour& colour, juce::Component& near, bool withAlpha)
{
    /** A colour picker that sets the panel's colour as it moves. */
    struct Picker final : public juce::ColourSelector, private juce::ChangeListener
    {
        Picker (juce::Colour& c, bool alpha, std::function<void()> moved)
            : ColourSelector (ColourSelector::showColourAtTop | ColourSelector::showSliders | ColourSelector::showColourspace
                              | (alpha ? ColourSelector::showAlphaChannel : 0)),
              colour (c), onMove (std::move (moved))
        {
            // A colour that's off starts a quarter strong, so the first move shows.
            setCurrentColour (alpha && c.isTransparent() ? juce::Colour (0x40000000) : c, juce::dontSendNotification);
            setSize (300, alpha ? 350 : 320);
            addChangeListener (this);
        }
        void changeListenerCallback (juce::ChangeBroadcaster*) override
        {
            colour = getCurrentColour();
            onMove();
        }
        juce::Colour& colour;
        std::function<void()> onMove;
    };
    auto picker = std::make_unique<Picker> (colour, withAlpha, [this] { changed(); });
    // Inside this window rather than on the desktop: some Linux desktops put a separate popup
    // window behind the panel window, where it can't be seen.
    juce::CallOutBox::launchAsynchronously (std::move (picker), getLocalArea (&near, near.getLocalBounds()), this);
}

void PanelEditor::chooseBackground()
{
    auto pick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Pick a background picture for the Decent Sampler panel",
                                                       juce::File::getSpecialLocation (juce::File::userPicturesDirectory),
                                                       "*.png;*.jpg;*.jpeg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file == juce::File())
                return;
            if (! juce::ImageFileFormat::loadFrom (file).isValid())
                return juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Background",
                                                               "Can't read " + file.getFileName() + " as a picture.");
            map.backgroundImage = file.getFullPathName();
            refresh();
            changed();
        });
    };
    if (map.backgroundImage.isEmpty())
        return pick();
    juce::PopupMenu menu;
    menu.addItem ("Choose another picture...", pick);
    menu.addItem ("No picture", [this]
    {
        map.backgroundImage = {};
        refresh();
        changed();
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (backgroundButton));
}

void PanelEditor::resetLayout()
{
    map.panel.controls.clear();
    selected = -1;
    refresh();
    changed();
}

void PanelEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
    g.setColour (juce::Colours::grey);
    g.drawRect (canvas->getBounds().expanded (1));
}

void PanelEditor::resized()
{
    auto area = getLocalBounds().reduced (margin);
    auto bar = area.removeFromTop (toolbarHeight - margin);
    for (auto* b : { &knobsButton, &addButton, &coloursButton, &floeButton })
    {
        b->setBounds (bar.removeFromLeft (100));
        bar.removeFromLeft (8);
    }
    backgroundButton.setBounds (bar.removeFromLeft (200));
    bar.removeFromLeft (8);
    resetButton.setBounds (bar.removeFromLeft (140));
    floeViewButton.setBounds (bar.removeFromRight (70));
    dsViewButton.setBounds (bar.removeFromRight (130));
    area.removeFromTop (margin);
    canvas->setBounds (area.removeFromLeft (syn::Panel::width).withHeight (syn::Panel::height));
    floeView->setBounds (canvas->getBounds());
    area.removeFromLeft (margin);
    floeHelp.setBounds (area);
    selectedName.setBounds (area.removeFromTop (28));
    labelCaption.setBounds (area.removeFromTop (22));
    labelText.setBounds (area.removeFromTop (26));
    area.removeFromTop (8);
    styleCaption.setBounds (area.removeFromTop (22));
    style.setBounds (area.removeFromTop (26));
    area.removeFromTop (8);
    sizeCaption.setBounds (area.removeFromTop (22));
    size.setBounds (area.removeFromTop (26));
    area.removeFromTop (8);
    removeButton.setBounds (area.removeFromTop (28).removeFromLeft (100));
    area.removeFromTop (12);
    help.setBounds (area);
}
