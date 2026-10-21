#include "PanelEditor.h"

namespace
{
constexpr int sideWidth = 230, toolbarHeight = 40, margin = 10, grid = 5;

/** Draws one control as Decent Sampler shows it, near enough to place it by. */
void drawControl (juce::Graphics& g, const syn::PanelControl& c, const syn::Panel& panel,
                  const juce::String& libraryName, const juce::String& firstArticulation)
{
    const auto r = c.bounds.toFloat();
    if (c.isKnob())
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
        dragStart = list[(size_t) hit].bounds.getPosition();
        dragging = true;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging || editor.selected < 0 || e.getDistanceFromDragStart() < 2)
            return;
        auto& c = editor.controls()[(size_t) editor.selected];
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
        dragging = moved = false;
    }

private:
    PanelEditor& editor;
    juce::Point<int> dragStart;
    bool dragging = false, moved = false;
};

PanelEditor::PanelEditor (syn::SampleMap& m, std::function<void()> changeCallback)
    : map (m), onChange (std::move (changeCallback)), canvas (std::make_unique<Canvas> (*this))
{
    for (auto* c : std::initializer_list<juce::Component*> { canvas.get(), &addButton, &backgroundButton, &coloursButton,
                                                            &resetButton, &selectedName, &labelCaption, &labelText,
                                                            &sizeCaption, &size, &removeButton, &help })
        addAndMakeVisible (c);

    addButton.onClick = [this] { showAddMenu(); };
    addButton.setTooltip ("Adds a knob that isn't on the panel yet, a line of text, or the articulation menu.");
    backgroundButton.onClick = [this] { chooseBackground(); };
    coloursButton.onClick = [this] { showColourMenu(); };
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
        if (c.isKnob())
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
    help.setText ("Drag things to move them. Click one to rename or resize it; right-click to remove it. "
                  "SFZ players show no panel: they list the Volume and Release knobs by the names given here.",
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
}

void PanelEditor::changed()
{
    if (onChange)
        onChange();
    canvas->repaint();
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
    for (auto* c : std::initializer_list<juce::Component*> { &labelCaption, &labelText, &sizeCaption, &size, &removeButton })
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
    if (c.isKnob())
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
    for (int k = syn::PanelControl::volume; k <= syn::PanelControl::room; ++k)
    {
        const auto kind = (syn::PanelControl::Kind) k;
        const bool effect = kind >= syn::PanelControl::tone;
        if (has (kind))
            continue;
        menu.addItem (syn::PanelControl::nameOf (kind) + " knob" + (effect && ! map.effects ? " (tick Effect knobs first)" : ""),
                      ! effect || map.effects, false, [this, kind]
        {
            controls().push_back (syn::Panel::standard (kind));
            select ((int) controls().size() - 1);
            changed();
        });
    }
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

void PanelEditor::showColourMenu()
{
    juce::PopupMenu menu;
    menu.addItem ("Background colour...", [this] { chooseColour (map.panel.background, "Background"); });
    menu.addItem ("Text colour...", [this] { chooseColour (map.panel.textColour, "Text"); });
    menu.addItem ("Knob colour...", [this] { chooseColour (map.panel.knobColour, "Knob"); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (coloursButton));
}

void PanelEditor::chooseColour (juce::Colour& colour, const juce::String& what)
{
    /** A colour picker that sets the panel's colour as it moves. */
    struct Picker final : public juce::ColourSelector, private juce::ChangeListener
    {
        Picker (juce::Colour& c, std::function<void()> moved)
            : ColourSelector (ColourSelector::showColourAtTop | ColourSelector::showSliders | ColourSelector::showColourspace),
              colour (c), onMove (std::move (moved))
        {
            setCurrentColour (c, juce::dontSendNotification);
            setSize (300, 320);
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
    juce::ignoreUnused (what);
    auto picker = std::make_unique<Picker> (colour, [this] { changed(); });
    juce::CallOutBox::launchAsynchronously (std::move (picker), coloursButton.getScreenBounds(), nullptr);
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
    for (auto* b : { &addButton, &coloursButton })
    {
        b->setBounds (bar.removeFromLeft (100));
        bar.removeFromLeft (8);
    }
    backgroundButton.setBounds (bar.removeFromLeft (240));
    bar.removeFromLeft (8);
    resetButton.setBounds (bar.removeFromLeft (140));
    area.removeFromTop (margin);
    canvas->setBounds (area.removeFromLeft (syn::Panel::width).withHeight (syn::Panel::height));
    area.removeFromLeft (margin);
    selectedName.setBounds (area.removeFromTop (28));
    labelCaption.setBounds (area.removeFromTop (22));
    labelText.setBounds (area.removeFromTop (26));
    area.removeFromTop (8);
    sizeCaption.setBounds (area.removeFromTop (22));
    size.setBounds (area.removeFromTop (26));
    area.removeFromTop (8);
    removeButton.setBounds (area.removeFromTop (28).removeFromLeft (100));
    area.removeFromTop (12);
    help.setBounds (area);
}
