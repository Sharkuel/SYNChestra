#pragma once
#include <juce_data_structures/juce_data_structures.h>

namespace syn
{
/** One set of notes recorded the same way: a whole keyboard range sampled every few semitones
    (a piano, a pad), or a single note (one drum, one effect). */
struct Group
{
    juce::String name = "Keys";
    bool enabled = true;
    int lowNote = 36, highNote = 96;   // the keys the group plays, inclusive
    int step = 3;                      // a recording every this many semitones; 0 or 1 records every key
    int layers = 4;                    // velocity layers
    int roundRobins = 1;               // takes per note and layer, played in turn
    double holdSeconds = 2.0;          // note-on to note-off
    double releaseSeconds = 0.3;       // the sampler's fade after key-up
    bool pitched = true;               // false: every key plays its own recording unpitched (drums)
    bool oneShot = false;              // true: a recording plays to its end, whatever the key does
    int midiChannel = 1;

    /** The notes recorded, lowest first: lowNote, every step above it, and always highNote. */
    juce::Array<int> sampledNotes() const;

    /** The velocities recorded, softest first, evenly spread over 1..127, hardest always 127. */
    juce::Array<int> velocities() const;

    /** The keys sampledNotes()[index] covers: from halfway to the note below to halfway to the one above. */
    juce::Range<int> keyRangeFor (int index) const;   // [low, high] inclusive, as getStart()..getEnd()

    /** The same for any set of notes, lowest first: the ones that actually sounded, so a silent
        recording's keys go to its neighbours. */
    juce::Range<int> keyRangeFor (const juce::Array<int>& notes, int index) const;

    /** The velocities velocities()[index] answers: from just above the softer layer up to itself. */
    juce::Range<int> velocityRangeFor (int index) const;

    juce::ValueTree toValueTree() const;
    static Group fromValueTree (const juce::ValueTree&);

    static Group keys();
    static Group drum (const juce::String& name, int note);

    /** Rows for the keys "Find notes" heard, on midiChannel. As drums, every key gets its own row;
        otherwise each run of 4 or more neighbouring keys becomes a key row and shorter runs drums. */
    static std::vector<Group> fromSoundingNotes (const juce::Array<int>& sorted, bool drums, int midiChannel);
};

/** Everything a library is recorded from. */
struct SampleMap
{
    juce::String libraryName = "My Library";
    std::vector<Group> groups;

    int totalRecordings() const;

    juce::ValueTree toValueTree() const;
    static SampleMap fromValueTree (const juce::ValueTree&);
};

/** "C4" for 60, the convention most samplers (and Decent Sampler) use. */
juce::String noteName (int note);
} // namespace syn
