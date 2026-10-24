#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "../Core/SampleMap.h"
#include "../Core/Project.h"
#include "../Core/Library.h"
#include "../Core/LibraryPlayer.h"
#include "../Core/SampleImporter.h"
#include "LiveHost.h"
#include "NoteKeyboard.h"
#include "PanelEditor.h"
#include "Help.h"

/** Find instrument plugins, load one, describe what to record, and record it into a
    Decent Sampler and SFZ library. */
class MainComponent : public juce::Component,
                      private juce::TableListBoxModel,
                      private juce::ChangeListener,
                      private juce::MidiKeyboardState::Listener
{
public:
    MainComponent();
    ~MainComponent() override;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    enum Column { colOn = 1, colName, colLow, colHigh, colStep, colLayers, colRoundRobins, colHold, colRelease,
                  colPitched, colOneShot, colLoop, colKeyUp, colChannel, colSwitch, colCount, colSound };

    void scanPlugins();
    void refreshPluginChoices();
    static juce::String pluginLabel (const juce::PluginDescription&);
    void loadSelectedPlugin();
    bool loadPlugin (const juce::PluginDescription&);
    void showMap();
    void showProjectMenu();
    void openProject();
    void saveProject();
    syn::Project currentProject() const;
    void useProject (const syn::Project&, const juce::String& name);
    void showPluginWindow();
    void refreshOutputs();
    void chooseOutput();
    void setPreview (bool on);
    void addGroup (syn::Group);
    int currentSound() const;
    void showAddSoundMenu();
    void addSound();
    void addSound (const juce::String& name, juce::MemoryBlock state);
    void addPresets();
    void catchSounds (bool on);
    void catchNextSound();
    void showSoundMenu (int row);
    void removeSound (int sound);
    void removeGroup();
    void listenToGroup();
    void measureGroup();
    void recordLibrary();
    /** A library from WAV or FLAC files instead of a plugin: each folder in the one picked is a row. */
    void librarySamples();
    /** Why the ticked formats can't be written, or empty. */
    juce::String presetProblem() const;
    /** Where each ticked format is saved. */
    struct Target { juce::File folder; bool sfz, ds, floe; };
    std::vector<Target> formatTargets() const;
    /** Writes the presets into every target, sharing the samples in the first one. Returns what was saved
        where, or an error. */
    static juce::String saveFormats (const syn::Library&, const std::vector<Target>&, juce::ThreadWithProgressWindow&);
    void findNotes();
    void offerFoundRows (const juce::Array<int>& found, int channel);
    void showKeyMenu (int note);
    void updateKeyboard();
    int selectedChannel() const;
    void selectedRowsChanged (int lastRowSelected) override;
    void handleNoteOn (juce::MidiKeyboardState*, int channel, int note, float velocity) override;
    void handleNoteOff (juce::MidiKeyboardState*, int channel, int note, float velocity) override;
    void saveMap();
    void refreshTable();
    juce::File outputFolder() const;
    void chooseOutputFolder();
    void showPanelEditor();
    void showOutputFolder();
    double sampleRate() const;
    void setLive (bool on);
    void enableMidiInputs();
    bool runJob (const juce::String& title, std::function<juce::String (juce::ThreadWithProgressWindow&)> work,
                 std::function<void (const juce::String&)> done);
    void setStatus (const juce::String& text);
    void setUpHelp();
    void showHelp (bool on);
    void explainAt (juce::Point<int>);
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    juce::File scanCrashFile() const;

    juce::String cellText (const syn::Group&, int column) const;
    void setCell (int row, int column, const juce::String& text);

    // TableListBoxModel
    int getNumRows() override;
    void paintRowBackground (juce::Graphics&, int row, int w, int h, bool selected) override;
    void paintCell (juce::Graphics&, int row, int column, int w, int h, bool selected) override;
    juce::Component* refreshComponentForCell (int row, int column, bool selected, juce::Component* existing) override;

    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList knownPlugins;
    juce::PropertiesFile::Options settingsOptions;
    std::unique_ptr<juce::PropertiesFile> settings;
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    std::unique_ptr<juce::DocumentWindow> pluginWindow;
    syn::SampleMap map;

    juce::AudioDeviceManager audioDevices;
    LiveHost live;   // the plugin on the sound card, played from MIDI inputs and its own window
    std::unique_ptr<juce::ThreadWithProgressWindow> job;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::TextButton scanButton { "Find plugins" }, loadButton { "Load plugin" }, showButton { "Show plugin" },
        addKeysButton { "Add keys" }, addSoundButton { "Add sound" }, addDrumButton { "Add drum" }, removeButton { "Remove" },
        listenButton { "Listen" }, measureButton { "Measure" }, findButton { "Find notes" }, recordButton { "Record library" }, saveToButton { "Change..." };
    juce::ToggleButton decentSampler { "Decent Sampler (.dspreset)" }, sfz { "SFZ (.sfz)" }, floe { "Floe" }, effectKnobs { "Effect knobs" },
        skipSameLayers { "Skip same layers" }, flacSamples { "FLAC" }, folderPerFormat { "Folder per format" };
    juce::TextButton projectButton { "Project..." };
    juce::TextButton panelButton { "Panel..." };
    std::unique_ptr<juce::DocumentWindow> panelWindow;
    PanelEditor* panelEditor = nullptr;   // in panelWindow
    juce::TextButton previewButton { "Try library" };
    juce::ComboBox pluginChoice, rateChoice, outputChoice;
    juce::Label nameLabel { {}, "Library name" }, saveToLabel { {}, "Save to" }, saveToPath, totalLabel;
    juce::TextEditor libraryName, pluginSearch;
    juce::TableListBox table { {}, this };
    juce::MidiKeyboardState keyboardState;
    NoteKeyboard keyboard { keyboardState };
    std::optional<syn::Library> lastLibrary;   // the library recorded last, which Try library plays
    juce::File lastLibraryFolder;
    std::unique_ptr<syn::LibraryPlayer> player;
    int newRowStart = -1;   // first key of a key row being drawn on the keyboard
    juce::Image logo;
    juce::Rectangle<int> logoArea;   // beside the top two rows
    juce::Label status;
    juce::TooltipWindow tooltips { this };
    juce::TextButton helpButton { "Help" };
    HelpOverlay helpOverlay;                                                // over the window while Help is on
    std::vector<std::pair<juce::Component*, juce::String>> helpTexts;       // "Title\nWhat it does"
    std::map<int, juce::String> columnHelp;                                 // the same for the table's columns

    /** Checks the plugin's settings every second while sounds are being caught. */
    struct CatchTimer final : public juce::Timer
    {
        std::function<void()> tick;
        void timerCallback() override { tick(); }
    } catchTimer;
    std::unique_ptr<syn::SoundCatcher> catcher;   // while catching sounds
    int caught = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
