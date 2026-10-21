#include "Library.h"
#include "Looper.h"
#include <juce_graphics/juce_graphics.h>

namespace syn
{
namespace
{
juce::String num (double v)
{
    return juce::String (v, 3).trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
}

/** The samples of one group, grouped by note and layer so round robins stay together: the
    ones played on key-down, or with release true the ones played on key-up. */
std::map<std::pair<int, int>, std::vector<const Sample*>> takesOf (const std::vector<Sample>& samples, int group,
                                                                    bool release = false)
{
    std::map<std::pair<int, int>, std::vector<const Sample*>> sets;
    for (const auto& s : samples)
        if (s.group == group && s.release == release)
            sets[{ s.rootNote, s.velocities.getEnd() }].push_back (&s);
    return sets;
}
} // namespace

double Library::mainRelease (int g) const
{
    // With key-up samples the recording fades fast and the release sample takes over.
    const auto& group = map.groups[(size_t) g];
    return takesOf (samples, g, true).empty() ? group.releaseSeconds : juce::jmin (group.releaseSeconds, 0.1);
}

std::vector<int> Library::articulationRows() const
{
    std::vector<int> rows;
    for (int g = 0; g < (int) map.groups.size(); ++g)
    {
        const auto& group = map.groups[(size_t) g];
        if ((group.switchNote() >= 0 || group.switchCc().first >= 0) && ! takesOf (samples, g).empty())
            rows.push_back (g);
    }
    return rows;
}

juce::String Library::toSfz() const
{
    // Rows with a keyswitch are articulations: the sampler plays the one whose switch came last.
    // Note switches share one key range; the first is on until another is pressed. CC switches
    // play while the CC has their value; the first one's value is set at load.
    const auto articulations = articulationRows();
    int switchLow = 128, switchHigh = -1, switchDefault = -1;
    std::map<int, int> ccDefaults;
    for (const int g : articulations)
    {
        const auto& group = map.groups[(size_t) g];
        if (const int note = group.switchNote(); note >= 0)
        {
            switchLow = juce::jmin (switchLow, note);
            switchHigh = juce::jmax (switchHigh, note);
            if (switchDefault < 0)
                switchDefault = note;
        }
        else if (const auto [cc, value] = group.switchCc(); cc >= 0 && ccDefaults.count (cc) == 0)
        {
            ccDefaults[cc] = value;
        }
    }

    juce::String out;
    out << "// " << map.libraryName << ", recorded with SYNChestra\n\n";
    out << "<control>\ndefault_path=\n";
    for (const auto& [cc, value] : ccDefaults)
        out << "set_cc" << cc << "=" << value << "\n";
    out << "\n";
    for (int g = 0; g < (int) map.groups.size(); ++g)
    {
        const auto& group = map.groups[(size_t) g];
        const auto sets = takesOf (samples, g);
        if (sets.empty())
            continue;
        out << "// " << group.name << "\n";
        for (const auto& [key, takes] : sets)
        {
            const auto& first = *takes.front();
            out << "<group> group_label=" << group.name.replaceCharacter (' ', '_')
                << " lokey=" << first.keys.getStart() << " hikey=" << first.keys.getEnd()
                << " lovel=" << first.velocities.getStart() << " hivel=" << first.velocities.getEnd()
                << " pitch_keycenter=" << first.rootNote;
            if (! group.pitched)
                out << " pitch_keytrack=0";
            if (group.oneShot)
                out << " loop_mode=one_shot";
            else
                out << " ampeg_release=" << num (mainRelease (g));
            if (takes.size() > 1)
                out << " seq_length=" << (int) takes.size();
            if (const int note = group.switchNote(); note >= 0)
                out << " sw_lokey=" << switchLow << " sw_hikey=" << switchHigh << " sw_last=" << note
                    << " sw_default=" << switchDefault;
            else if (const auto [cc, value] = group.switchCc(); cc >= 0)
                out << " locc" << cc << "=" << value << " hicc" << cc << "=" << value;
            out << "\n";
            for (const auto* s : takes)
            {
                // sample= goes last: players read it to the end of the line, spaces and all.
                out << "<region>";
                if (takes.size() > 1)
                    out << " seq_position=" << s->roundRobin;
                if (s->loops())
                    out << " loop_mode=loop_continuous loop_start=" << s->loopStart << " loop_end=" << s->loopEnd;
                out << " sample=" << s->path << "\n";
            }
        }
        // The key-up sounds: played once, to their end, when a key is let go.
        for (const auto& [key, takes] : takesOf (samples, g, true))
        {
            const auto& r = *takes.front();
            out << "<group> group_label=" << group.name.replaceCharacter (' ', '_') << "_release trigger=release loop_mode=one_shot"
                << " lokey=" << r.keys.getStart() << " hikey=" << r.keys.getEnd()
                << " lovel=" << r.velocities.getStart() << " hivel=" << r.velocities.getEnd()
                << " pitch_keycenter=" << r.rootNote;
            if (! group.pitched)
                out << " pitch_keytrack=0";
            if (const int note = group.switchNote(); note >= 0)
                out << " sw_lokey=" << switchLow << " sw_hikey=" << switchHigh << " sw_last=" << note
                    << " sw_default=" << switchDefault;
            else if (const auto [cc, value] = group.switchCc(); cc >= 0)
                out << " locc" << cc << "=" << value << " hicc" << cc << "=" << value;
            out << "\n<region> sample=" << r.path << "\n";
        }
        out << "\n";
    }
    return out;
}

namespace
{
constexpr int panelWidth = 812, panelHeight = 375;   // Decent Sampler's recommended panel size
constexpr const char* backgroundPath = "Images/background.png";
} // namespace

juce::String Library::toDecentSampler() const
{
    juce::XmlElement root ("DecentSampler");
    root.setAttribute ("minVersion", "1.0.0");

    // A front panel: volume and release for the whole instrument, the effects' knobs, and the
    // background picture if there is one (writePresets copies it to Images/background.png).
    auto* ui = root.createNewChildElement ("ui");
    ui->setAttribute ("width", panelWidth);
    ui->setAttribute ("height", panelHeight);
    ui->setAttribute ("bgColor", "FF22282E");
    if (map.backgroundImage.isNotEmpty())
        ui->setAttribute ("bgImage", backgroundPath);
    auto* tab = ui->createNewChildElement ("tab");
    tab->setAttribute ("name", "main");
    auto* title = tab->createNewChildElement ("label");
    title->setAttribute ("x", 20);
    title->setAttribute ("y", 20);
    title->setAttribute ("width", 772);
    title->setAttribute ("height", 40);
    title->setAttribute ("text", map.libraryName);
    title->setAttribute ("textSize", 28);
    title->setAttribute ("textColor", "FFFFFFFF");
    // Knobs sit in a row along the bottom, so a background picture shows above them.
    int knobX = 20;
    auto knob = [tab, &knobX] (const juce::String& label, double min, double max, double value,
                               const juce::String& type, int effect, const juce::String& parameter)
    {
        auto* k = tab->createNewChildElement ("labeled-knob");
        k->setAttribute ("x", knobX);
        k->setAttribute ("y", panelHeight - 110);
        k->setAttribute ("width", 90);
        k->setAttribute ("height", 90);
        k->setAttribute ("label", label);
        k->setAttribute ("type", "float");
        k->setAttribute ("minValue", min);
        k->setAttribute ("maxValue", max);
        k->setAttribute ("value", value);
        k->setAttribute ("textColor", "FFFFFFFF");
        auto* b = k->createNewChildElement ("binding");
        b->setAttribute ("type", type);
        b->setAttribute ("level", "instrument");
        b->setAttribute ("position", effect);
        b->setAttribute ("parameter", parameter);
        knobX += 110;
    };
    // The Release knob sets the whole instrument's release, starting at the first held group's.
    // A group with its own release (or a one-shot) keeps it, as a group's envelope wins.
    double release = 0.3;
    for (const auto& g : map.groups)
        if (g.enabled && ! g.oneShot)
        {
            release = g.releaseSeconds;
            break;
        }
    knob ("Volume", 0.0, 1.0, 0.8, "amp", 0, "AMP_VOLUME");
    knob ("Release", 0.0, juce::jmax (5.0, release), release, "amp", 0, "ENV_RELEASE");
    if (map.effects)
    {
        // Effects on the whole instrument, all off (or open) to start. A knob finds its effect by
        // its place in this list.
        auto* effects = root.createNewChildElement ("effects");
        auto effect = [effects] (const juce::String& type, std::initializer_list<std::pair<const char*, double>> values)
        {
            auto* e = effects->createNewChildElement ("effect");
            e->setAttribute ("type", type);
            for (const auto& [name, value] : values)
                e->setAttribute (name, value);
        };
        effect ("lowpass", { { "frequency", 22000.0 }, { "resonance", 0.7 } });
        effect ("chorus", { { "mix", 0.0 }, { "modDepth", 0.2 }, { "modRate", 0.2 } });
        effect ("delay", { { "delayTime", 0.3 }, { "feedback", 0.3 }, { "stereoOffset", 0.01 }, { "wetLevel", 0.0 } });
        effect ("reverb", { { "roomSize", 0.7 }, { "damping", 0.3 }, { "wetLevel", 0.0 } });
        knob ("Tone", 60.0, 22000.0, 22000.0, "effect", 0, "FX_FILTER_FREQUENCY");
        knob ("Chorus", 0.0, 1.0, 0.0, "effect", 1, "FX_MIX");
        knob ("Delay", 0.0, 1.0, 0.0, "effect", 2, "FX_WET_LEVEL");
        knob ("Reverb", 0.0, 1.0, 0.0, "effect", 3, "FX_REVERB_WET_LEVEL");
        knob ("Room", 0.0, 1.0, 0.7, "effect", 3, "FX_REVERB_ROOM_SIZE");
    }

    // Articulations: only the first plays to start with. A menu on the panel picks another, and
    // so do the keyswitch notes, as in the plugin.
    std::map<int, int> dsIndex;   // table row -> Decent Sampler group, which skips rows with nothing recorded
    for (int g = 0; g < (int) map.groups.size(); ++g)
        if (! takesOf (samples, g).empty())
            dsIndex[g] = (int) dsIndex.size();
    std::map<int, int> dsReleaseIndex;   // key-up groups come after all the others
    for (int g = 0; g < (int) map.groups.size(); ++g)
        if (dsIndex.count (g) > 0 && ! takesOf (samples, g, true).empty())
            dsReleaseIndex[g] = (int) (dsIndex.size() + dsReleaseIndex.size());
    const auto articulations = articulationRows();
    if (articulations.size() > 1)
        title->setAttribute ("width", panelWidth - 280);   // room for the articulation menu
    auto chooseArticulation = [&] (juce::XmlElement& parent, int chosen)
    {
        auto bind = [&parent] (int position, bool on)
        {
            auto* b = parent.createNewChildElement ("binding");
            b->setAttribute ("type", "general");
            b->setAttribute ("level", "group");
            b->setAttribute ("position", position);
            b->setAttribute ("parameter", "ENABLED");
            b->setAttribute ("translation", "fixed_value");
            b->setAttribute ("translationValue", on ? "true" : "false");
        };
        for (const int g : articulations)
        {
            bind (dsIndex[g], g == chosen);
            if (dsReleaseIndex.count (g) > 0)
                bind (dsReleaseIndex[g], g == chosen);
        }
    };
    if (articulations.size() > 1)
    {
        auto* menu = tab->createNewChildElement ("menu");
        menu->setAttribute ("x", panelWidth - 230);
        menu->setAttribute ("y", 25);
        menu->setAttribute ("width", 210);
        menu->setAttribute ("height", 30);
        menu->setAttribute ("value", 1);
        for (const int g : articulations)
        {
            auto* option = menu->createNewChildElement ("option");
            option->setAttribute ("name", map.groups[(size_t) g].name);
            chooseArticulation (*option, g);
        }
        juce::XmlElement* midi = nullptr;
        for (const int g : articulations)
            if (const int note = map.groups[(size_t) g].switchNote(); note >= 0)
            {
                if (midi == nullptr)
                    midi = root.createNewChildElement ("midi");
                auto* n = midi->createNewChildElement ("note");
                n->setAttribute ("note", note);
                n->setAttribute ("eventType", "note_on");
                chooseArticulation (*n, g);
            }
    }

    auto* groups = root.createNewChildElement ("groups");
    groups->setAttribute ("attack", 0.0);
    groups->setAttribute ("decay", 0.0);
    groups->setAttribute ("sustain", 1.0);
    groups->setAttribute ("release", release);
    for (int g = 0; g < (int) map.groups.size(); ++g)
    {
        const auto& group = map.groups[(size_t) g];
        const auto sets = takesOf (samples, g);
        if (sets.empty())
            continue;
        auto* xml = groups->createNewChildElement ("group");
        xml->setAttribute ("name", group.name);
        if (articulations.size() > 1 && std::find (articulations.begin(), articulations.end(), g) != articulations.begin()
            && std::find (articulations.begin(), articulations.end(), g) != articulations.end())
            xml->setAttribute ("enabled", "false");
        // A one-shot keeps ringing after key-up: the release lasts as long as the longest take could.
        if (group.oneShot)
            xml->setAttribute ("release", 30.0);
        else if (! juce::approximatelyEqual (mainRelease (g), release))
            xml->setAttribute ("release", mainRelease (g));
        bool anyRoundRobin = false;
        for (const auto& [key, takes] : sets)
            anyRoundRobin = anyRoundRobin || takes.size() > 1;
        if (anyRoundRobin)
            xml->setAttribute ("seqMode", "round_robin");
        for (const auto& [key, takes] : sets)
            for (const auto* s : takes)
            {
                auto* e = xml->createNewChildElement ("sample");
                e->setAttribute ("path", s->path);
                e->setAttribute ("rootNote", s->rootNote);
                e->setAttribute ("loNote", s->keys.getStart());
                e->setAttribute ("hiNote", s->keys.getEnd());
                e->setAttribute ("loVel", s->velocities.getStart());
                e->setAttribute ("hiVel", s->velocities.getEnd());
                if (! group.pitched)
                    e->setAttribute ("pitchKeyTrack", 0);
                if (s->loops())
                {
                    e->setAttribute ("loopEnabled", "true");
                    e->setAttribute ("loopStart", s->loopStart);
                    e->setAttribute ("loopEnd", s->loopEnd);
                }
                if (takes.size() > 1)
                {
                    e->setAttribute ("seqPosition", s->roundRobin);
                    e->setAttribute ("seqLength", (int) takes.size());
                }
            }
    }

    // The key-up sounds, after every other group so the indexes above hold.
    for (const auto& [g, index] : dsReleaseIndex)
    {
        const auto& group = map.groups[(size_t) g];
        auto* xml = groups->createNewChildElement ("group");
        xml->setAttribute ("name", group.name + " release");
        xml->setAttribute ("trigger", "release");
        xml->setAttribute ("release", 30.0);   // plays to its end
        if (articulations.size() > 1 && ! articulations.empty() && articulations.front() != g
            && std::find (articulations.begin(), articulations.end(), g) != articulations.end())
            xml->setAttribute ("enabled", "false");
        for (const auto& [key, takes] : takesOf (samples, g, true))
        {
            const auto* r = takes.front();
            auto* e = xml->createNewChildElement ("sample");
            e->setAttribute ("path", r->path);
            e->setAttribute ("rootNote", r->rootNote);
            e->setAttribute ("loNote", r->keys.getStart());
            e->setAttribute ("hiNote", r->keys.getEnd());
            e->setAttribute ("loVel", r->velocities.getStart());
            e->setAttribute ("hiVel", r->velocities.getEnd());
            if (! group.pitched)
                e->setAttribute ("pitchKeyTrack", 0);
        }
    }
    return root.toString();
}

juce::String Library::writePresets (const juce::File& folder, bool sfz, bool decentSampler) const
{
    const auto name = LibraryRecorder::safeName (map.libraryName);
    if (decentSampler && map.backgroundImage.isNotEmpty())
    {
        // Scaled and cropped to fill the panel, so any picture fits.
        const auto picture = juce::ImageFileFormat::loadFrom (juce::File (map.backgroundImage));
        if (! picture.isValid())
            return "Could not read the background picture " + map.backgroundImage + ".";
        juce::Image panel (juce::Image::RGB, panelWidth, panelHeight, true);
        {
            juce::Graphics g (panel);
            g.drawImage (picture, panel.getBounds().toFloat(), juce::RectanglePlacement::fillDestination);
        }
        const auto file = folder.getChildFile (backgroundPath);
        file.getParentDirectory().createDirectory();
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat png;
        if (! out.openedOk() || ! png.writeImageToStream (panel, out))
            return "Could not write " + juce::String (backgroundPath) + ".";
    }
    if (sfz && ! folder.getChildFile (name + ".sfz").replaceWithText (toSfz()))
        return "Could not write " + name + ".sfz.";
    if (decentSampler && ! folder.getChildFile (name + ".dspreset").replaceWithText (toDecentSampler()))
        return "Could not write " + name + ".dspreset.";
    return {};
}

namespace
{
/** Seconds from the start until the sound last reaches 60 dB below its peak, in 10 ms steps. */
double secondsToFade (const juce::AudioBuffer<float>& audio, double sampleRate)
{
    const int step = juce::jmax (1, (int) (sampleRate * 0.01));
    float peak = 0.0f;
    for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        peak = juce::jmax (peak, audio.getMagnitude (ch, 0, audio.getNumSamples()));
    const float quiet = peak * juce::Decibels::decibelsToGain (-60.0f);
    int last = 0;
    for (int at = 0; at < audio.getNumSamples(); at += step)
    {
        const int n = juce::jmin (step, audio.getNumSamples() - at);
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
            if (audio.getMagnitude (ch, at, n) >= quiet)
                last = at + n;
    }
    return last / sampleRate;
}
} // namespace

LibraryRecorder::Timing LibraryRecorder::measureTiming (juce::AudioPluginInstance& plugin, NoteRenderer::Settings settings,
                                                        const Group& group)
{
    // The lowest note rings longest, so what suits it suits the rest of the row.
    constexpr double longHold = 20.0, shortHold = 0.1;
    settings.maxSeconds = 30.0;
    NoteRenderer renderer (plugin, settings);
    renderer.setSwitch (group.switchMessages());
    const int note = group.sampledNotes().isEmpty() ? group.lowNote : group.sampledNotes().getFirst();
    Timing t;

    const auto held = renderer.render (note, 127, longHold, group.midiChannel);
    if (held.getNumSamples() == 0)
        return t;
    t.sounds = true;
    t.ringSeconds = secondsToFade (held, settings.sampleRate);
    t.sustains = t.ringSeconds >= longHold - 0.5;

    // Let go almost at once: how long it lasts after the key comes up, and whether it cares.
    const auto tapped = renderer.render (note, 127, shortHold, group.midiChannel);
    const double tappedSeconds = secondsToFade (tapped, settings.sampleRate);
    t.release = juce::jlimit (0.05, 10.0, tappedSeconds - shortHold);
    t.oneShot = ! t.sustains && tappedSeconds >= 0.8 * t.ringSeconds;

    if (t.oneShot)
        t.hold = shortHold;   // the recording rings to its end whatever the hold
    else if (t.sustains)
        t.hold = 4.0;         // long enough for held chords; a key held longer runs out of sample
    else
        t.hold = std::ceil (t.ringSeconds * 10.0) / 10.0;   // hold until it has died away by itself
    t.release = std::round (t.release * 100.0) / 100.0;
    return t;
}

juce::Array<int> LibraryRecorder::findSoundingNotes (juce::AudioPluginInstance& plugin, NoteRenderer::Settings settings,
                                                     int midiChannel, std::function<bool (int)> onProgress)
{
    // Short and a little deaf on purpose: a note only needs to be heard, and a pad's tail from
    // the last note (cut off after a few seconds) must not count as the next one sounding.
    settings.maxSeconds = 3.0;
    settings.giveUpSeconds = 0.5;
    settings.startDb = -50.0;
    juce::Array<int> found;
    NoteRenderer renderer (plugin, settings);
    for (int note = 0; note < 128; ++note)
    {
        if (onProgress && ! onProgress (note))
            break;
        if (renderer.render (note, 100, 0.2, midiChannel).getNumSamples() > 0)
            found.add (note);
    }
    return found;
}

juce::String LibraryRecorder::safeName (const juce::String& s)
{
    auto name = s.removeCharacters ("\\/:*?\"<>|").trim();
    return name.isEmpty() ? juce::String ("Untitled") : name;
}

bool LibraryRecorder::writeWav (const juce::File& file, const juce::AudioBuffer<float>& audio, double sampleRate,
                                const Sample* loopInfo)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    if (stream == nullptr)
        return false;
    auto options = juce::AudioFormatWriterOptions{}
                             .withSampleRate (sampleRate)
                             .withNumChannels (audio.getNumChannels())
                             .withBitsPerSample (24);
    if (loopInfo != nullptr && loopInfo->loops())
        options = options.withMetadataValues ({ { "MidiUnityNote", juce::String (loopInfo->rootNote) },
                                                { "NumSampleLoops", "1" },
                                                { "Loop0Type", "0" },   // forward
                                                { "Loop0Start", juce::String (loopInfo->loopStart) },
                                                { "Loop0End", juce::String (loopInfo->loopEnd) } });
    auto writer = wav.createWriterFor (stream, options);
    return writer != nullptr && writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
}

Library LibraryRecorder::record (juce::AudioPluginInstance& plugin, const SampleMap& map,
                                 const NoteRenderer::Settings& settings, const juce::File& folder,
                                 std::function<bool (const Progress&)> onProgress, juce::String& report)
{
    Library library;
    library.map = map;
    NoteRenderer renderer (plugin, settings);
    Progress progress;
    progress.total = map.totalRecordings();
    juce::StringArray silent, failed;
    int looped = 0;

    for (int g = 0; g < (int) map.groups.size(); ++g)
    {
        const auto& group = map.groups[(size_t) g];
        if (! group.enabled)
            continue;
        const auto notes = group.sampledNotes();
        const auto velocities = group.velocities();
        const int takes = juce::jmax (1, group.roundRobins);
        const auto groupFolder = "Samples/" + safeName (group.name);
        renderer.setSwitch (group.switchMessages());
        for (int n = 0; n < notes.size(); ++n)
            for (int v = velocities.size(); --v >= 0;)   // loudest first: a cancelled run still has the main layer
                for (int take = 1; take <= takes; ++take)
                {
                    const int note = notes[n], velocity = velocities[v];
                    progress.current = group.name + " " + noteName (note) + " v" + juce::String (velocity)
                                       + (takes > 1 ? " take " + juce::String (take) : juce::String());
                    if (onProgress && ! onProgress (progress))
                    {
                        report = "Stopped after " + juce::String (progress.done) + " of "
                                 + juce::String (progress.total) + " recordings.";
                        return library;
                    }
                    ++progress.done;

                    int noteOff = 0;
                    auto audio = renderer.render (note, velocity, group.holdSeconds, group.midiChannel, &noteOff);
                    if (audio.getNumSamples() == 0)
                    {
                        silent.addIfNotAlreadyThere (group.name + " " + noteName (note));
                        continue;
                    }
                    Sample s;
                    s.group = g;
                    s.rootNote = note;
                    s.keys = group.keyRangeFor (n);
                    s.velocities = group.velocityRangeFor (v);
                    s.roundRobin = take;
                    s.roundRobins = takes;
                    s.path = groupFolder + "/" + safeName (group.name) + "_" + noteName (note).replace ("#", "s")
                             + "_v" + juce::String (velocity).paddedLeft ('0', 3)
                             + (takes > 1 ? "_rr" + juce::String (take) : juce::String()) + ".wav";
                    if (group.releaseSamples && ! group.oneShot && take == 1)
                    {
                        // What follows key-up is the release: dampers, fret noise, the room. A
                        // couple of milliseconds of fade-in keep the cut from clicking.
                        const int tail = audio.getNumSamples() - noteOff;
                        if (noteOff > 0 && tail > (int) (settings.sampleRate * 0.02))
                        {
                            juce::AudioBuffer<float> releaseAudio (audio.getNumChannels(), tail);
                            for (int ch = 0; ch < audio.getNumChannels(); ++ch)
                                releaseAudio.copyFrom (ch, 0, audio, ch, noteOff, tail);
                            releaseAudio.applyGainRamp (0, juce::jmin (tail, (int) (settings.sampleRate * 0.002)), 0.0f, 1.0f);
                            auto r = s;
                            r.release = true;
                            r.roundRobin = r.roundRobins = 1;
                            r.path = r.path.upToLastOccurrenceOf (".wav", false, false).upToLastOccurrenceOf ("_rr", false, false)
                                     + "_rel.wav";
                            if (writeWav (folder.getChildFile (r.path), releaseAudio, settings.sampleRate))
                                library.samples.push_back (r);
                            else
                                failed.add (r.path);
                        }
                    }
                    if (group.loop && ! group.oneShot)
                    {
                        const auto loop = Looper::find (audio, noteOff, settings.sampleRate);
                        if (loop.isValid())
                        {
                            Looper::apply (audio, loop, settings.sampleRate);
                            s.loopStart = loop.start;
                            s.loopEnd = loop.end - 1;
                            ++looped;
                        }
                    }
                    if (! writeWav (folder.getChildFile (s.path), audio, settings.sampleRate, &s))
                    {
                        failed.add (s.path);
                        continue;
                    }
                    library.samples.push_back (s);
                }
    }

    // A silent note leaves a gap on the keyboard: its neighbours stretch over its keys.
    for (int g = 0; g < (int) map.groups.size(); ++g)
    {
        juce::Array<int> sounding;
        for (const auto& s : library.samples)
            if (s.group == g)
                sounding.addIfNotAlreadyThere (s.rootNote);
        sounding.sort();
        for (auto& s : library.samples)
            if (s.group == g)
                s.keys = map.groups[(size_t) g].keyRangeFor (sounding, sounding.indexOf (s.rootNote));
    }

    // Round robins of a note and layer that came out silent leave gaps: renumber what's there.
    std::map<std::tuple<int, int, int, bool>, int> counts;
    for (const auto& s : library.samples)
        ++counts[{ s.group, s.rootNote, s.velocities.getEnd(), s.release }];
    std::map<std::tuple<int, int, int, bool>, int> seen;
    for (auto& s : library.samples)
    {
        const std::tuple<int, int, int, bool> key { s.group, s.rootNote, s.velocities.getEnd(), s.release };
        s.roundRobin = ++seen[key];
        s.roundRobins = counts[key];
    }

    report = "Recorded " + juce::String ((int) library.samples.size()) + " samples";
    report << (looped > 0 ? ", " + juce::String (looped) + " of them looped." : juce::String ("."));
    if (! silent.isEmpty())
        report << " Silent, so left out: " << silent.joinIntoString (", ") << ".";
    if (! failed.isEmpty())
        report << " Could not write " << failed.size() << " files (is the disk full?).";
    return library;
}
} // namespace syn
