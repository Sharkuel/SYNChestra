#pragma once
#include "SampleMap.h"
#include "NoteRenderer.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace syn
{
/** One recording in a library, and where it sits on the keyboard. */
struct Sample
{
    int group = 0;              // index into the map's groups
    juce::String path;          // relative to the library folder, with forward slashes
    int rootNote = 60;
    juce::Range<int> keys;      // inclusive: getStart() .. getEnd()
    juce::Range<int> velocities;
    int roundRobin = 1;         // 1-based take number
    int roundRobins = 1;        // takes for this note and layer
};

/** A recorded library: the map it came from and its samples. */
struct Library
{
    SampleMap map;
    std::vector<Sample> samples;

    /** <Library>.sfz, for sforzando, Sfizz, TAL-Sampler and other SFZ players. */
    juce::String toSfz() const;

    /** <Library>.dspreset, for Decent Sampler. */
    juce::String toDecentSampler() const;

    /** Writes the files asked for next to the Samples folder. Returns an error, or empty. */
    juce::String writePresets (const juce::File& folder, bool sfz, bool decentSampler) const;
};

/** Records a SampleMap from a plugin into <root>/<library>/Samples/<group>/ as 24-bit stereo WAVs. */
class LibraryRecorder
{
public:
    struct Progress
    {
        int done = 0, total = 0;
        juce::String current;   // "Keys C4 v96 take 2"
    };

    /** Records every enabled group. onProgress returns false to stop early; what's recorded so
        far is kept. Silent notes are skipped and listed in report. */
    static Library record (juce::AudioPluginInstance&, const SampleMap&, const NoteRenderer::Settings&,
                           const juce::File& libraryFolder, std::function<bool (const Progress&)> onProgress,
                           juce::String& report);

    /** Plays every MIDI note once, briefly, at velocity 100 on midiChannel and returns those that
        make a sound. onProgress gets the note being tried and returns false to stop. */
    static juce::Array<int> findSoundingNotes (juce::AudioPluginInstance&, NoteRenderer::Settings, int midiChannel,
                                               std::function<bool (int note)> onProgress);

    /** Strips characters file systems reject. */
    static juce::String safeName (const juce::String&);

    static bool writeWav (const juce::File&, const juce::AudioBuffer<float>&, double sampleRate);
};
} // namespace syn
