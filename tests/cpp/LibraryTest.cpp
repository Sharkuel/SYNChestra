// Records a small library from a stand-in synth plugin and checks the samples and both presets,
// so the recorder can be checked without a commercial plugin. Usage: SYNChestra_tests OUTPUT_FOLDER
#include "../../Source/Core/Library.h"
#include <juce_graphics/juce_graphics.h>

namespace
{
/** Each note plays a sine at its pitch, louder with velocity, while held, then fades over
    0.2 s. The left side is louder than the right, so the test can tell stereo was kept.
    Notes from 100 up are plucks: they die away over about 3.5 s and ignore note-off.
    Soft notes (velocity 40 and under) sound brighter, with a third harmonic; louder ones are
    the same sound, only louder. C1 and C#1 (24, 25) are keyswitches, silent: after C#1 notes play at a quarter of the level, until C1. */
class FakeSynth final : public juce::AudioPluginInstance
{
public:
    FakeSynth() : AudioPluginInstance (BusesProperties().withOutput ("Out", juce::AudioChannelSet::stereo())) {}

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
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    using AudioPluginInstance::processBlock;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        buffer.clear();
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
                frequency = juce::MidiMessage::getMidiNoteInHertz (msg.getNoteNumber());
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
    check (withPicture.writePresets (folder, true, true).isEmpty(), "presets written");
    const auto background = juce::ImageFileFormat::loadFrom (folder.getChildFile ("Images/background.png"));
    check (background.getWidth() == 812 && background.getHeight() == 375, "the background picture is scaled to the panel");
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

    std::cout << (failures == 0 ? "All checks passed." : juce::String (failures) + " checks FAILED.") << std::endl;
    return failures == 0 ? 0 : 1;
}
