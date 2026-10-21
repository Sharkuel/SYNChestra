#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace syn
{
/** Plays single notes through a hosted plugin offline and captures them in stereo. */
class NoteRenderer
{
public:
    struct Settings
    {
        double sampleRate = 48000.0;
        int blockSize = 512;
        double maxSeconds = 30.0;        // the longest recording kept, note and tail together
        double startDb = -70.0;          // a recording starts at the first sound above this...
        double tailDb = -70.0;           // ...and ends once, after note-off, it stays this far below its peak
        double floorDb = -96.0;          //    or below this, whichever is louder (plugin noise)...
        double silenceSeconds = 0.3;     // ...for this long
        double fadeSeconds = 0.01;       // a short fade-out at the cut, so the end never clicks
        double giveUpSeconds = 1.0;      // a note still silent after this plays nothing
    };

    NoteRenderer (juce::AudioPluginInstance& plugin, Settings settings);
    ~NoteRenderer();

    /** Plays one note held for holdSeconds and returns it in stereo from its first sound to the
        end of its tail. Empty when the note makes no sound. noteOffSample, if given, gets where
        in the result the key came up. */
    juce::AudioBuffer<float> render (int note, int velocity, double holdSeconds, int midiChannel = 1,
                                     int* noteOffSample = nullptr);

    const Settings& getSettings() const { return settings; }

private:
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);
    void waitForSilence (int midiChannel);
    void addMainOutput (juce::AudioBuffer<float>& dest, int destStart, const juce::AudioBuffer<float>& src, int n);

    juce::AudioPluginInstance& plugin;
    Settings settings;
};
} // namespace syn
