#pragma once
#include "Library.h"

namespace syn
{
/** Makes a library from WAV and FLAC files someone already has, instead of recording a plugin:
    each folder becomes a row, and each file's note, velocity layer and round robin come from its
    name ("Piano_C#4_v100_rr2.wav", "Strings 60 127.flac", "Kick.wav"). */
class SampleImporter
{
public:
    /** What a file's name says about it. */
    struct NameInfo
    {
        int note = -1;              // from a note name ("C#4", "Cs4", "Db4", C4 = 60) or a note number
        double layer = -1.0;        // orders the velocity layers of a note, softest first; -1 when the name has none
        bool velocity = false;      // layer is the velocity the file was played at (v100, or "60 100")
        int roundRobin = -1;        // rr2, take 2, seq 2
        bool release = false;       // the sound after key-up: "rel", "release"
        juce::String rest;          // the other words, lower case: tells drums ("kick", "snare") apart
    };
    static NameInfo parseName (const juce::String& fileNameWithoutExtension);

    /** The WAV and FLAC files in a folder and the folders inside it. */
    static juce::Array<juce::File> findSamples (const juce::File& folder);

    /** Puts the samples found in source into libraryFolder/Samples (hard links where the disk
        allows, copies elsewhere) and maps them, with settings' name, panel and pictures.
        Folders with notes in their files' names are pitched rows that stretch each sample over
        the keys up to its neighbours; folders without are drums, one key each from C2 up. Rows
        that would play the same keys get keyswitches below them. onProgress returns false to stop. */
    static Library import (const juce::File& source, const SampleMap& settings, const juce::File& libraryFolder,
                           std::function<bool (double progress, const juce::String& file)> onProgress, juce::String& report);
};
} // namespace syn
