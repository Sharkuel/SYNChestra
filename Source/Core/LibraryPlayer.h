#pragma once
#include "Library.h"

namespace syn
{
/** Plays a recorded library from MIDI, roughly as Decent Sampler or an SFZ player would: key
    and velocity ranges, pitching, round robins, loops, release, one-shots, key-up samples and
    articulation keyswitches. Lets a library be tried in the app before opening it elsewhere. */
class LibraryPlayer
{
public:
    /** Reads the library's samples from folder. Returns an error, or empty. Not while rendering. */
    juce::String load (const Library&, const juce::File& folder, double sampleRate);
    bool isLoaded() const { return ! sounds.empty(); }

    /** Plays the MIDI in the block into audio (added to what's there), stereo. */
    void render (juce::AudioBuffer<float>& audio, const juce::MidiBuffer& midi);

    /** Stops every voice at once. */
    void allNotesOff();

private:
    struct Sound
    {
        Sample sample;
        juce::AudioBuffer<float> audio;
        double rateRatio = 1.0;   // file rate / playback rate
    };
    struct Voice
    {
        const Sound* sound = nullptr;
        int note = -1, channel = 1;
        double position = 0.0, step = 1.0;
        float gain = 1.0f, envelope = 1.0f, fadePerSample = 0.0f;
        bool held = false, releasing = false;
    };

    void noteOn (int channel, int note, int velocity);
    void noteOff (int channel, int note);
    void start (const Sound&, int channel, int note, int velocity, bool held);
    bool isActiveArticulation (int group) const;

    Library library;
    std::vector<Sound> sounds;
    std::array<Voice, 48> voices;
    std::map<std::tuple<int, int, int>, int> roundRobinCounter;   // group, root note, layer -> last take
    std::array<int, 128> lastVelocity {};
    int articulation = -1;   // the row whose keyswitch came last
    double sampleRate = 48000.0;
};
} // namespace syn
