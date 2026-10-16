#include "SampleMap.h"

namespace syn
{
juce::Array<int> Group::sampledNotes() const
{
    juce::Array<int> notes;
    const int low = juce::jlimit (0, 127, juce::jmin (lowNote, highNote));
    const int high = juce::jlimit (0, 127, juce::jmax (lowNote, highNote));
    const int every = juce::jmax (1, step);
    for (int n = low; n <= high; n += every)
        notes.add (n);
    if (notes.getLast() != high)
        notes.add (high);
    return notes;
}

juce::Array<int> Group::velocities() const
{
    juce::Array<int> v;
    const int count = juce::jlimit (1, 127, layers);
    for (int i = 1; i <= count; ++i)
        v.addIfNotAlreadyThere (juce::jmax (1, juce::roundToInt (127.0 * i / count)));
    return v;
}

juce::Range<int> Group::keyRangeFor (int index) const
{
    return keyRangeFor (sampledNotes(), index);
}

juce::Range<int> Group::keyRangeFor (const juce::Array<int>& notes, int index) const
{
    if (! pitched)
        return { notes[index], notes[index] };
    const int low = index == 0 ? juce::jmin (lowNote, highNote) : (notes[index - 1] + notes[index]) / 2 + 1;
    const int high = index == notes.size() - 1 ? juce::jmax (lowNote, highNote) : (notes[index] + notes[index + 1]) / 2;
    return { low, high };
}

juce::Range<int> Group::velocityRangeFor (int index) const
{
    const auto v = velocities();
    return { index == 0 ? 1 : v[index - 1] + 1, v[index] };
}

juce::ValueTree Group::toValueTree() const
{
    juce::ValueTree t ("Group");
    t.setProperty ("name", name, nullptr);
    t.setProperty ("enabled", enabled, nullptr);
    t.setProperty ("lowNote", lowNote, nullptr);
    t.setProperty ("highNote", highNote, nullptr);
    t.setProperty ("step", step, nullptr);
    t.setProperty ("layers", layers, nullptr);
    t.setProperty ("roundRobins", roundRobins, nullptr);
    t.setProperty ("holdSeconds", holdSeconds, nullptr);
    t.setProperty ("releaseSeconds", releaseSeconds, nullptr);
    t.setProperty ("pitched", pitched, nullptr);
    t.setProperty ("oneShot", oneShot, nullptr);
    t.setProperty ("midiChannel", midiChannel, nullptr);
    return t;
}

Group Group::fromValueTree (const juce::ValueTree& t)
{
    Group g;
    g.name = t.getProperty ("name", g.name);
    g.enabled = t.getProperty ("enabled", g.enabled);
    g.lowNote = t.getProperty ("lowNote", g.lowNote);
    g.highNote = t.getProperty ("highNote", g.highNote);
    g.step = t.getProperty ("step", g.step);
    g.layers = t.getProperty ("layers", g.layers);
    g.roundRobins = t.getProperty ("roundRobins", g.roundRobins);
    g.holdSeconds = t.getProperty ("holdSeconds", g.holdSeconds);
    g.releaseSeconds = t.getProperty ("releaseSeconds", g.releaseSeconds);
    g.pitched = t.getProperty ("pitched", g.pitched);
    g.oneShot = t.getProperty ("oneShot", g.oneShot);
    g.midiChannel = t.getProperty ("midiChannel", g.midiChannel);
    return g;
}

Group Group::keys()
{
    return {};
}

Group Group::drum (const juce::String& name, int note)
{
    Group g;
    g.name = name;
    g.lowNote = g.highNote = note;
    g.step = 1;
    g.layers = 8;
    g.roundRobins = 3;
    g.holdSeconds = 0.1;
    g.pitched = false;
    g.oneShot = true;
    g.midiChannel = 10;
    return g;
}

std::vector<Group> Group::fromSoundingNotes (const juce::Array<int>& sorted, bool drums, int midiChannel)
{
    std::vector<Group> rows;
    for (int i = 0; i < sorted.size();)
    {
        int j = i;
        while (j + 1 < sorted.size() && sorted[j + 1] == sorted[j] + 1)
            ++j;
        const int low = sorted[i], high = sorted[j];
        if (drums || high - low < 3)
        {
            for (int note = low; note <= high; ++note)
            {
                rows.push_back (drum (noteName (note), note));
                rows.back().midiChannel = midiChannel;
            }
        }
        else
        {
            auto g = keys();
            g.name = "Keys " + noteName (low) + "-" + noteName (high);
            g.lowNote = low;
            g.highNote = high;
            g.midiChannel = midiChannel;
            rows.push_back (g);
        }
        i = j + 1;
    }
    return rows;
}

int SampleMap::totalRecordings() const
{
    int total = 0;
    for (const auto& g : groups)
        if (g.enabled)
            total += g.sampledNotes().size() * g.velocities().size() * juce::jmax (1, g.roundRobins);
    return total;
}

juce::ValueTree SampleMap::toValueTree() const
{
    juce::ValueTree t ("SampleMap");
    t.setProperty ("libraryName", libraryName, nullptr);
    t.setProperty ("effects", effects, nullptr);
    t.setProperty ("backgroundImage", backgroundImage, nullptr);
    for (const auto& g : groups)
        t.appendChild (g.toValueTree(), nullptr);
    return t;
}

SampleMap SampleMap::fromValueTree (const juce::ValueTree& t)
{
    SampleMap m;
    m.libraryName = t.getProperty ("libraryName", m.libraryName);
    m.effects = t.getProperty ("effects", m.effects);
    m.backgroundImage = t.getProperty ("backgroundImage", m.backgroundImage);
    for (const auto& child : t)
        if (child.hasType ("Group"))
            m.groups.push_back (Group::fromValueTree (child));
    return m;
}

juce::String noteName (int note)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[((note % 12) + 12) % 12]) + juce::String (note / 12 - 1);
}
} // namespace syn
