#include "MainComponent.h"
#include "../Core/Library.h"
#include "PluginScanner.h"

namespace
{
constexpr int blockSize = 512;

juce::File defaultOutputFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("SYNChestra");
}

class PluginWindow : public juce::DocumentWindow
{
public:
    PluginWindow (juce::AudioProcessorEditor* editor, const juce::String& name)
        : DocumentWindow (name, juce::Colours::black, DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (editor, true);
        setVisible (true);
    }

    void closeButtonPressed() override { setVisible (false); }
};

/** A table cell the user can type into. */
class EditableCell : public juce::Label
{
public:
    EditableCell() { setEditable (false, true, false); }
    std::function<void (const juce::String&)> onEdit;
    std::function<void()> onSelect;
    void textWasEdited() override { if (onEdit) onEdit (getText()); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (onSelect) onSelect();   // clicking a cell picks its row, for Listen and Remove
        Label::mouseDown (e);
    }
};

/** A tick box in a table cell. */
class ToggleCell : public juce::Component
{
public:
    ToggleCell() { addAndMakeVisible (box); }
    void resized() override { box.setBounds (getLocalBounds().withSizeKeepingCentre (24, 24)); }
    juce::ToggleButton box;
};

/** Runs work on a background thread behind a progress window with a Cancel button. */
class Job final : public juce::ThreadWithProgressWindow
{
public:
    Job (const juce::String& title, std::function<juce::String (ThreadWithProgressWindow&)> w,
         std::function<void (const juce::String&)> d)
        : ThreadWithProgressWindow (title, true, true), work (std::move (w)), done (std::move (d))
    {
    }

    void run() override { report = work (*this); }
    void threadComplete (bool) override { done (report); }

private:
    std::function<juce::String (ThreadWithProgressWindow&)> work;
    std::function<void (const juce::String&)> done;
    juce::String report;
};

/** A note number from "60", "C4" or "c#4". -1 if it's neither. */
int parseNote (juce::String text)
{
    text = text.trim();
    if (text.containsOnly ("0123456789"))
        return text.isEmpty() ? -1 : juce::jlimit (0, 127, text.getIntValue());
    for (int n = 0; n < 128; ++n)
        if (syn::noteName (n).equalsIgnoreCase (text))
            return n;
    return -1;
}

juce::String seconds (double s)
{
    return juce::String (s, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
}
} // namespace

MainComponent::MainComponent()
{
    formats.addDefaultFormats();

    settingsOptions.applicationName = "SYNChestra";
    settingsOptions.filenameSuffix = ".settings";
    settingsOptions.osxLibrarySubFolder = "Application Support";
    settings = std::make_unique<juce::PropertiesFile> (settingsOptions);
    if (auto xml = settings->getXmlValue ("knownPlugins"))
        knownPlugins.recreateFromXml (*xml);
    // A plugin that crashed the last scan is named in this file; it goes on the skip list.
    juce::PluginDirectoryScanner::applyBlacklistingsFromDeadMansPedal (knownPlugins, scanCrashFile());
    if (auto xml = knownPlugins.createXml())
        settings->setValue ("knownPlugins", xml.get());
    knownPlugins.addChangeListener (this);
    knownPlugins.setCustomScanner (std::make_unique<OutOfProcessScanner>());

    audioDevices.initialiseWithDefaultDevices (0, 2);
    audioDevices.addAudioCallback (&live);
    enableMidiInputs();   // a keyboard plays the plugin as soon as one is loaded

    for (juce::Component* c : { (juce::Component*) &scanButton, (juce::Component*) &pluginSearch,
                                (juce::Component*) &pluginChoice, (juce::Component*) &loadButton,
                                (juce::Component*) &showButton, (juce::Component*) &nameLabel,
                                (juce::Component*) &libraryName, (juce::Component*) &addKeysButton,
                                (juce::Component*) &addDrumButton, (juce::Component*) &removeButton,
                                (juce::Component*) &table, (juce::Component*) &totalLabel,
                                (juce::Component*) &decentSampler, (juce::Component*) &sfz,
                                (juce::Component*) &rateChoice, (juce::Component*) &saveToLabel,
                                (juce::Component*) &saveToPath, (juce::Component*) &saveToButton,
                                (juce::Component*) &listenButton, (juce::Component*) &recordButton,
                                (juce::Component*) &findButton, (juce::Component*) &keyboard,
                                (juce::Component*) &status })
        addAndMakeVisible (c);

    scanButton.onClick = [this] { scanPlugins(); };
    scanButton.setTooltip ("Looks through your plugin folders for instruments. Each plugin is opened in a separate "
                           "process, so one that crashes is just skipped.");
    pluginSearch.setTextToShowWhenEmpty ("Search plugins", juce::Colours::grey);
    pluginSearch.setJustification (juce::Justification::centredLeft);
    pluginSearch.onTextChange = [this] { refreshPluginChoices(); };
    pluginSearch.onReturnKey = [this] { pluginChoice.showPopup(); };
    loadButton.onClick = [this] { loadSelectedPlugin(); };
    showButton.onClick = [this] { showPluginWindow(); };
    showButton.setTooltip ("Opens the plugin's own window, to pick the sound you want to record.");

    libraryName.onTextChange = [this]
    {
        map.libraryName = libraryName.getText();
        saveMap();
    };
    addKeysButton.onClick = [this] { addGroup (syn::Group::keys()); };
    addKeysButton.setTooltip ("Adds a keyboard range: a recording every few keys, pitched to fill the keys between.");
    addDrumButton.onClick = [this] { addGroup (syn::Group::drum ("Drum", 38)); };
    addDrumButton.setTooltip ("Adds a single unpitched note that plays to its end: a drum, a hit or an effect.");
    removeButton.onClick = [this] { removeGroup(); };
    findButton.onClick = [this] { findNotes(); };
    findButton.setTooltip ("Plays every key once on the selected row's MIDI channel and marks the ones that make a "
                           "sound on the keyboard below.");

    keyboardState.addListener (this);
    keyboard.setTooltip ("Click to play the plugin on the selected row's channel. Right-click a key to set it as a "
                         "row's lowest or highest note, or to add a drum on it. Amber: the selected row's keys. "
                         "Green: keys Find notes heard.");
    keyboard.onKeyMenu = [this] (int note) { showKeyMenu (note); };

    auto& header = table.getHeader();
    header.addColumn ("Record", colOn, 56);
    header.addColumn ("Name", colName, 130);
    header.addColumn ("Lowest", colLow, 64);
    header.addColumn ("Highest", colHigh, 64);
    header.addColumn ("Every", colStep, 56);
    header.addColumn ("Layers", colLayers, 56);
    header.addColumn ("Round robins", colRoundRobins, 96);
    header.addColumn ("Hold (s)", colHold, 64);
    header.addColumn ("Release (s)", colRelease, 80);
    header.addColumn ("Pitched", colPitched, 60);
    header.addColumn ("One-shot", colOneShot, 66);
    header.addColumn ("Channel", colChannel, 64);
    header.addColumn ("Recordings", colCount, 80);
    table.setRowHeight (26);
    table.setTooltip ("Lowest and Highest take note numbers or names (C4 is 60). Every: a recording every this many "
                      "keys. Layers: velocity layers. Hold: how long each note is held before its release.");

    decentSampler.setToggleState (settings->getBoolValue ("writeDecentSampler", true), juce::dontSendNotification);
    sfz.setToggleState (settings->getBoolValue ("writeSfz", true), juce::dontSendNotification);
    decentSampler.onClick = [this] { settings->setValue ("writeDecentSampler", decentSampler.getToggleState()); };
    sfz.onClick = [this] { settings->setValue ("writeSfz", sfz.getToggleState()); };

    rateChoice.addItem ("44.1 kHz", 44100);
    rateChoice.addItem ("48 kHz", 48000);
    rateChoice.addItem ("96 kHz", 96000);
    rateChoice.setSelectedId (settings->getIntValue ("sampleRate", 48000), juce::dontSendNotification);
    if (rateChoice.getSelectedId() == 0)
        rateChoice.setSelectedId (48000, juce::dontSendNotification);
    rateChoice.onChange = [this] { settings->setValue ("sampleRate", rateChoice.getSelectedId()); };
    rateChoice.setTooltip ("The sample rate of the recordings. Samples are saved as 24-bit stereo WAV files.");

    saveToButton.onClick = [this] { chooseOutputFolder(); };
    saveToPath.setMinimumHorizontalScale (0.5f);
    showOutputFolder();
    listenButton.onClick = [this] { listenToGroup(); };
    listenButton.setTooltip ("Plays the selected row's middle note, held as it will be recorded.");
    recordButton.onClick = [this] { recordLibrary(); };
    recordButton.setTooltip ("Records every ticked row into Samples, then writes the library's presets.");
    status.setJustificationType (juce::Justification::topLeft);

    if (auto xml = settings->getXmlValue ("sampleMap"))
        map = syn::SampleMap::fromValueTree (juce::ValueTree::fromXml (*xml));
    else
        map.groups.push_back (syn::Group::keys());
    libraryName.setText (map.libraryName, juce::dontSendNotification);
    refreshTable();

    refreshPluginChoices();
    setStatus (knownPlugins.getNumTypes() == 0 ? "Find your instrument plugins to start."
                                               : "Pick a plugin and load it.");
    setSize (1100, 740);
}

MainComponent::~MainComponent()
{
    setLive (false);
    knownPlugins.removeChangeListener (this);
    keyboardState.removeListener (this);
    audioDevices.removeMidiInputDeviceCallback ({}, &live);
    audioDevices.removeAudioCallback (&live);
    pluginWindow = nullptr;
    plugin = nullptr;
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (10);
    const int rowHeight = 28, gap = 8;

    auto row = area.removeFromTop (rowHeight);
    scanButton.setBounds (row.removeFromLeft (110));
    row.removeFromLeft (gap);
    pluginSearch.setBounds (row.removeFromLeft (160));
    row.removeFromLeft (gap);
    showButton.setBounds (row.removeFromRight (110));
    row.removeFromRight (gap);
    loadButton.setBounds (row.removeFromRight (110));
    row.removeFromRight (gap);
    pluginChoice.setBounds (row);
    area.removeFromTop (gap);

    row = area.removeFromTop (rowHeight);
    nameLabel.setBounds (row.removeFromLeft (100));
    libraryName.setBounds (row.removeFromLeft (260));
    row.removeFromLeft (gap);
    removeButton.setBounds (row.removeFromRight (90));
    row.removeFromRight (gap);
    findButton.setBounds (row.removeFromRight (100));
    row.removeFromRight (gap * 3);
    addDrumButton.setBounds (row.removeFromRight (90));
    row.removeFromRight (gap);
    addKeysButton.setBounds (row.removeFromRight (90));
    area.removeFromTop (gap);

    status.setBounds (area.removeFromBottom (44));
    area.removeFromBottom (gap);
    keyboard.setBounds (area.removeFromBottom (80));
    area.removeFromBottom (gap);
    row = area.removeFromBottom (rowHeight);
    recordButton.setBounds (row.removeFromRight (140));
    row.removeFromRight (gap);
    listenButton.setBounds (row.removeFromRight (90));
    row.removeFromRight (gap * 2);
    saveToLabel.setBounds (row.removeFromLeft (60));
    saveToButton.setBounds (row.removeFromRight (90));
    row.removeFromRight (gap);
    saveToPath.setBounds (row);
    area.removeFromBottom (gap);
    row = area.removeFromBottom (rowHeight);
    decentSampler.setBounds (row.removeFromLeft (210));
    sfz.setBounds (row.removeFromLeft (110));
    rateChoice.setBounds (row.removeFromLeft (110));
    row.removeFromLeft (gap * 2);
    totalLabel.setBounds (row);
    area.removeFromBottom (gap);

    table.setBounds (area);
}

void MainComponent::setStatus (const juce::String& text)
{
    status.setText (text, juce::dontSendNotification);
}

double MainComponent::sampleRate() const
{
    return (double) juce::jmax (44100, rateChoice.getSelectedId());
}

// --- plugins -------------------------------------------------------------------------------

void MainComponent::refreshPluginChoices()
{
    // Rebuilt after every scan step and search keystroke: keep the user's pick (or the last plugin
    // loaded) selected. Instruments come first; anything else is listed below them, in case a
    // plugin doesn't call itself an instrument.
    auto keep = pluginChoice.getText();
    if (keep.isEmpty())
        keep = settings->getValue ("lastPlugin");
    const auto words = juce::StringArray::fromTokens (pluginSearch.getText().toLowerCase(), true);
    auto matches = [&words] (const juce::PluginDescription& d)
    {
        const auto text = (d.name + " " + d.manufacturerName + " " + d.pluginFormatName).toLowerCase();
        for (const auto& w : words)
            if (w.isNotEmpty() && ! text.contains (w))
                return false;
        return true;
    };

    pluginChoice.clear (juce::dontSendNotification);
    int id = 1, shown = 0, lastId = 0;
    const auto types = knownPlugins.getTypes();
    for (const bool instruments : { true, false })
    {
        bool headed = instruments;
        for (const auto& d : types)
            if (d.isInstrument == instruments && matches (d))
            {
                if (! headed)
                {
                    pluginChoice.addSectionHeading ("Other plugins");
                    headed = true;
                }
                const auto name = pluginLabel (d);
                pluginChoice.addItem (name, id);
                if (name == keep)
                    pluginChoice.setSelectedId (id, juce::dontSendNotification);
                lastId = id++;
                ++shown;
            }
    }
    if (shown == 1)
        pluginChoice.setSelectedId (lastId, juce::dontSendNotification);
    pluginChoice.setTextWhenNothingSelected (types.isEmpty() ? "Find plugins first"
                                             : shown == 0  ? "No plugin matches the search"
                                                           : juce::String (shown) + " plugins");
}

juce::String MainComponent::pluginLabel (const juce::PluginDescription& d)
{
    return d.name + " (" + d.pluginFormatName + ")";
}

juce::File MainComponent::scanCrashFile() const
{
    return settings->getFile().getSiblingFile ("SYNChestra-scan-crash.txt");
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // Saved after every plugin the scan finds, so a crash later in the scan loses nothing.
    if (auto xml = knownPlugins.createXml())
        settings->setValue ("knownPlugins", xml.get());
    settings->saveIfNeeded();
    refreshPluginChoices();
}

void MainComponent::scanPlugins()
{
    auto* list = new juce::PluginListComponent (formats, knownPlugins, scanCrashFile(), settings.get(), true);
    list->setSize (700, 500);

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (list);
    o.dialogTitle = "Find plugins";
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.launchAsync();

    setStatus ("Scan your plugin folders, then close the window and pick a plugin.");
}

void MainComponent::loadSelectedPlugin()
{
    const auto chosen = pluginChoice.getText();
    juce::PluginDescription desc;
    bool found = false;
    for (const auto& d : knownPlugins.getTypes())
        if (pluginLabel (d) == chosen)
        {
            desc = d;
            found = true;
        }
    if (chosen.isEmpty() || ! found)
        return setStatus ("Pick a plugin first.");
    settings->setValue ("lastPlugin", chosen);

    setLive (false);
    pluginWindow = nullptr;
    plugin = nullptr;
    juce::String error;
    plugin = formats.createPluginInstance (desc, sampleRate(), blockSize, error);
    if (plugin == nullptr)
        return setStatus ("Could not load the plugin: " + error);
    plugin->prepareToPlay (sampleRate(), blockSize);
    showPluginWindow();
    setLive (true);
    setStatus ("Loaded " + plugin->getName() + ". Pick a sound in its window, set the rows below, then Listen "
               "to check a row and Record library.");
}

void MainComponent::showPluginWindow()
{
    if (plugin == nullptr)
        return setStatus ("Load a plugin first.");
    if (pluginWindow == nullptr)
    {
        // Some plugins crash if they're asked to play while their window is being built, and may
        // change their outputs when it opens: pause them for it, and prepare them again afterwards.
        const bool wasLive = live.getProcessor() != nullptr;
        setLive (false);
        if (auto* editor = plugin->createEditorIfNeeded())
            pluginWindow = std::make_unique<PluginWindow> (editor, plugin->getName());
        if (wasLive)
            setLive (true);
    }
    if (pluginWindow != nullptr)
        pluginWindow->setVisible (true);
}

void MainComponent::setLive (bool on)
{
    // The plugin runs on the sound card whenever it's loaded, except while it renders offline.
    if (on && plugin != nullptr)
    {
        live.setProcessor (plugin.get());
        return;
    }
    if (live.getProcessor() != nullptr)
    {
        live.setProcessor (nullptr);
        if (plugin != nullptr)
            plugin->prepareToPlay (sampleRate(), blockSize);
    }
}

void MainComponent::enableMidiInputs()
{
    // Every connected MIDI input plays the loaded plugin, like a track in a DAW.
    audioDevices.removeMidiInputDeviceCallback ({}, &live);
    for (const auto& device : juce::MidiInput::getAvailableDevices())
        audioDevices.setMidiInputDeviceEnabled (device.identifier, true);
    audioDevices.addMidiInputDeviceCallback ({}, &live);
}

// --- groups ----------------------------------------------------------------------------------

void MainComponent::saveMap()
{
    if (auto xml = map.toValueTree().createXml())
        settings->setValue ("sampleMap", xml.get());
}

void MainComponent::refreshTable()
{
    table.updateContent();
    table.repaint();
    const int total = map.totalRecordings();
    totalLabel.setText (juce::String (total) + (total == 1 ? " recording" : " recordings"), juce::dontSendNotification);
    updateKeyboard();
}

void MainComponent::addGroup (syn::Group group)
{
    // A new row picks up after the last one, so adding a few drums doesn't stack them on one key.
    if (! map.groups.empty() && ! group.pitched)
    {
        const auto& last = map.groups.back();
        group.lowNote = group.highNote = juce::jmin (127, juce::jmax (last.lowNote, last.highNote) + (last.pitched ? 12 : 1));
    }
    map.groups.push_back (group);
    saveMap();
    refreshTable();
    table.selectRow ((int) map.groups.size() - 1);
}

void MainComponent::removeGroup()
{
    const int row = table.getSelectedRow();
    if (! juce::isPositiveAndBelow (row, (int) map.groups.size()))
        return setStatus ("Select a row to remove.");
    map.groups.erase (map.groups.begin() + row);
    saveMap();
    refreshTable();
}

void MainComponent::listenToGroup()
{
    const int row = table.getSelectedRow();
    if (plugin == nullptr)
        return setStatus ("Load a plugin first.");
    if (! juce::isPositiveAndBelow (row, (int) map.groups.size()))
        return setStatus ("Select a row to listen to.");
    const auto& group = map.groups[(size_t) row];
    const auto notes = group.sampledNotes();
    const int note = notes[notes.size() / 2];
    const int channel = juce::jlimit (1, 16, group.midiChannel);
    live.play (juce::MidiMessage::noteOn (channel, note, (juce::uint8) 100));
    juce::Timer::callAfterDelay (juce::jmax (10, (int) (group.holdSeconds * 1000.0)),
                                 [safe = juce::Component::SafePointer<MainComponent> (this), channel, note]
    {
        if (safe != nullptr)
            safe->live.play (juce::MidiMessage::noteOff (channel, note));
    });
    setStatus ("Playing " + group.name + " " + syn::noteName (note) + " at velocity 100.");
}

// --- recording -------------------------------------------------------------------------------

juce::File MainComponent::outputFolder() const
{
    const auto chosen = settings->getValue ("outputFolder");
    return chosen.isNotEmpty() && juce::File::isAbsolutePath (chosen) ? juce::File (chosen) : defaultOutputFolder();
}

void MainComponent::chooseOutputFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Where should libraries go?", outputFolder());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
    {
        const auto folder = fc.getResult();
        if (folder == juce::File())
            return;
        if (! folder.createDirectory() || ! folder.hasWriteAccess())
            return setStatus ("Can't write to " + folder.getFullPathName() + ".");
        settings->setValue ("outputFolder", folder.getFullPathName());
        showOutputFolder();
    });
}

void MainComponent::showOutputFolder()
{
    saveToPath.setText (outputFolder().getFullPathName(), juce::dontSendNotification);
    saveToPath.setTooltip (outputFolder().getFullPathName());
}

bool MainComponent::runJob (const juce::String& title,
                            std::function<juce::String (juce::ThreadWithProgressWindow&)> work,
                            std::function<void (const juce::String&)> done)
{
    if (job != nullptr)
        return false;
    setLive (false);
    for (auto* b : { &recordButton, &listenButton, &loadButton, &findButton })
        b->setEnabled (false);
    job = std::make_unique<Job> (title, std::move (work), [this, done] (const juce::String& report)
    {
        if (plugin != nullptr)
            plugin->prepareToPlay (sampleRate(), blockSize);
        setLive (true);
        for (auto* b : { &recordButton, &listenButton, &loadButton, &findButton })
            b->setEnabled (true);
        done (report);
        // The job can't delete itself from inside its own callback.
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)]
        {
            if (safe != nullptr)
                safe->job = nullptr;
        });
    });
    job->launchThread();
    return true;
}

void MainComponent::recordLibrary()
{
    if (plugin == nullptr)
        return setStatus ("Load a plugin first.");
    if (map.totalRecordings() == 0)
        return setStatus ("Tick Record on at least one row.");
    if (! decentSampler.getToggleState() && ! sfz.getToggleState())
        return setStatus ("Tick Decent Sampler, SFZ or both, so the library has a preset to open.");

    const auto folder = outputFolder().getChildFile (syn::LibraryRecorder::safeName (map.libraryName));
    const auto toRecord = map;
    const bool writeDs = decentSampler.getToggleState(), writeSfz = sfz.getToggleState();
    syn::NoteRenderer::Settings render;
    render.sampleRate = sampleRate();
    render.blockSize = blockSize;
    auto* instrument = plugin.get();

    runJob ("Recording " + map.libraryName, [instrument, toRecord, render, folder, writeDs, writeSfz]
                                            (juce::ThreadWithProgressWindow& window)
    {
        juce::String report;
        const auto library = syn::LibraryRecorder::record (*instrument, toRecord, render, folder,
                                                           [&window] (const syn::LibraryRecorder::Progress& p)
        {
            window.setProgress (p.total > 0 ? (double) p.done / p.total : 0.0);
            window.setStatusMessage (p.current);
            return ! window.threadShouldExit();
        }, report);
        if (library.samples.empty())
            return report;
        const auto error = library.writePresets (folder, writeSfz, writeDs);
        return error.isNotEmpty() ? report + " " + error
                                  : report + " Saved in " + folder.getFullPathName() + ".";
    },
    [this, folder] (const juce::String& report)
    {
        setStatus (report);
        if (folder.isDirectory())
            folder.revealToUser();
    });
}

// --- keyboard ------------------------------------------------------------------------------

int MainComponent::selectedChannel() const
{
    const int row = table.getSelectedRow();
    return juce::isPositiveAndBelow (row, (int) map.groups.size()) ? juce::jlimit (1, 16, map.groups[(size_t) row].midiChannel)
                                                                 : 1;
}

void MainComponent::updateKeyboard()
{
    const int row = table.getSelectedRow();
    if (juce::isPositiveAndBelow (row, (int) map.groups.size()))
    {
        const auto& g = map.groups[(size_t) row];
        keyboard.showRow ({ juce::jmin (g.lowNote, g.highNote), juce::jmax (g.lowNote, g.highNote) });
    }
    else
    {
        keyboard.showRow ({ -1, -1 });
    }
}

void MainComponent::selectedRowsChanged (int)
{
    updateKeyboard();
}

void MainComponent::handleNoteOn (juce::MidiKeyboardState*, int, int note, float velocity)
{
    // The keyboard's own channel is ignored: it plays on the selected row's, as recording will.
    live.play (juce::MidiMessage::noteOn (selectedChannel(), note, velocity));
}

void MainComponent::handleNoteOff (juce::MidiKeyboardState*, int, int note, float)
{
    live.play (juce::MidiMessage::noteOff (selectedChannel(), note));
}

void MainComponent::showKeyMenu (int note)
{
    const int row = table.getSelectedRow();
    const bool haveRow = juce::isPositiveAndBelow (row, (int) map.groups.size());
    const auto rowName = haveRow ? map.groups[(size_t) row].name : juce::String();
    const auto name = syn::noteName (note);

    juce::PopupMenu menu;
    menu.addSectionHeader (name + " (" + juce::String (note) + ")");
    menu.addItem (1, "Set as lowest note of " + (haveRow ? rowName : "the selected row"), haveRow);
    menu.addItem (2, "Set as highest note of " + (haveRow ? rowName : "the selected row"), haveRow);
    menu.addItem (3, "Add a drum on " + name);
    // The run of sounding keys around this one, for a whole instrument in one click.
    int low = note, high = note;
    if (keyboard.sounding.count (note) > 0)
    {
        while (keyboard.sounding.count (low - 1) > 0) --low;
        while (keyboard.sounding.count (high + 1) > 0) ++high;
        if (high > low)
            menu.addItem (4, "Set " + (haveRow ? rowName : "the selected row") + " to the sounding keys "
                                 + syn::noteName (low) + " to " + syn::noteName (high), haveRow);
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                        [safe = juce::Component::SafePointer<MainComponent> (this), row, note, low, high, name] (int choice)
    {
        if (safe == nullptr || choice == 0)
            return;
        auto& self = *safe;
        if (choice == 3)
        {
            auto drum = syn::Group::drum (name, note);
            drum.midiChannel = self.selectedChannel();
            self.map.groups.push_back (drum);
            self.saveMap();
            self.refreshTable();
            self.table.selectRow ((int) self.map.groups.size() - 1);
            return;
        }
        if (! juce::isPositiveAndBelow (row, (int) self.map.groups.size()))
            return;
        auto& g = self.map.groups[(size_t) row];
        if (choice == 1) g.lowNote = note;
        if (choice == 2) g.highNote = note;
        if (choice == 4)
        {
            g.lowNote = low;
            g.highNote = high;
        }
        self.saveMap();
        self.refreshTable();
    });
}

void MainComponent::findNotes()
{
    if (plugin == nullptr)
        return setStatus ("Load a plugin first.");
    syn::NoteRenderer::Settings render;
    render.sampleRate = sampleRate();
    render.blockSize = blockSize;
    const int channel = selectedChannel();
    auto* instrument = plugin.get();
    auto found = std::make_shared<juce::Array<int>>();

    runJob ("Finding notes", [instrument, render, channel, found] (juce::ThreadWithProgressWindow& window)
    {
        *found = syn::LibraryRecorder::findSoundingNotes (*instrument, render, channel, [&window] (int note)
        {
            window.setProgress (note / 128.0);
            window.setStatusMessage ("Trying " + syn::noteName (note));
            return ! window.threadShouldExit();
        });
        return juce::String();
    },
    [this, found, channel] (const juce::String&)
    {
        keyboard.showFound (std::set<int> (found->begin(), found->end()));
        if (found->isEmpty())
            return setStatus ("No key made a sound on channel " + juce::String (channel) + ".");
        // Describe the sounding keys as runs: "C1-C6, F7".
        juce::StringArray runs;
        for (int i = 0; i < found->size();)
        {
            int j = i;
            while (j + 1 < found->size() && (*found)[j + 1] == (*found)[j] + 1)
                ++j;
            runs.add (syn::noteName ((*found)[i]) + (j > i ? "-" + syn::noteName ((*found)[j]) : juce::String()));
            i = j + 1;
        }
        setStatus (juce::String (found->size()) + " keys make a sound on channel " + juce::String (channel) + ": "
                   + runs.joinIntoString (", ") + ". They're marked green on the keyboard; right-click one to use it.");
    });
}

// --- table -----------------------------------------------------------------------------------

int MainComponent::getNumRows()
{
    return (int) map.groups.size();
}

void MainComponent::paintRowBackground (juce::Graphics& g, int row, int, int, bool selected)
{
    if (selected)
        g.fillAll (findColour (juce::TextEditor::highlightColourId));
    else if (row % 2 == 1)
        g.fillAll (juce::Colours::white.withAlpha (0.05f));
}

void MainComponent::paintCell (juce::Graphics&, int, int, int, int, bool) {}

juce::String MainComponent::cellText (const syn::Group& g, int column) const
{
    switch (column)
    {
        case colName:        return g.name;
        case colLow:         return syn::noteName (g.lowNote);
        case colHigh:        return syn::noteName (g.highNote);
        case colStep:        return juce::String (juce::jmax (1, g.step));
        case colLayers:      return juce::String (g.velocities().size());
        case colRoundRobins: return juce::String (juce::jmax (1, g.roundRobins));
        case colHold:        return seconds (g.holdSeconds);
        case colRelease:     return seconds (g.releaseSeconds);
        case colChannel:     return juce::String (g.midiChannel);
        case colCount:       return juce::String (g.sampledNotes().size() * g.velocities().size() * juce::jmax (1, g.roundRobins));
        default:             return {};
    }
}

void MainComponent::setCell (int row, int column, const juce::String& text)
{
    if (! juce::isPositiveAndBelow (row, (int) map.groups.size()))
        return;
    auto& g = map.groups[(size_t) row];
    switch (column)
    {
        case colName:        if (text.trim().isNotEmpty()) g.name = text.trim(); break;
        case colLow:         if (const int n = parseNote (text); n >= 0) g.lowNote = n; break;
        case colHigh:        if (const int n = parseNote (text); n >= 0) g.highNote = n; break;
        case colStep:        g.step = juce::jlimit (1, 127, text.getIntValue()); break;
        case colLayers:      g.layers = juce::jlimit (1, 127, text.getIntValue()); break;
        case colRoundRobins: g.roundRobins = juce::jlimit (1, 16, text.getIntValue()); break;
        case colHold:        g.holdSeconds = juce::jlimit (0.01, 30.0, text.getDoubleValue()); break;
        case colRelease:     g.releaseSeconds = juce::jlimit (0.0, 30.0, text.getDoubleValue()); break;
        case colChannel:     g.midiChannel = juce::jlimit (1, 16, text.getIntValue()); break;
        default: break;
    }
    saveMap();
    refreshTable();
}

juce::Component* MainComponent::refreshComponentForCell (int row, int column, bool, juce::Component* existing)
{
    if (! juce::isPositiveAndBelow (row, (int) map.groups.size()))
    {
        delete existing;
        return nullptr;
    }
    const auto& g = map.groups[(size_t) row];

    if (column == colOn || column == colPitched || column == colOneShot)
    {
        auto* cell = dynamic_cast<ToggleCell*> (existing);
        if (cell == nullptr)
        {
            delete existing;
            cell = new ToggleCell();
        }
        const bool on = column == colOn ? g.enabled : column == colPitched ? g.pitched : g.oneShot;
        cell->box.setToggleState (on, juce::dontSendNotification);
        cell->box.onClick = [this, row, column, cell]
        {
            auto& group = map.groups[(size_t) row];
            const bool state = cell->box.getToggleState();
            if (column == colOn) group.enabled = state;
            if (column == colPitched) group.pitched = state;
            if (column == colOneShot) group.oneShot = state;
            saveMap();
            refreshTable();
        };
        return cell;
    }

    auto* cell = dynamic_cast<EditableCell*> (existing);
    if (cell == nullptr)
    {
        delete existing;
        cell = new EditableCell();
    }
    cell->setText (cellText (g, column), juce::dontSendNotification);
    cell->setEditable (false, column != colCount, false);
    cell->setJustificationType (column == colName ? juce::Justification::centredLeft : juce::Justification::centred);
    cell->onEdit = [this, row, column] (const juce::String& text) { setCell (row, column, text); };
    cell->onSelect = [this, row] { table.selectRow (row); };
    return cell;
}
