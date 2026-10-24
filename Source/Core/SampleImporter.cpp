#include "SampleImporter.h"
#include <filesystem>
#include <regex>

namespace syn
{
namespace
{
/** A note name as one word ("c#4", "cs4", "db4", "a-1"), C4 = 60, or -1. */
int noteFromName (const std::string& word)
{
    static const std::regex pattern ("^([a-g])(#|s|b)?(-?[0-9])$");
    std::smatch m;
    if (! std::regex_match (word, m, pattern))
        return -1;
    static const int semitones[] = { 9, 11, 0, 2, 4, 5, 7 };   // a b c d e f g
    int note = semitones[m[1].str()[0] - 'a'] + (std::stoi (m[3].str()) + 1) * 12;
    if (m[2].matched)
        note += m[2].str() == "b" ? -1 : 1;
    return juce::isPositiveAndBelow (note, 128) ? note : -1;
}

bool isNumber (const juce::String& word)
{
    return word.isNotEmpty() && word.containsOnly ("0123456789") && word.length() <= 3;
}

/** The number after a prefix ("rr2" with "rr"), or -1. */
int numberAfter (const juce::String& word, const juce::StringArray& prefixes)
{
    for (const auto& p : prefixes)
        if (word.startsWith (p) && isNumber (word.substring (p.length())))
            return word.substring (p.length()).getIntValue();
    return -1;
}

struct Found
{
    juce::File file;
    SampleImporter::NameInfo name;
    int note = -1;
    int unityNote = -1;          // the WAV's own root note, from its sampler chunk
    int loopStart = -1, loopEnd = -1;
    double level = 0.0;          // RMS of its start, to order layers the name doesn't
};

double levelOf (juce::AudioFormatReader& reader)
{
    const int length = (int) juce::jmin (reader.lengthInSamples, (juce::int64) (reader.sampleRate * 2.0));
    if (length <= 0)
        return 0.0;
    juce::AudioBuffer<float> audio ((int) reader.numChannels, length);
    reader.read (&audio, 0, length, 0, true, true);
    double sum = 0.0;
    for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        sum += audio.getRMSLevel (ch, 0, length);
    return sum;
}
} // namespace

SampleImporter::NameInfo SampleImporter::parseName (const juce::String& fileName)
{
    NameInfo info;
    juce::StringArray words;
    words.addTokens (fileName.toLowerCase().replaceCharacters ("_-.,()[]{}+&", "            "), " ", {});
    words.removeEmptyStrings();

    static const juce::StringArray dynamics { "ppp", "pp", "p", "mp", "mf", "f", "ff", "fff" };
    juce::StringArray rest;
    juce::Array<int> numbers;   // indexes into words of the plain numbers, decided below
    for (int i = 0; i < words.size(); ++i)
    {
        const auto& w = words[i];
        const bool numberNext = i + 1 < words.size() && isNumber (words[i + 1]);
        if (const int note = noteFromName (w.toStdString()); note >= 0 && info.note < 0)
            info.note = note;
        else if (const int v = numberAfter (w, { "velocity", "velo", "vel", "v" }); v >= 0 && info.layer < 0)
            info.layer = v, info.velocity = true;
        else if ((w == "v" || w == "vel" || w == "velocity") && numberNext && info.layer < 0)
            info.layer = words[++i].getIntValue(), info.velocity = true;
        else if (const int l = numberAfter (w, { "layer", "lyr", "dyn", "vl", "l" }); l >= 0 && info.layer < 0)
            info.layer = l;
        else if (dynamics.contains (w) && info.layer < 0)
            info.layer = dynamics.indexOf (w);
        else if (const int rr = numberAfter (w, { "rr", "take", "seq", "var" }); rr >= 0 && info.roundRobin < 0)
            info.roundRobin = rr;
        else if ((w == "rr" || w == "take") && numberNext && info.roundRobin < 0)
            info.roundRobin = words[++i].getIntValue();
        else if (w == "rel" || w == "release" || w == "releases" || w == "keyup")
            info.release = true;
        else if (isNumber (w))
            numbers.add (i);
        else
            rest.add (w);
    }

    // Plain numbers: a name that is only a number ("60"), or a note and a velocity ("Piano 60 100").
    // Any other number is part of the sound's name ("Tom 2", "Crash 18").
    juce::Array<int> used;
    if (info.note < 0 && numbers.size() >= 1 && (words.size() == 1 || numbers.size() >= 2)
        && words[numbers[0]].getIntValue() <= 127)
    {
        info.note = words[numbers[0]].getIntValue();
        used.add (numbers[0]);
        if (numbers.size() >= 2 && info.layer < 0 && juce::isPositiveAndNotGreaterThan (words[numbers[1]].getIntValue(), 127))
        {
            info.layer = words[numbers[1]].getIntValue();
            info.velocity = true;
            used.add (numbers[1]);
        }
    }
    else if (info.note >= 0 && numbers.size() == 1 && info.layer < 0)
    {
        info.layer = words[numbers[0]].getIntValue();   // "Piano C4 3": the third layer, or velocity 3?
        used.add (numbers[0]);
    }
    for (int i = 0; i < words.size(); ++i)
        if (numbers.contains (i) && ! used.contains (i))
            rest.add (words[i]);
    info.rest = rest.joinIntoString (" ");
    return info;
}

juce::Array<juce::File> SampleImporter::findSamples (const juce::File& folder)
{
    auto files = folder.findChildFiles (juce::File::findFiles, true, "*.wav;*.WAV;*.flac;*.FLAC;*.Wav;*.Flac");
    juce::Array<juce::File> kept;
    for (const auto& f : files)
        if (! f.getFileName().startsWithChar ('.'))   // macOS's ._ files
            kept.add (f);
    struct Natural
    {
        static int compareElements (const juce::File& a, const juce::File& b)
        {
            return a.getFullPathName().compareNatural (b.getFullPathName());
        }
    } natural;
    kept.sort (natural);
    return kept;
}

Library SampleImporter::import (const juce::File& source, const SampleMap& settings, const juce::File& libraryFolder,
                                std::function<bool (double, const juce::String&)> onProgress, juce::String& report)
{
    Library library;
    library.map = settings;
    library.map.groups.clear();
    library.map.sounds.clear();

    const auto files = findSamples (source);
    if (files.isEmpty())
    {
        report = "No WAV or FLAC files in " + source.getFullPathName() + ".";
        return library;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    juce::StringArray skipped, unreadable;

    // Read every file's name and the WAV's own root note and loop, a folder at a time.
    std::vector<juce::String> folderNames;
    std::vector<std::vector<Found>> folders;
    for (int i = 0; i < files.size(); ++i)
    {
        const auto& file = files[i];
        if (onProgress && ! onProgress (0.5 * i / files.size(), file.getFileName()))
        {
            report = "Stopped.";
            return {};
        }
        const auto relative = file.getParentDirectory().getRelativePathFrom (source).replaceCharacter ('\\', '/');
        const auto folderName = relative == "." ? source.getFileName() : relative.replace ("/", " - ");
        auto at = std::find (folderNames.begin(), folderNames.end(), folderName);
        if (at == folderNames.end())
        {
            folderNames.push_back (folderName);
            folders.emplace_back();
            at = folderNames.end() - 1;
        }
        std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (file) };
        if (reader == nullptr)
        {
            unreadable.add (file.getFileName());
            continue;
        }
        Found f;
        f.file = file;
        f.name = parseName (file.getFileNameWithoutExtension());
        f.note = f.name.note;
        const auto& meta = reader->metadataValues;
        if (meta.containsKey ("MidiUnityNote"))
            f.unityNote = juce::jlimit (0, 127, meta["MidiUnityNote"].getIntValue());
        if (meta["NumSampleLoops"].getIntValue() > 0 && meta["Loop0End"].getIntValue() > meta["Loop0Start"].getIntValue())
        {
            f.loopStart = meta["Loop0Start"].getIntValue();
            f.loopEnd = meta["Loop0End"].getIntValue();
        }
        f.level = levelOf (*reader);
        folders[(size_t) (at - folderNames.begin())].push_back (f);
    }

    int nextDrum = 36;   // drums get a key each from C2 up, as General MIDI starts its kit there
    for (size_t g = 0; g < folders.size(); ++g)
    {
        auto& found = folders[g];
        if (found.empty())
            continue;

        // Without notes in the names, the WAVs' own root notes count, unless they all say the
        // same (most editors write 60 into every file).
        const bool namedNotes = std::any_of (found.begin(), found.end(), [] (const Found& f) { return f.note >= 0; });
        if (! namedNotes)
        {
            juce::Array<int> unity;
            for (const auto& f : found)
                unity.addIfNotAlreadyThere (f.unityNote);
            if (unity.size() > 1 && ! unity.contains (-1))
                for (auto& f : found)
                    f.note = f.unityNote;
        }
        Group group;
        group.name = folderNames[g];
        group.pitched = std::any_of (found.begin(), found.end(), [] (const Found& f) { return f.note >= 0; });
        group.oneShot = ! group.pitched;
        group.step = 1;
        group.layers = 1;
        if (! group.pitched)
        {
            // A drum is the words left in its name: Kick_v1 and Kick_v2 are one drum, Tom 1 and Tom 2 two.
            juce::StringArray drums;
            for (const auto& f : found)
                drums.addIfNotAlreadyThere (f.name.rest);
            for (auto& f : found)
                f.note = nextDrum + drums.indexOf (f.name.rest);
            nextDrum += drums.size();
            if (nextDrum > 128)
                for (auto& f : found)
                    if (f.note > 127)
                        skipped.add (f.file.getFileName() + " (no keys left)");
        }
        for (const auto& f : found)
            if (f.note < 0)
                skipped.add (f.file.getFileName() + " (no note in its name)");
        found.erase (std::remove_if (found.begin(), found.end(), [] (const Found& f) { return f.note < 0 || f.note > 127; }),
                     found.end());
        if (found.empty())
            continue;

        const int g2 = (int) library.map.groups.size();
        const auto folderPath = "Samples/" + LibraryRecorder::safeName (group.name) + "/";
        juce::Array<int> notes;
        for (const auto& f : found)
            notes.addIfNotAlreadyThere (f.note);
        notes.sort();

        for (const bool release : { false, true })
            for (const int note : notes)
            {
                std::vector<Found*> takes;
                for (auto& f : found)
                    if (f.note == note && f.name.release == release)
                        takes.push_back (&f);
                if (takes.empty())
                    continue;

                // The note's layers, softest first: from the names, else by how loud the files
                // are when there's more than one and nothing says they're round robins.
                const bool named = std::any_of (takes.begin(), takes.end(), [] (const Found* f) { return f->name.layer >= 0; });
                const bool roundRobins = std::any_of (takes.begin(), takes.end(), [] (const Found* f) { return f->name.roundRobin >= 0; });
                std::map<double, std::vector<Found*>> layers;
                for (auto* f : takes)
                    layers[named ? f->name.layer : (roundRobins || release ? 0.0 : f->level)].push_back (f);
                if (release)
                    layers = { { 0.0, { takes.front() } } };

                // Velocities in the names set the layers' tops; otherwise they share 1-127 evenly.
                const bool velocities = named && std::all_of (takes.begin(), takes.end(), [] (const Found* f)
                                        { return f->name.velocity && juce::isPositiveAndNotGreaterThan ((int) f->name.layer, 127); });
                const int count = (int) layers.size();
                int layer = 0, below = 0;
                for (auto& [key, set] : layers)
                {
                    int top = velocities ? (int) key : juce::roundToInt (127.0 * (layer + 1) / count);
                    if (layer == count - 1)
                        top = 127;
                    top = juce::jlimit (below + 1, 127, top);
                    std::sort (set.begin(), set.end(), [] (const Found* a, const Found* b)
                               { return a->name.roundRobin != b->name.roundRobin ? a->name.roundRobin < b->name.roundRobin
                                                                                 : a->file.getFileName().compareNatural (b->file.getFileName()) < 0; });
                    int take = 0;
                    for (auto* f : set)
                    {
                        Sample s;
                        s.group = g2;
                        s.rootNote = note;
                        s.velocities = { release ? 1 : below + 1, release ? 127 : top };
                        s.roundRobin = ++take;
                        s.roundRobins = (int) set.size();
                        s.release = release;
                        s.path = folderPath + LibraryRecorder::safeName (f->file.getFileName());
                        if (! release && f->loopStart >= 0)
                        {
                            s.loopStart = f->loopStart;
                            s.loopEnd = f->loopEnd;
                            group.loop = true;
                        }
                        library.samples.push_back (s);
                        group.roundRobins = juce::jmax (group.roundRobins, (int) set.size());
                        if (release)
                            break;
                    }
                    below = top;
                    ++layer;
                    if (below >= 127)
                        break;
                }
                if (release)
                    group.releaseSamples = true;
                else
                    group.layers = juce::jmax (group.layers, count);
            }

        // A pitched row plays its lowest sample half an octave further down and its highest up.
        group.lowNote = group.pitched ? juce::jmax (0, notes.getFirst() - 6) : notes.getFirst();
        group.highNote = group.pitched ? juce::jmin (127, notes.getLast() + 6) : notes.getLast();
        group.loop = std::any_of (library.samples.begin(), library.samples.end(), [g2] (const Sample& s) { return s.group == g2 && s.loops(); });
        library.map.groups.push_back (group);

        // The files, into the library: Samples/<row>/<file>.
        for (const auto& f : found)
        {
            const auto target = libraryFolder.getChildFile (folderPath + LibraryRecorder::safeName (f.file.getFileName()));
            if (! target.getParentDirectory().createDirectory())
            {
                report = "Could not make " + target.getParentDirectory().getFullPathName() + ".";
                return {};
            }
            if (target == f.file)
                continue;
            target.deleteFile();
            std::error_code linkError;
            std::filesystem::create_hard_link (std::filesystem::u8path (f.file.getFullPathName().toRawUTF8()),
                                               std::filesystem::u8path (target.getFullPathName().toRawUTF8()), linkError);
            if (linkError && ! f.file.copyFileTo (target))
            {
                report = "Could not copy " + f.file.getFullPathName() + " (is the disk full?).";
                return {};
            }
        }
        if (onProgress && ! onProgress (0.5 + 0.5 * (double) (g + 1) / folders.size(), group.name))
        {
            report = "Stopped.";
            return {};
        }
    }

    // Each sample stretches over the keys up to its neighbours.
    for (int g = 0; g < (int) library.map.groups.size(); ++g)
    {
        juce::Array<int> notes;
        for (const auto& s : library.samples)
            if (s.group == g)
                notes.addIfNotAlreadyThere (s.rootNote);
        notes.sort();
        for (auto& s : library.samples)
            if (s.group == g)
                s.keys = library.map.groups[(size_t) g].keyRangeFor (notes, notes.indexOf (s.rootNote));
    }

    // Rows that play the same keys are articulations: each gets a keyswitch, below every row's keys.
    auto& groups = library.map.groups;
    juce::Array<int> clashing;
    for (int a = 0; a < (int) groups.size(); ++a)
        for (int b = 0; b < (int) groups.size(); ++b)
            if (a != b && groups[(size_t) a].lowNote <= groups[(size_t) b].highNote
                && groups[(size_t) b].lowNote <= groups[(size_t) a].highNote)
                clashing.addIfNotAlreadyThere (a);
    clashing.sort();
    int lowest = 127;
    for (const auto& group : groups)
        lowest = juce::jmin (lowest, group.lowNote);
    const bool switched = clashing.size() > 1 && lowest >= clashing.size();
    if (switched)
    {
        const int first = juce::jmin (24, lowest - clashing.size());   // C1 when there's room
        for (int i = 0; i < clashing.size(); ++i)
            groups[(size_t) clashing[i]].keyswitch = noteName (first + i);
    }

    int loops = 0;
    for (const auto& s : library.samples)
        loops += s.loops() ? 1 : 0;
    report = "Made " + juce::String ((int) groups.size()) + (groups.size() == 1 ? " row" : " rows") + " from "
             + juce::String ((int) library.samples.size()) + " samples";
    report << (loops > 0 ? ", " + juce::String (loops) + " of them looped." : juce::String ("."));
    if (switched)
        report << " Rows that share keys switch from " << noteName (groups[(size_t) clashing[0]].switchNote()) << " up.";
    else if (clashing.size() > 1)
        report << " Some rows share keys and play together: there's no room below them for keyswitches.";
    if (! skipped.isEmpty())
        report << " Left out " << skipped.size() << ": " << skipped.joinIntoString (", ") << ".";
    if (! unreadable.isEmpty())
        report << " Could not read " << unreadable.joinIntoString (", ") << ".";
    return library;
}
} // namespace syn
