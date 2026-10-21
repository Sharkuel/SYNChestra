#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

namespace syn
{
/** Finds a seamless loop in the held part of a sustained recording, so a sampler can hold the
    note for as long as the key is down. */
struct Loop
{
    int start = -1, end = -1;   // the loop plays samples start .. end - 1, then jumps back to start
    bool isValid() const { return start >= 0 && end > start; }
};

class Looper
{
public:
    /** Looks for a loop in the first heldSamples of audio (the part played while the key was
        down). Returns an invalid loop when the note is too short or dies away while held, since
        a looped decay would hang at one level. */
    static Loop find (const juce::AudioBuffer<float>& audio, int heldSamples, double sampleRate);

    /** Blends the end of the loop into what comes before its start, so the jump back is
        seamless, and drops everything after the loop. */
    static void apply (juce::AudioBuffer<float>& audio, Loop, double sampleRate);
};
} // namespace syn
