#include "NoteRenderer.h"

namespace syn
{
namespace
{
float peakOf (const juce::AudioBuffer<float>& b, int start, int n)
{
    float peak = 0.0f;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        peak = juce::jmax (peak, b.getMagnitude (ch, start, n));
    return peak;
}
} // namespace

NoteRenderer::NoteRenderer (juce::AudioPluginInstance& p, Settings s) : plugin (p), settings (s)
{
    plugin.setNonRealtime (true);
    plugin.setRateAndBufferSizeDetails (settings.sampleRate, settings.blockSize);
    plugin.prepareToPlay (settings.sampleRate, settings.blockSize);
}

NoteRenderer::~NoteRenderer()
{
    plugin.releaseResources();
    plugin.setNonRealtime (false);
}

void NoteRenderer::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    buffer.clear();
    const juce::ScopedLock lock (plugin.getCallbackLock());
    plugin.processBlock (buffer, midi);
    midi.clear();
}

void NoteRenderer::waitForSilence (int midiChannel)
{
    // Let the last note ring out (and stop any still held) so nothing bleeds into the next recording.
    const int channels = juce::jmax (plugin.getTotalNumInputChannels(), plugin.getTotalNumOutputChannels(), 2);
    juce::AudioBuffer<float> buffer (channels, settings.blockSize);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::allNotesOff (midiChannel), 0);
    const float threshold = juce::Decibels::decibelsToGain ((float) settings.floorDb);
    const int maxBlocks = (int) (settings.maxSeconds * settings.sampleRate / settings.blockSize);
    for (int i = 0; i < maxBlocks; ++i)
    {
        processBlock (buffer, midi);
        if (peakOf (buffer, 0, buffer.getNumSamples()) < threshold)
            return;
    }
}

void NoteRenderer::addMainOutput (juce::AudioBuffer<float>& dest, int destStart,
                                  const juce::AudioBuffer<float>& src, int n)
{
    // The main output only: a mono one goes to both sides, and extra busses (multi-outs) are left out.
    auto* bus = plugin.getBus (false, 0);
    const int outs = juce::jlimit (1, juce::jmin (2, src.getNumChannels()),
                                   bus != nullptr ? bus->getNumberOfChannels() : 2);
    for (int ch = 0; ch < 2; ++ch)
        dest.copyFrom (ch, destStart, src, juce::jmin (ch, outs - 1), 0, n);
}

juce::AudioBuffer<float> NoteRenderer::render (int note, int velocity, double holdSeconds, int midiChannel)
{
    midiChannel = juce::jlimit (1, 16, midiChannel);
    waitForSilence (midiChannel);

    const int channels = juce::jmax (plugin.getTotalNumInputChannels(), plugin.getTotalNumOutputChannels(), 2);
    const int block = settings.blockSize;
    const int maxSamples = (int) (settings.maxSeconds * settings.sampleRate);
    const int noteOffAt = juce::jlimit (1, maxSamples, (int) (holdSeconds * settings.sampleRate));
    const int silentNeeded = (int) (settings.silenceSeconds * settings.sampleRate);
    const float start = juce::Decibels::decibelsToGain ((float) settings.startDb);
    const float tailGain = juce::Decibels::decibelsToGain ((float) settings.tailDb);
    const float floor = juce::Decibels::decibelsToGain ((float) settings.floorDb);

    juce::AudioBuffer<float> recorded (2, maxSamples);
    recorded.clear();
    juce::AudioBuffer<float> buffer (channels, block);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (midiChannel, note, (juce::uint8) juce::jlimit (1, 127, velocity)), 0);

    float peak = 0.0f;
    int written = 0, silentRun = 0, firstSound = -1, lastSound = 0;
    while (written < maxSamples)
    {
        if (written <= noteOffAt && noteOffAt < written + block)
            midi.addEvent (juce::MidiMessage::noteOff (midiChannel, note), noteOffAt - written);

        processBlock (buffer, midi);
        const int n = juce::jmin (block, maxSamples - written);
        addMainOutput (recorded, written, buffer, n);

        for (int i = 0; i < n; ++i)
        {
            const int at = written + i;
            const float level = juce::jmax (std::abs (recorded.getSample (0, at)), std::abs (recorded.getSample (1, at)));
            peak = juce::jmax (peak, level);
            const float end = juce::jlimit (floor, start, peak * tailGain);
            if (level >= (firstSound < 0 ? start : end))
            {
                if (firstSound < 0)
                    firstSound = at;
                lastSound = at;
                silentRun = 0;
            }
            else if (firstSound >= 0)
            {
                ++silentRun;
            }
        }
        written += n;
        // A held note can go quiet (a slow swell, a gap) without being over: only stop after note-off.
        if (firstSound >= 0 && silentRun >= silentNeeded && written > noteOffAt)
            break;
        if (firstSound < 0 && written >= settings.giveUpSeconds * settings.sampleRate)
            break;
    }

    if (firstSound < 0)
        return {};

    // Keep a few samples before the first sound, so the attack isn't shaved.
    firstSound = juce::jmax (0, firstSound - 32);
    const int length = lastSound - firstSound + 1;
    juce::AudioBuffer<float> out (2, length);
    for (int ch = 0; ch < 2; ++ch)
        out.copyFrom (ch, 0, recorded, ch, firstSound, length);
    const int fade = juce::jmin (length, (int) (settings.fadeSeconds * settings.sampleRate));
    if (fade > 0)
        out.applyGainRamp (length - fade, fade, 1.0f, 0.0f);
    return out;
}
} // namespace syn
