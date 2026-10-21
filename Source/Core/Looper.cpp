#include "Looper.h"

namespace syn
{
namespace
{
float rms (const juce::AudioBuffer<float>& audio, int start, int length)
{
    float sum = 0.0f;
    for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        sum += audio.getRMSLevel (ch, start, length);
    return sum / (float) juce::jmax (1, audio.getNumChannels());
}

/** Where the left and right channels together rise through zero, between from and to. */
juce::Array<int> risingZeroCrossings (const juce::AudioBuffer<float>& audio, int from, int to)
{
    juce::Array<int> found;
    auto mono = [&audio] (int i)
    {
        float v = 0.0f;
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
            v += audio.getSample (ch, i);
        return v;
    };
    for (int i = juce::jmax (1, from); i < to; ++i)
        if (mono (i - 1) < 0.0f && mono (i) >= 0.0f)
            found.add (i);
    return found;
}
} // namespace

Loop Looper::find (const juce::AudioBuffer<float>& audio, int heldSamples, double sampleRate)
{
    const int held = juce::jmin (heldSamples, audio.getNumSamples());
    const int window = juce::jmax (64, (int) (sampleRate * 0.02));       // compared before each point
    const int shortest = (int) (sampleRate * 0.25);
    if (held < (int) (sampleRate * 1.0))
        return {};

    // Leave the attack alone and stop a little before key-up, where some plugins start to change.
    const int searchFrom = juce::jmax (window * 2, (int) (held * 0.3));
    const int endTarget = (int) (held * 0.95);
    if (endTarget - searchFrom < shortest)
        return {};

    // A note that fades while held is a decay, not a sustain: looping it would sound wrong.
    if (rms (audio, endTarget - window, window) < 0.5f * rms (audio, searchFrom, window))
        return {};

    auto ends = risingZeroCrossings (audio, endTarget - (int) (sampleRate * 0.05), endTarget);
    const int end = ends.isEmpty() ? endTarget : ends.getLast();
    // Long loops repeat less audibly: at least half of the searchable stretch.
    const int longest = juce::jmax (shortest, (end - searchFrom) / 2);
    auto starts = risingZeroCrossings (audio, searchFrom, end - longest);
    if (starts.isEmpty())   // no clear crossings (a noisy sound): try evenly spaced points
        for (int i = searchFrom; i < end - longest; i += 64)
            starts.add (i);
    if (starts.isEmpty())
        return {};

    // The start whose lead-in looks most like the end's lead-in, so the blend between them is
    // inaudible. Among near-equals, the longer loop wins: it repeats less obviously.
    const float energy = juce::jmax (1.0e-9f, rms (audio, end - window, window));
    double bestCost = std::numeric_limits<double>::max();
    int best = -1;
    for (const int s : starts)
    {
        double diff = 0.0;
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        {
            const float* x = audio.getReadPointer (ch);
            for (int i = 1; i <= window; ++i)
            {
                const float d = x[end - i] - x[s - i];
                diff += (double) d * d;
            }
        }
        const double error = std::sqrt (diff / (window * audio.getNumChannels())) / energy;
        const double cost = error * (1.0 + 0.2 * (double) (s - searchFrom) / (end - searchFrom));
        if (cost < bestCost)
        {
            bestCost = cost;
            best = s;
        }
    }
    return best >= 0 ? Loop { best, end } : Loop {};
}

void Looper::apply (juce::AudioBuffer<float>& audio, Loop loop, double sampleRate)
{
    if (! loop.isValid() || loop.end > audio.getNumSamples())
        return;
    const int blend = juce::jmin ((int) (sampleRate * 0.1), (loop.end - loop.start) / 2, loop.start);
    for (int ch = 0; ch < audio.getNumChannels(); ++ch)
    {
        float* x = audio.getWritePointer (ch);
        for (int i = 0; i < blend; ++i)
        {
            // Towards the end, more and more of what leads into the start: at the jump they meet.
            const float t = (float) (i + 1) / (float) blend;
            const int at = loop.end - blend + i;
            x[at] = x[at] * (1.0f - t) + x[loop.start - blend + i] * t;
        }
    }
    audio.setSize (audio.getNumChannels(), loop.end, true);
}
} // namespace syn
