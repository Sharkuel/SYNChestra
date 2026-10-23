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
    int loopStart = -1, loopEnd = -1;   // inclusive sample frames, when the recording loops
    bool release = false;       // the sound after key-up, played when the key is let go
    bool loops() const { return loopStart >= 0 && loopEnd > loopStart; }
};

/** A recorded library: the map it came from and its samples. */
struct Library
{
    SampleMap map;
    std::vector<Sample> samples;

    /** A group's release on key-up: shorter when it has key-up samples to take over. */
    double mainRelease (int group) const;

    /** The rows with recordings and a keyswitch: the library's articulations, in table order. */
    std::vector<int> articulationRows() const;

    /** The rows of one sound (-1: the rows without one) and their samples, as a library of its own
        named "<Library> - <Sound>". */
    Library forSound (int sound) const;

    /** The sounds with recordings, in table order: -1 for rows without one. */
    std::vector<int> soundsRecorded() const;

    /** <Library>.sfz, for sforzando, Sfizz, TAL-Sampler and other SFZ players. */
    juce::String toSfz() const;

    /** <Library>.dspreset, for Decent Sampler. */
    juce::String toDecentSampler() const;

    /** <Library>.floe.lua, for Floe: one Floe instrument per row, by the given author. */
    juce::String toFloe (const juce::String& author) const;

    /** Writes the files asked for next to the Samples folder: with several sounds, a .sfz and a
        .dspreset for each, and one .floe.lua with all of them. Returns an error, or empty. */
    juce::String writePresets (const juce::File& folder, bool sfz, bool decentSampler, bool floe = false) const;

    /** Puts the samples recorded in one folder into another as well, for a library saved once per
        format: hard links where the disk allows (no extra space), copies elsewhere. Returns an
        error, or empty. */
    juce::String shareSamples (const juce::File& from, const juce::File& to) const;
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

    /** How a row's notes ring, measured on its lowest note at full velocity, and the Hold,
        Release and One-shot settings that follow from it. */
    struct Timing
    {
        bool sounds = false;        // false: the note plays nothing, and nothing below is set
        bool sustains = false;      // still sounding after being held for the whole test
        bool oneShot = false;       // lets go of the key and rings on as if still held
        double ringSeconds = 0.0;   // held, how long until it fades 60 dB below its peak
        double hold = 2.0, release = 0.3;
    };
    static Timing measureTiming (juce::AudioPluginInstance&, NoteRenderer::Settings, const Group&);

    /** Whether two recordings of a note are the same sound apart from their level: the plugin
        only turned the volume down for the softer one. */
    static bool soundsTheSame (const juce::AudioBuffer<float>& louder, const juce::AudioBuffer<float>& softer,
                               double sampleRate);

    /** Switches the plugin to a sound and waits until it plays the row's middle note steadily, as
        big instruments (Kontakt's) load their samples in the background. keepGoing returns false
        to stop. Returns false when stopped, or when it never played within maxSeconds. */
    static bool switchSound (juce::AudioPluginInstance&, const Sound&, NoteRenderer&, const Group&,
                             std::function<bool()> keepGoing, double maxSeconds = 180.0, int checkEveryMs = 1000);

    /** Strips characters file systems reject. */
    static juce::String safeName (const juce::String&);

    /** A 24-bit WAV, or FLAC when the file's name ends in .flac. With a loop, a WAV also carries
        a sampler chunk with the loop and root note, which most samplers read on their own; FLAC
        has no such chunk, so there the loop is only in the presets. */
    static bool writeSample (const juce::File&, const juce::AudioBuffer<float>&, double sampleRate,
                             const Sample* loopInfo = nullptr);
};
} // namespace syn
