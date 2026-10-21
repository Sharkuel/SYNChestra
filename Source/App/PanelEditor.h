#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "../Core/SampleMap.h"

/** Lays out a library's Decent Sampler panel: drag the knobs, title, texts and articulation menu
    where you want them, pick the colours and the background picture. Edits the map it's given
    and calls onChange after each change. */
class PanelEditor final : public juce::Component
{
public:
    PanelEditor (syn::SampleMap& map, std::function<void()> onChange);
    ~PanelEditor() override;

    /** Shows the map as it is now, after it changed elsewhere (a project opened, effects ticked). */
    void refresh();

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    class Canvas;

    std::vector<syn::PanelControl>& controls();   // the map's own, filled in from the standard layout on the first edit
    bool showsArticulations() const;
    bool visible (const syn::PanelControl&) const;
    void changed();
    void select (int index);
    void showAddMenu();
    void showColourMenu();
    void chooseBackground();
    void chooseColour (juce::Colour& colour, const juce::String& what);
    void resetLayout();
    void showSelection();

    syn::SampleMap& map;
    std::function<void()> onChange;
    std::vector<syn::PanelControl> shown;   // the standard layout, until the first edit
    int selected = -1;
    juce::Image background;
    juce::String backgroundLoaded;

    std::unique_ptr<Canvas> canvas;
    juce::TextButton addButton { "Add..." }, backgroundButton { "Background..." }, coloursButton { "Colours..." },
        resetButton { "Standard layout" }, removeButton { "Remove" };
    juce::Label selectedName, labelCaption { {}, "Label" }, sizeCaption { {}, "Size" }, styleCaption { {}, "Style" }, help;
    juce::ComboBox style;
    juce::TextEditor labelText;
    juce::Slider size { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PanelEditor)
};
