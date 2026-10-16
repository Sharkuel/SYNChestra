#include "Library.h"

namespace syn
{
namespace
{
juce::String num (double v)
{
    return juce::String (v, 3).trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
}

/** The samples of one group, grouped by note and layer so round robins stay together. */
std::map<std::pair<int, int>, std::vector<const Sample*>> takesOf (const std::vector<Sample>& samples, int group)
{
    std::map<std::pair<int, int>, std::vector<const Sample*>> sets;
    for (const auto& s : samples)
        if (s.group == group)
            sets[{ s.rootNote, s.velocities.getEnd() }].push_back (&s);
    return sets;
}
} // namespace

juce::String Library::toSfz() const
{
    juce::String out;
    out << "// " << map.libraryName << ", recorded with SYNChestra\n\n";
    out << "<control>\ndefault_path=\n\n";
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
                out << " ampeg_release=" << num (group.releaseSeconds);
            if (takes.size() > 1)
                out << " seq_length=" << (int) takes.size();
            out << "\n";
            for (const auto* s : takes)
            {
                // sample= goes last: players read it to the end of the line, spaces and all.
                out << "<region>";
                if (takes.size() > 1)
                    out << " seq_position=" << s->roundRobin;
                out << " sample=" << s->path << "\n";
            }
        }
        out << "\n";
    }
    return out;
}

juce::String Library::toDecentSampler() const
{
    juce::XmlElement root ("DecentSampler");
    root.setAttribute ("minVersion", "1.0.0");

    // A simple front panel: volume and release for the whole instrument.
    auto* ui = root.createNewChildElement ("ui");
    ui->setAttribute ("width", 812);
    ui->setAttribute ("height", 375);
    auto* tab = ui->createNewChildElement ("tab");
    tab->setAttribute ("name", "main");
    auto* title = tab->createNewChildElement ("label");
    title->setAttribute ("x", 20);
    title->setAttribute ("y", 20);
    title->setAttribute ("width", 772);
    title->setAttribute ("height", 40);
    title->setAttribute ("text", map.libraryName);
    title->setAttribute ("textSize", 28);
    auto knob = [tab] (int x, const juce::String& label, double min, double max, double value, const juce::String& parameter)
    {
        auto* k = tab->createNewChildElement ("labeled-knob");
        k->setAttribute ("x", x);
        k->setAttribute ("y", 120);
        k->setAttribute ("width", 90);
        k->setAttribute ("label", label);
        k->setAttribute ("type", "float");
        k->setAttribute ("minValue", min);
        k->setAttribute ("maxValue", max);
        k->setAttribute ("value", value);
        auto* b = k->createNewChildElement ("binding");
        b->setAttribute ("type", "amp");
        b->setAttribute ("level", "instrument");
        b->setAttribute ("position", 0);
        b->setAttribute ("parameter", parameter);
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
    knob (40, "Volume", 0.0, 1.0, 0.8, "AMP_VOLUME");
    knob (150, "Release", 0.0, juce::jmax (5.0, release), release, "ENV_RELEASE");

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
        // A one-shot keeps ringing after key-up: the release lasts as long as the longest take could.
        if (group.oneShot)
            xml->setAttribute ("release", 30.0);
        else if (! juce::approximatelyEqual (group.releaseSeconds, release))
            xml->setAttribute ("release", group.releaseSeconds);
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
                if (takes.size() > 1)
                {
                    e->setAttribute ("seqPosition", s->roundRobin);
                    e->setAttribute ("seqLength", (int) takes.size());
                }
            }
    }
    return root.toString();
}

juce::String Library::writePresets (const juce::File& folder, bool sfz, bool decentSampler) const
{
    const auto name = LibraryRecorder::safeName (map.libraryName);
    if (sfz && ! folder.getChildFile (name + ".sfz").replaceWithText (toSfz()))
        return "Could not write " + name + ".sfz.";
    if (decentSampler && ! folder.getChildFile (name + ".dspreset").replaceWithText (toDecentSampler()))
        return "Could not write " + name + ".dspreset.";
    return {};
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

bool LibraryRecorder::writeWav (const juce::File& file, const juce::AudioBuffer<float>& audio, double sampleRate)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    if (stream == nullptr)
        return false;
    const auto options = juce::AudioFormatWriterOptions{}
                             .withSampleRate (sampleRate)
                             .withNumChannels (audio.getNumChannels())
                             .withBitsPerSample (24);
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

    for (int g = 0; g < (int) map.groups.size(); ++g)
    {
        const auto& group = map.groups[(size_t) g];
        if (! group.enabled)
            continue;
        const auto notes = group.sampledNotes();
        const auto velocities = group.velocities();
        const int takes = juce::jmax (1, group.roundRobins);
        const auto groupFolder = "Samples/" + safeName (group.name);
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

                    const auto audio = renderer.render (note, velocity, group.holdSeconds, group.midiChannel);
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
                    if (! writeWav (folder.getChildFile (s.path), audio, settings.sampleRate))
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
    std::map<std::tuple<int, int, int>, int> counts;
    for (const auto& s : library.samples)
        ++counts[{ s.group, s.rootNote, s.velocities.getEnd() }];
    std::map<std::tuple<int, int, int>, int> seen;
    for (auto& s : library.samples)
    {
        const std::tuple<int, int, int> key { s.group, s.rootNote, s.velocities.getEnd() };
        s.roundRobin = ++seen[key];
        s.roundRobins = counts[key];
    }

    report = "Recorded " + juce::String ((int) library.samples.size()) + " samples.";
    if (! silent.isEmpty())
        report << " Silent, so left out: " << silent.joinIntoString (", ") << ".";
    if (! failed.isEmpty())
        report << " Could not write " << failed.size() << " files (is the disk full?).";
    return library;
}
} // namespace syn
