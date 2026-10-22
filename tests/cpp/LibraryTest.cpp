// Records a small library from a stand-in synth plugin and checks the samples and both presets,
// so the recorder can be checked without a commercial plugin. Usage: SYNChestra_tests OUTPUT_FOLDER
#include "../../Source/Core/Library.h"
#include "../../Source/Core/Project.h"
#include "../../Source/Core/LibraryPlayer.h"
#include <juce_graphics/juce_graphics.h>

namespace
{
/** Each note plays a sine at its pitch, louder with velocity, while held, then fades over
    0.2 s. The left side is louder than the right, so the test can tell stereo was kept.
    Notes from 100 up are plucks: they die away over about 3.5 s and ignore note-off.
    Soft notes (velocity 40 and under) sound brighter, with a third harmonic; louder ones are
    the same sound, only louder. C1 and C#1 (24, 25) are keyswitches, silent: after C#1 notes play at a quarter of the level, until C1.
    Its state is a patch number: patch 2 plays an octave up. A new state takes 150 blocks to "load",
    silent meanwhile, like a big Kontakt instrument. */
class FakeSynth final : public juce::AudioPluginInstance
{
public:
    FakeSynth() : AudioPluginInstance (BusesProperties().withOutput ("Out", juce::AudioChannelSet::stereo())) {}
    /** With a second stereo output, off to start, that plays the same note at a fifth of the level, centred. */
    struct MultiOut {};
    explicit FakeSynth (MultiOut)
        : AudioPluginInstance (BusesProperties().withOutput ("Out", juce::AudioChannelSet::stereo())
                                                .withOutput ("Aux", juce::AudioChannelSet::stereo(), false)) {}

    const juce::String getName() const override { return "Fake Synth"; }
    void fillInPluginDescription (juce::PluginDescription& d) const override { d.name = getName(); d.isInstrument = true; }
    void prepareToPlay (double r, int) override { rate = r; }
    void releaseResources() override {}
    double getTailLengthSeconds() const override { return 0.2; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& state) override
    {
        const auto text = "patch " + juce::String (patch);
        state.replaceAll (text.toRawUTF8(), text.getNumBytesAsUTF8());
    }
    void setStateInformation (const void* data, int size) override
    {
        patch = juce::String::fromUTF8 ((const char*) data, size).fromFirstOccurrenceOf ("patch ", false, false).getIntValue();
        loading = 150;
    }
    int patch = 7;

    using AudioPluginInstance::processBlock;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        buffer.clear();
        if (loading > 0)
        {
            --loading;
            midi.clear();
            return;
        }
        int pos = 0;
        auto render = [&] (int end)
        {
            for (; pos < end; ++pos)
            {
                if (pluck)
                    level *= std::exp (-1.0 / (0.5 * rate));
                else if (! held)
                    level *= std::exp (-1.0 / (0.03 * rate));
                if (level < 1.0e-6)
                    continue;
                const auto v = (float) (level * (std::sin (phase) + (bright ? 0.5 * std::sin (3.0 * phase) : 0.0)));
                phase += juce::MathConstants<double>::twoPi * frequency / rate;
                buffer.setSample (0, pos, v);
                buffer.setSample (1, pos, v * 0.5f);
                for (int ch = 2; ch < juce::jmin (4, buffer.getNumChannels()); ++ch)
                    buffer.setSample (ch, pos, v * 0.2f);
            }
        };
        for (const auto m : midi)
        {
            render (m.samplePosition);
            const auto msg = m.getMessage();
            if (msg.isNoteOn() && (msg.getNoteNumber() == 24 || msg.getNoteNumber() == 25))
            {
                soft = msg.getNoteNumber() == 25;
                continue;
            }
            if (msg.isNoteOn() && msg.getNoteNumber() != 61)   // C#4 plays nothing
            {
                level = msg.getFloatVelocity() * (soft ? 0.125 : 0.5);
                frequency = juce::MidiMessage::getMidiNoteInHertz (msg.getNoteNumber()) * (patch == 2 ? 2.0 : 1.0);
                held = true;
                pluck = msg.getNoteNumber() >= 100;
                bright = msg.getVelocity() <= 40;
                channels.insert (msg.getChannel());
            }
            if (msg.isNoteOff() || msg.isAllNotesOff())
                held = false;
        }
        render (buffer.getNumSamples());
    }

    std::set<int> channels;

private:
    double rate = 48000.0, level = 0.0, phase = 0.0, frequency = 440.0;
    int loading = 0;
    bool held = false, pluck = false, soft = false, bright = false;
};

int failures = 0;
void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "ok    " : "WRONG ") << what << std::endl;
    if (! ok)
        ++failures;
}

void checkMapMaths()
{
    syn::Group g;
    g.lowNote = 21;
    g.highNote = 31;
    g.step = 4;
    check (g.sampledNotes() == juce::Array<int> ({ 21, 25, 29, 31 }), "notes every step, and always the top one");
    check (g.keyRangeFor (0) == juce::Range<int> (21, 23), "lowest sample covers from the bottom key");
    check (g.keyRangeFor (1) == juce::Range<int> (24, 27), "middle sample covers halfway to each neighbour");
    check (g.keyRangeFor (3) == juce::Range<int> (31, 31), "top sample covers up to the top key");
    g.layers = 4;
    check (g.velocities() == juce::Array<int> ({ 32, 64, 95, 127 }), "velocity layers spread up to 127");
    check (g.velocityRangeFor (0) == juce::Range<int> (1, 32) && g.velocityRangeFor (2) == juce::Range<int> (65, 95),
           "velocity ranges meet without gaps");
    const auto drum = syn::Group::drum ("Snare", 38);
    check (drum.keyRangeFor (0) == juce::Range<int> (38, 38) && ! drum.pitched, "a drum plays on its own key only");
    check (syn::noteName (60) == "C4" && syn::noteName (61) == "C#4", "note names use C4 for 60");

    const auto found = syn::Group::fromSoundingNotes ({ 36, 37, 38, 39, 40, 41, 50, 51, 70 }, false, 2);
    check (found.size() == 4 && found[0].pitched && found[0].lowNote == 36 && found[0].highNote == 41
               && found[0].midiChannel == 2 && ! found[1].pitched && found[1].lowNote == 50 && found[3].lowNote == 70,
           "found keys become a key row per run, and drums for lone keys");
    check (syn::Group::fromSoundingNotes ({ 36, 37, 38, 39, 40 }, true, 10).size() == 5, "as drums, every key gets a row");

    syn::SampleMap m;
    m.libraryName = "Saved";
    m.groups = { g, drum };
    const auto back = syn::SampleMap::fromValueTree (m.toValueTree());
    check (back.libraryName == "Saved" && back.groups.size() == 2 && back.groups[1].name == "Snare"
               && back.groups[0].step == 4 && back.groups[1].midiChannel == 10,
           "a map survives saving and loading");
}

int countOf (const juce::String& text, const juce::String& part)
{
    int n = 0;
    for (int at = text.indexOf (part); at >= 0; at = text.indexOf (at + 1, part))
        ++n;
    return n;
}
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juce;
    if (argc < 2)
    {
        std::cout << "Usage: SYNChestra_tests OUTPUT_FOLDER" << std::endl;
        return 2;
    }
    checkMapMaths();

    const juce::File folder = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]).getChildFile ("Test Synth");
    folder.deleteRecursively();

    syn::SampleMap map;
    map.libraryName = "Test Synth";
    map.skipSameLayers = false;   // the synth's layers differ only in level: keep them all here
    syn::Group keys;
    keys.name = "Lead";
    keys.lowNote = 57;
    keys.highNote = 63;
    keys.step = 2;            // 57, 59, 61 (silent), 63
    keys.layers = 2;
    keys.roundRobins = 2;
    keys.holdSeconds = 0.5;
    map.groups.push_back (keys);
    auto hit = syn::Group::drum ("Hit", 40);
    hit.layers = 1;
    hit.roundRobins = 1;
    map.groups.push_back (hit);
    auto off = syn::Group::drum ("Off", 45);
    off.enabled = false;
    map.groups.push_back (off);

    FakeSynth synth;
    juce::String report;
    int calls = 0;
    const auto library = syn::LibraryRecorder::record (synth, map, {}, folder,
                                                       [&calls] (const syn::LibraryRecorder::Progress&) { ++calls; return true; },
                                                       report);
    std::cout << report << std::endl;
    check (calls == 4 * 2 * 2 + 1, "progress for every recording of the enabled groups");
    check (library.samples.size() == 3 * 2 * 2 + 1, "every sounding note recorded, the silent one left out");
    check (report.contains ("Lead C#4"), "the report names the silent note");
    check (synth.channels.count (10) == 1 && synth.channels.count (1) == 1, "each group plays on its own MIDI channel");

    const auto first = folder.getChildFile (library.samples.front().path);
    check (first.existsAsFile() && first.getFullPathName().contains ("Samples/Lead"), "samples go in Samples/<group>");
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    if (std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (first) })
    {
        check (reader->numChannels == 2 && reader->bitsPerSample == 24 && juce::approximatelyEqual (reader->sampleRate, 48000.0),
               "samples are 24-bit stereo at 48 kHz");
        const double seconds = (double) reader->lengthInSamples / reader->sampleRate;
        check (seconds > 0.5 && seconds < 1.5, "a sample holds the note and its tail (" + juce::String (seconds, 2) + " s)");
        juce::AudioBuffer<float> audio (2, (int) reader->lengthInSamples);
        reader->read (&audio, 0, audio.getNumSamples(), 0, true, true);
        const float left = audio.getRMSLevel (0, 0, audio.getNumSamples());
        const float right = audio.getRMSLevel (1, 0, audio.getNumSamples());
        check (right > 0.4f * left && right < 0.6f * left, "the stereo image is kept");
        check (std::abs (audio.getSample (0, audio.getNumSamples() - 1)) < 0.001f, "the end fades out without a click");
    }
    else
    {
        check (false, "the first sample can be read");
    }

    // Trying the library: the player plays it from MIDI as a sampler would.
    {
        syn::LibraryPlayer player;
        check (player.load (library, folder, 48000.0).isEmpty() && player.isLoaded(), "the player loads the library");
        auto play = [&player] (std::vector<juce::MidiMessage> messages, double seconds)
        {
            juce::AudioBuffer<float> out (2, (int) (seconds * 48000.0));
            out.clear();
            juce::MidiBuffer midi;
            for (const auto& m : messages)
                midi.addEvent (m, 0);
            player.render (out, midi);
            return out;
        };
        const auto lead = play ({ juce::MidiMessage::noteOn (1, 59, (juce::uint8) 127) }, 0.2);
        const float left = lead.getRMSLevel (0, 0, lead.getNumSamples()), right = lead.getRMSLevel (1, 0, lead.getNumSamples());
        check (left > 0.1f && right > 0.4f * left && right < 0.6f * left, "a key plays its recording, in stereo");
        const auto released = play ({ juce::MidiMessage::noteOff (1, 59) }, 1.0);
        check (released.getMagnitude (0, released.getNumSamples() - 4800, 4800) < 0.001f, "key-up fades it out");
        const auto between = play ({ juce::MidiMessage::noteOn (1, 58, (juce::uint8) 127) }, 0.2);
        check (between.getRMSLevel (0, 0, between.getNumSamples()) > 0.1f, "a key between recordings plays a pitched neighbour");
        player.allNotesOff();
        const auto soft = play ({ juce::MidiMessage::noteOn (1, 59, (juce::uint8) 40) }, 0.2);
        check (soft.getRMSLevel (0, 0, soft.getNumSamples()) < 0.5f * left, "a soft key plays softer");
        player.allNotesOff();
        check (play ({ juce::MidiMessage::noteOn (10, 41, (juce::uint8) 127) }, 0.2).getMagnitude (0, 0, 9600) < 1.0e-6f,
               "a key no row covers plays nothing");
        // With a release so short that key-up would silence it at once, a one-shot still plays on.
        auto quickRelease = library;
        quickRelease.map.groups[1].releaseSeconds = 0.001;
        player.load (quickRelease, folder, 48000.0);
        play ({ juce::MidiMessage::noteOn (10, 40, (juce::uint8) 127) }, 0.05);
        const auto drumOn = play ({ juce::MidiMessage::noteOff (10, 40) }, 0.02);
        check (drumOn.getMagnitude (0, 480, 480) > 0.1f, "a one-shot drum plays on after key-up");
        quickRelease.map.groups[1].oneShot = false;
        player.load (quickRelease, folder, 48000.0);
        play ({ juce::MidiMessage::noteOn (10, 40, (juce::uint8) 127) }, 0.05);
        const auto drumOff = play ({ juce::MidiMessage::noteOff (10, 40) }, 0.02);
        check (drumOff.getMagnitude (0, 480, 480) < 1.0e-6f, "a key that isn't a one-shot stops after its release");
    }

    // A multi-out plugin: its second output is off until turned on, then recorded on its own.
    {
        FakeSynth multi { FakeSynth::MultiOut {} };
        check (syn::NoteRenderer::outputsOf (multi).size() == 1, "an output that's off isn't offered");
        multi.enableAllBuses();
        const auto outputs = syn::NoteRenderer::outputsOf (multi);
        check (outputs.size() == 2 && outputs[1].firstChannel == 2 && outputs[1].name == "Aux"
                   && outputs[0].name.startsWith ("Main"),
               "each stereo output is offered by name");
        syn::NoteRenderer::Settings mainOut, auxOut;
        auxOut.outputChannel = 2;
        const auto fromMain = syn::NoteRenderer (multi, mainOut).render (60, 127, 0.3);
        const auto fromAux = syn::NoteRenderer (multi, auxOut).render (60, 127, 0.3);
        const float mainLeft = fromMain.getRMSLevel (0, 0, fromMain.getNumSamples());
        const float auxLeft = fromAux.getRMSLevel (0, 0, fromAux.getNumSamples());
        const float auxRight = fromAux.getRMSLevel (1, 0, fromAux.getNumSamples());
        check (auxLeft > 0.15f * mainLeft && auxLeft < 0.25f * mainLeft && std::abs (auxLeft - auxRight) < 0.01f * auxLeft,
               "the chosen output is the one recorded");
    }

    // A small picture for the Decent Sampler panel, which writePresets scales to fill it.
    const auto picture = folder.getSiblingFile ("picture.png");
    {
        juce::Image image (juce::Image::RGB, 100, 50, true);
        image.clear (image.getBounds(), juce::Colours::orange);
        picture.deleteFile();
        juce::FileOutputStream out (picture);
        juce::PNGImageFormat().writeImageToStream (image, out);
    }
    auto withPicture = library;
    withPicture.map.backgroundImage = picture.getFullPathName();
    withPicture.map.floeIcon = picture.getFullPathName();
    withPicture.map.panel.floeVignette = juce::Colour (0x66000000);
    withPicture.map.panel.floeTint = juce::Colour (0x0D4FD6FF);
    check (withPicture.writePresets (folder, true, true, true).isEmpty(), "presets written");
    const auto background = juce::ImageFileFormat::loadFrom (folder.getChildFile ("Images/background.png"));
    check (background.getWidth() == 812 && background.getHeight() == 375, "the background picture is scaled to the panel");
    const auto floeCover = juce::ImageFileFormat::loadFrom (folder.getChildFile ("Images/floe-background.jpg"));
    check (floeCover.getWidth() == 1500 && floeCover.getHeight() == 844, "Floe gets a 16:9 background picture");
    const auto floeIcon = juce::ImageFileFormat::loadFrom (folder.getChildFile ("Images/floe-icon.png"));
    check (floeIcon.getWidth() == 256 && floeIcon.getHeight() == 256, "Floe gets a square icon");
    const auto floe = folder.getChildFile ("Test Synth.floe.lua").loadFileAsString();
    check (countOf (floe, "floe.add_region(") == 13 && countOf (floe, "floe.new_instrument(") == 2,
           "the .floe.lua has a region per sample and an instrument per row");
    check (floe.contains ("key_range = { 57, 59 }") && floe.contains ("velocity_range_high_resolution = { 0, 507 }"),
           "the .floe.lua maps keys and velocities, ends exclusive");
    check (floe.contains ("round_robin_index = 1,") && floe.contains ("keytrack_requirement = \"never\"")
               && floe.contains ("background_image_path = \"Images/floe-background.jpg\"")
               && floe.contains ("icon_image_path = \"Images/floe-icon.png\"")
               && floe.contains ("vignette = { colour = 0x00000066 }") && floe.contains ("panel_tint = { colour = 0x4FD6FF0D }")
               && floe.trimEnd().endsWith ("return library"),
           "the .floe.lua plays round robins, the drum unpitched, over the picture");
    {
        auto twins = library;
        twins.map.groups[1].name = twins.map.groups[0].name;
        check (twins.toFloe ("Me").contains ("name = \"" + twins.map.groups[0].name + " 2\""),
               "rows with the same name become differently named Floe instruments");
        check (syn::Library().toFloe ({}).contains ("author = \"SYNChestra\"") && ! syn::Library().toFloe ({}).contains ("background_overlay"),
               "a Floe library always has an author, and Floe's own look unless colours are picked");
        const auto kept = syn::SampleMap::fromValueTree (withPicture.map.toValueTree());
        check (kept.floeIcon == withPicture.map.floeIcon && kept.panel.floeTint == withPicture.map.panel.floeTint
                   && kept.panel.floeVignette == withPicture.map.panel.floeVignette,
               "the Floe icon and colours are saved with the project");
    }
    const auto sfz = folder.getChildFile ("Test Synth.sfz").loadFileAsString();
    check (countOf (sfz, "<region>") == 13, "the .sfz has a region per sample");
    check (sfz.contains ("lokey=57 hikey=58 lovel=1 hivel=64 pitch_keycenter=57"), "the .sfz maps keys and velocities");
    check (sfz.contains ("lokey=62 hikey=63"), "the top sample covers the keys the silent one left");
    check (sfz.contains ("seq_length=2") && sfz.contains ("seq_position=2 sample=Samples/Lead/"), "the .sfz plays round robins");
    check (sfz.contains ("pitch_keytrack=0 loop_mode=one_shot"), "the drum plays unpitched to its end");

    const auto ds = juce::parseXML (folder.getChildFile ("Test Synth.dspreset"));
    check (ds != nullptr && ds->hasTagName ("DecentSampler"), "the .dspreset is Decent Sampler XML");
    if (ds != nullptr)
    {
        auto* groups = ds->getChildByName ("groups");
        check (groups != nullptr && groups->getNumChildElements() == 2, "one Decent Sampler group per recorded group");
        if (groups != nullptr && groups->getNumChildElements() == 2)
        {
            auto* lead = groups->getChildElement (0);
            check (lead->getStringAttribute ("seqMode") == "round_robin" && lead->getNumChildElements() == 12,
                   "the lead group has its samples and round robins");
            auto* sample = lead->getChildElement (0);
            check (sample->getIntAttribute ("rootNote") == 57 && sample->getIntAttribute ("loNote") == 57
                       && sample->getIntAttribute ("hiNote") == 58 && sample->getStringAttribute ("path").startsWith ("Samples/Lead/")
                       && folder.getChildFile (sample->getStringAttribute ("path")).existsAsFile(),
                   "Decent Sampler samples point at the recorded files");
            auto* drum = groups->getChildElement (1)->getChildElement (0);
            check (ds->getChildByName ("ui")->getStringAttribute ("bgImage") == "Images/background.png",
                   "the panel shows the background picture");
            auto* effects = ds->getChildByName ("effects");
            check (effects != nullptr && effects->getNumChildElements() == 4
                       && effects->getChildElement (3)->getStringAttribute ("type") == "reverb",
                   "the library has filter, chorus, delay and reverb");
            bool reverbKnob = false;
            for (auto* knob : ds->getChildByName ("ui")->getChildByName ("tab")->getChildWithTagNameIterator ("labeled-knob"))
                if (auto* b = knob->getChildByName ("binding"))
                    reverbKnob = reverbKnob || (b->getStringAttribute ("parameter") == "FX_REVERB_WET_LEVEL"
                                                && b->getIntAttribute ("position") == 3);
            check (reverbKnob, "the Reverb knob controls the reverb");
            check (drum->getIntAttribute ("pitchKeyTrack", 1) == 0 && drum->getIntAttribute ("loNote") == 40
                       && drum->getIntAttribute ("hiNote") == 40,
                   "the drum sits on its own key, unpitched");
        }
    }

    auto plain = library;
    plain.map.effects = false;
    check (! plain.toDecentSampler().contains ("<effects") && ! plain.toDecentSampler().contains ("bgImage"),
           "effects and background can be left out");

    // A panel laid out in the editor: its own places, names and colours, in both formats.
    {
        auto laidOut = library;
        auto& panel = laidOut.map.panel;
        panel.background = juce::Colour (0xFF102030);
        panel.knobColour = juce::Colour (0xFFFF0000);
        auto title = syn::Panel::standard (syn::PanelControl::title);
        title.label = "Big Strings";
        auto volume = syn::Panel::standard (syn::PanelControl::volume);
        volume.label = "Level";
        volume.bounds = { 400, 150, 120, 120 };
        auto reverb = syn::Panel::standard (syn::PanelControl::reverb);
        reverb.setStyle (syn::PanelControl::verticalFader);
        panel.controls = { title, volume, reverb };
        const auto laidOutDs = laidOut.toDecentSampler();
        check (laidOutDs.contains ("x=\"400\" y=\"150\" width=\"120\" height=\"120\" label=\"Level\""),
               "a knob goes where it was put, with its new name");
        check (laidOutDs.contains ("bgColor=\"FF102030\"") && laidOutDs.contains ("trackForegroundColor=\"FFFF0000\"")
                   && laidOutDs.contains ("text=\"Big Strings\""),
               "the panel's colours and title are written");
        check (! laidOutDs.contains ("ENV_RELEASE") && laidOutDs.contains ("FX_REVERB_WET_LEVEL") && laidOutDs.contains ("<effects"),
               "only the knobs on the panel are written, with the effects they need");
        check (laidOutDs.contains ("label=\"Reverb\" style=\"linear_vertical\"") && countOf (laidOutDs, "style=") == 1,
               "a fader is written as one, and knobs stay knobs");
        laidOut.map.effects = false;
        check (! laidOut.toDecentSampler().contains ("<effects"), "unticking Effect knobs leaves their knobs out");
        const auto laidOutSfz = laidOut.toSfz();
        check (laidOutSfz.contains ("label_cc7=Level") && ! laidOutSfz.contains ("label_cc72") && laidOutSfz.contains ("amplitude_oncc7=100"),
               "the .sfz names the Volume knob as on the panel");
        const auto back = syn::SampleMap::fromValueTree (laidOut.map.toValueTree());
        check (back.panel.controls.size() == 3 && back.panel.controls[1].bounds == volume.bounds
                   && back.panel.controls[1].label == "Level" && back.panel.knobColour == panel.knobColour
                   && back.panel.controls[2].style == syn::PanelControl::verticalFader,
               "the panel is saved with the map");
        check (syn::SampleMap().panel.layout (true, true).size() == 9, "the standard layout has the title, 7 knobs and the menu");
    }

    // A project keeps the rows, the plugin and its settings.
    {
        syn::Project project;
        project.map = map;
        juce::PluginDescription description;
        synth.fillInPluginDescription (description);
        description.fileOrIdentifier = "/plugins/Fake Synth.vst3";
        description.pluginFormatName = "VST3";
        project.plugin = description;
        synth.getStateInformation (project.pluginState);
        const auto file = folder.getSiblingFile (juce::String ("Test") + syn::Project::fileExtension);
        check (project.save (file), "a project is saved");
        const auto back = syn::Project::load (file);
        check (back.has_value() && back->map.libraryName == "Test Synth" && back->map.groups.size() == map.groups.size()
                   && back->plugin.has_value() && back->plugin->name == "Fake Synth"
                   && back->plugin->fileOrIdentifier == "/plugins/Fake Synth.vst3"
                   && back->pluginState == project.pluginState,
               "a project opens with its rows, plugin and the plugin's settings");
        check (! syn::Project::load (folder.getChildFile ("Test Synth.sfz")).has_value(), "other files aren't projects");
    }

    // Same-sounding layers: 127, 95 and 64 are one sound at three levels, 32 is brighter.
    {
        syn::SampleMap layered;
        layered.libraryName = "Layered";
        syn::Group row;
        row.name = "Keys";
        row.lowNote = row.highNote = 60;
        row.layers = 4;
        row.roundRobins = 2;
        row.holdSeconds = 0.3;
        layered.groups = { row };
        juce::String layeredReport;
        const auto lib = syn::LibraryRecorder::record (synth, layered, {}, folder.getSiblingFile ("Layered"), nullptr, layeredReport);
        check (lib.samples.size() == 2 * 2 && lib.samples[0].velocities == juce::Range<int> (33, 127)
                   && lib.samples[2].velocities == juce::Range<int> (1, 32),
               "layers that only differ in level are left out, and the louder one covers them");
        check (layeredReport.contains ("2 velocity layers"), "the report says so (" + layeredReport + ")");
        check (! syn::LibraryRecorder::soundsTheSame (juce::AudioBuffer<float> (2, 10), juce::AudioBuffer<float> (2, 10), 48000.0),
               "too-short recordings are never called the same");
    }

    // Release samples: the sound after key-up is kept as its own sample, played on key-up.
    {
        syn::SampleMap withRelease;
        withRelease.libraryName = "Released";
        withRelease.skipSameLayers = false;
        syn::Group held;
        held.name = "Keys";
        held.lowNote = 57;
        held.highNote = 59;
        held.step = 2;
        held.layers = 2;
        held.roundRobins = 2;
        held.holdSeconds = 0.5;
        held.releaseSamples = true;
        withRelease.groups = { held };
        const auto releaseFolder = folder.getSiblingFile ("Released");
        juce::String releaseReport;
        const auto lib = syn::LibraryRecorder::record (synth, withRelease, {}, releaseFolder, nullptr, releaseReport);
        int releases = 0;
        const syn::Sample* firstRelease = nullptr;
        for (const auto& s : lib.samples)
            if (s.release)
            {
                ++releases;
                firstRelease = firstRelease == nullptr ? &s : firstRelease;
            }
        check (lib.samples.size() == 2 * 2 * 2 + 2 * 2 && releases == 2 * 2,
               "one release sample per note and layer, besides the round robins");
        if (firstRelease != nullptr)
            if (std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (releaseFolder.getChildFile (firstRelease->path)) })
            {
                const double seconds = (double) reader->lengthInSamples / reader->sampleRate;
                check (firstRelease->path.endsWith ("_rel.wav") && seconds > 0.1 && seconds < 0.4,
                       "a release sample is the sound after key-up (" + juce::String (seconds, 2) + " s)");
            }
        const auto releaseSfz = lib.toSfz();
        check (countOf (releaseSfz, "trigger=release") == 4 && releaseSfz.contains ("ampeg_release=0.1"),
               "the .sfz plays release samples on key-up, and fades the held sound fast");
        check (countOf (lib.toFloe ("Me"), "trigger_event = \"note-off\"") == 4
                   && lib.writePresets (releaseFolder, false, false, true).isEmpty(),
               "the .floe.lua plays release samples on key-up");
        const auto releaseDs = juce::parseXML (lib.toDecentSampler());
        auto* groups = releaseDs != nullptr ? releaseDs->getChildByName ("groups") : nullptr;
        check (groups != nullptr && groups->getNumChildElements() == 2
                   && groups->getChildElement (1)->getStringAttribute ("trigger") == "release"
                   && groups->getChildElement (1)->getNumChildElements() == 4,
               "the .dspreset has a key-up group");
    }

    // Keyswitches: each articulation row is recorded after its switch, and the presets switch too.
    {
        check (syn::Group::tidyKeyswitch ("c1") == "C1" && syn::Group::tidyKeyswitch ("24") == "C1"
                   && syn::Group::tidyKeyswitch ("cc 32 = 10") == "CC32=10" && syn::Group::tidyKeyswitch ("hello").isEmpty(),
               "keyswitches are read as notes or CCs");
        syn::Group cc;
        cc.keyswitch = "CC32=10";
        cc.midiChannel = 3;
        const auto messages = cc.switchMessages();
        check (messages.size() == 1 && messages[0].isControllerOfType (32) && messages[0].getControllerValue() == 10
                   && messages[0].getChannel() == 3,
               "a CC keyswitch sends its CC on the row's channel");

        syn::SampleMap switched;
        switched.libraryName = "Switched";
        syn::Group loud;
        loud.name = "Sustain";
        loud.lowNote = loud.highNote = 60;
        loud.layers = 1;
        loud.holdSeconds = 0.3;
        loud.keyswitch = "C1";
        auto quiet = loud;
        quiet.name = "Muted";
        quiet.keyswitch = "C#1";
        switched.groups = { quiet, loud };   // quiet first: the switch must be sent for every row
        const auto switchFolder = folder.getSiblingFile ("Switched");
        juce::String switchReport;
        const auto lib = syn::LibraryRecorder::record (synth, switched, {}, switchFolder, nullptr, switchReport);
        check (countOf (lib.toFloe ("Me"), "floe.new_instrument(") == 2 && lib.writePresets (switchFolder, false, false, true).isEmpty(),
               "each articulation is a Floe instrument of its own");
        float peaks[2] = {};
        for (int i = 0; i < 2 && i < (int) lib.samples.size(); ++i)
            if (std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (switchFolder.getChildFile (lib.samples[(size_t) i].path)) })
            {
                juce::AudioBuffer<float> audio (2, (int) reader->lengthInSamples);
                reader->read (&audio, 0, audio.getNumSamples(), 0, true, true);
                peaks[i] = audio.getMagnitude (0, 0, audio.getNumSamples());
            }
        check (lib.samples.size() == 2 && peaks[0] > 0.0f && peaks[1] > 3.0f * peaks[0],
               "each articulation is recorded after its keyswitch (" + juce::String (peaks[0], 3) + " vs " + juce::String (peaks[1], 3) + ")");
        const auto switchSfz = lib.toSfz();
        check (switchSfz.contains ("sw_lokey=24 sw_hikey=25 sw_last=25 sw_default=25") && switchSfz.contains ("sw_last=24"),
               "the .sfz switches articulations with the same keys");
        const auto switchDs = juce::parseXML (lib.toDecentSampler());
        if (switchDs != nullptr)
        {
            auto* menu = switchDs->getChildByName ("ui")->getChildByName ("tab")->getChildByName ("menu");
            check (menu != nullptr && menu->getNumChildElements() == 2
                       && menu->getChildElement (1)->getStringAttribute ("name") == "Sustain",
                   "the .dspreset has an articulation menu");
            auto* groups = switchDs->getChildByName ("groups");
            check (groups->getChildElement (0)->getStringAttribute ("enabled", "true") == "true"
                       && groups->getChildElement (1)->getStringAttribute ("enabled") == "false",
                   "only the first articulation plays to start with");
            auto* midi = switchDs->getChildByName ("midi");
            check (midi != nullptr && midi->getNumChildElements() == 2 && midi->getChildElement (1)->getIntAttribute ("note") == 24,
                   "the .dspreset's keyswitch notes pick articulations");
        }
    }

    // Looping: a held sine loops seamlessly; a pluck dies away while held, so it doesn't.
    {
        syn::SampleMap loopMap;
        loopMap.libraryName = "Loops";
        syn::Group pad;
        pad.name = "Pad";
        pad.lowNote = pad.highNote = 69;
        pad.layers = 1;
        pad.holdSeconds = 3.0;
        auto pluckRow = pad;
        pluckRow.name = "Pluck";
        pluckRow.lowNote = pluckRow.highNote = 100;
        loopMap.groups = { pad, pluckRow };
        const auto loopFolder = folder.getSiblingFile ("Loops");
        juce::String loopReport;
        const auto loops = syn::LibraryRecorder::record (synth, loopMap, {}, loopFolder, nullptr, loopReport);
        check (loops.samples.size() == 2 && loops.samples[0].loops() && ! loops.samples[1].loops(),
               "a sustained note loops and a dying one doesn't (" + loopReport + ")");
        if (loops.samples.size() == 2 && loops.samples[0].loops())
        {
            const auto& s = loops.samples[0];
            check (s.loopStart > 48000 * 0.9 && s.loopEnd < 48000 * 3 && s.loopEnd - s.loopStart > 48000 * 0.9,
                   "the loop sits in the held part (" + juce::String (s.loopStart) + " to " + juce::String (s.loopEnd) + ")");
            if (std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (loopFolder.getChildFile (s.path)) })
            {
                check ((int) reader->lengthInSamples == s.loopEnd + 1, "the recording stops at the loop's end");
                check (reader->metadataValues["Loop0Start"].getIntValue() == s.loopStart
                           && reader->metadataValues["Loop0End"].getIntValue() == s.loopEnd,
                       "the WAV carries the loop for other samplers");
                juce::AudioBuffer<float> audio (2, (int) reader->lengthInSamples);
                reader->read (&audio, 0, audio.getNumSamples(), 0, true, true);
                // Jumping from the loop's end back to its start must be no bigger a step than
                // the sine takes anyway (440 Hz at 48 kHz, level about 0.5: about 0.03 a sample).
                const float jump = std::abs (audio.getSample (0, s.loopStart) - audio.getSample (0, s.loopEnd));
                check (jump < 0.05f, "the loop is seamless (step " + juce::String (jump, 4) + ")");
            }
            check (loops.toSfz().contains ("loop_mode=loop_continuous loop_start=" + juce::String (s.loopStart)),
                   "the .sfz loops it");
            check (loops.toDecentSampler().contains ("loopEnabled=\"true\" loopStart=\"" + juce::String (s.loopStart)),
                   "the .dspreset loops it");
            check (loops.toFloe ("Me").contains ("builtin_loop = { start_frame = " + juce::String (s.loopStart)
                                                 + ", end_frame = " + juce::String (s.loopEnd + 1) + ", crossfade = 0 }")
                       && loops.writePresets (loopFolder, false, false, true).isEmpty(),
                   "the .floe.lua loops it, its end one past the last frame as Floe counts");
        }
    }

    // Measuring: the held sine sustains and fades in about 0.2 s after key-up; the pluck dies
    // away by itself in about 3.5 s whatever the key does.
    syn::Group sine;
    sine.lowNote = sine.highNote = 60;
    const auto sustained = syn::LibraryRecorder::measureTiming (synth, {}, sine);
    check (sustained.sounds && sustained.sustains && ! sustained.oneShot && sustained.release > 0.1 && sustained.release < 0.4,
           "a sustained note keeps its hold and gets its release (" + juce::String (sustained.release) + " s)");
    syn::Group pluck;
    pluck.lowNote = pluck.highNote = 100;
    const auto plucked = syn::LibraryRecorder::measureTiming (synth, {}, pluck);
    check (plucked.sounds && ! plucked.sustains && plucked.oneShot && plucked.ringSeconds > 3.0 && plucked.ringSeconds < 4.0,
           "a note that ignores key-up is a one-shot (" + juce::String (plucked.ringSeconds, 2) + " s)");
    syn::Group silent;
    silent.lowNote = silent.highNote = 61;
    check (! syn::LibraryRecorder::measureTiming (synth, {}, silent).sounds, "a silent note measures nothing");

    // Find notes hears every key but the silent one, and stops when asked.
    const auto found = syn::LibraryRecorder::findSoundingNotes (synth, {}, 1, nullptr);
    check (found.size() == 125 && ! found.contains (61) && ! found.contains (24) && found.contains (0) && found.contains (127),
           "Find notes hears every sounding key");
    const auto someFound = syn::LibraryRecorder::findSoundingNotes (synth, {}, 1, [] (int note) { return note < 10; });
    check (someFound.size() == 10, "Find notes stops when cancelled");

    // Cancelling keeps what was recorded.
    juce::String stopped;
    const auto partial = syn::LibraryRecorder::record (synth, map, {}, folder.getSiblingFile ("Stopped"),
                                                       [] (const syn::LibraryRecorder::Progress& p) { return p.done < 3; },
                                                       stopped);
    check (partial.samples.size() == 3 && stopped.startsWith ("Stopped after 3"), "a cancelled run keeps its recordings");

    // Several of the plugin's sounds in one library: the plugin is switched to each, and waited for.
    {
        syn::SampleMap two;
        two.libraryName = "Two Sounds";
        two.sounds.push_back ({ "Low", juce::MemoryBlock ("patch 1", 7) });
        two.sounds.push_back ({ "High", juce::MemoryBlock ("patch 2", 7) });
        for (int s = 0; s < 2; ++s)
        {
            auto g = syn::Group::keys();
            g.lowNote = g.highNote = 69;
            g.layers = 1;
            g.holdSeconds = 0.3;
            g.loop = false;
            g.sound = s;
            two.groups.push_back (g);
        }
        const auto back = syn::SampleMap::fromValueTree (two.toValueTree());
        check (back.sounds.size() == 2 && back.sounds[1].name == "High" && back.sounds[1].state == two.sounds[1].state
                   && back.groups[1].sound == 1,
               "the sounds and each row's sound are saved");

        const auto twoFolder = folder.getSiblingFile ("Two Sounds");
        juce::String twoReport;
        const auto lib = syn::LibraryRecorder::record (synth, two, {}, twoFolder, nullptr, twoReport);
        check (lib.samples.size() == 2 && lib.samples[0].path.startsWith ("Samples/Low/Keys/")
                   && lib.samples[1].path.startsWith ("Samples/High/Keys/"),
               "each sound records into a folder of its own, after it loaded (" + twoReport + ")");
        if (lib.samples.size() == 2)
        {
            // Count zero crossings: High plays an octave up.
            auto crossings = [&] (const syn::Sample& s)
            {
                std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (twoFolder.getChildFile (s.path)) };
                if (reader == nullptr)
                    return 0;
                juce::AudioBuffer<float> audio (2, 4800);
                reader->read (&audio, 0, 4800, 0, true, true);
                int n = 0;
                for (int i = 1; i < 4800; ++i)
                    n += (audio.getSample (0, i - 1) < 0.0f) != (audio.getSample (0, i) < 0.0f);
                return n;
            };
            const int low = crossings (lib.samples[0]), high = crossings (lib.samples[1]);
            check (high > low * 3 / 2, "the plugin plays each row's sound (" + juce::String (low) + " vs " + juce::String (high) + " crossings)");
        }
        check (lib.writePresets (twoFolder, true, true, true).isEmpty()
                   && twoFolder.getChildFile ("Two Sounds - Low.dspreset").existsAsFile()
                   && twoFolder.getChildFile ("Two Sounds - High.sfz").existsAsFile()
                   && ! twoFolder.getChildFile ("Two Sounds.sfz").existsAsFile(),
               "Decent Sampler and SFZ get a preset per sound");
        const auto twoFloe = twoFolder.getChildFile ("Two Sounds.floe.lua").loadFileAsString();
        check (twoFloe.contains ("name = \"Low\"") && twoFloe.contains ("name = \"High\""), "Floe gets an instrument per sound, named after it");
        check (countOf (twoFolder.getChildFile ("Two Sounds - High.sfz").loadFileAsString(), "<region>") == 1,
               "a sound's preset holds only its own rows");

        syn::NoteRenderer renderer (synth, {});
        int asked = 0;
        check (! syn::LibraryRecorder::switchSound (synth, two.sounds[0], renderer, two.groups[0],
                                                    [&asked] { return ++asked < 2; }, 60.0, 10),
               "waiting for a sound stops when cancelled");
    }

    std::cout << (failures == 0 ? "All checks passed." : juce::String (failures) + " checks FAILED.") << std::endl;
    return failures == 0 ? 0 : 1;
}
