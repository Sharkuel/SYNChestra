#include "LibraryPlayer.h"

namespace syn
{
juce::String LibraryPlayer::load (const Library& lib, const juce::File& folder, double rate)
{
    library = lib;
    sounds.clear();
    roundRobinCounter.clear();
    for (auto& v : voices)
        v = {};
    sampleRate = rate;
    const auto articulations = library.articulationRows();
    articulation = articulations.empty() ? -1 : articulations.front();

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    for (const auto& s : library.samples)
    {
        std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (folder.getChildFile (s.path)) };
        if (reader == nullptr)
            return "Could not read " + s.path + ".";
        Sound sound;
        sound.sample = s;
        sound.rateRatio = reader->sampleRate / rate;
        sound.audio.setSize (2, (int) reader->lengthInSamples);
        reader->read (&sound.audio, 0, sound.audio.getNumSamples(), 0, true, true);
        sounds.push_back (std::move (sound));
    }
    return {};
}

bool LibraryPlayer::isActiveArticulation (int group) const
{
    const auto& g = library.map.groups[(size_t) group];
    const bool switched = g.switchNote() >= 0 || g.switchCc().first >= 0;
    return ! switched || articulation < 0 || group == articulation;
}

void LibraryPlayer::start (const Sound& sound, int channel, int note, int velocity, bool held)
{
    // A free voice, or else the quietest.
    Voice* voice = &voices[0];
    for (auto& v : voices)
    {
        if (v.sound == nullptr)
        {
            voice = &v;
            break;
        }
        if (v.envelope * v.gain < voice->envelope * voice->gain)
            voice = &v;
    }
    const auto& group = library.map.groups[(size_t) sound.sample.group];
    *voice = {};
    voice->sound = &sound;
    voice->note = note;
    voice->channel = channel;
    voice->held = held;
    const double semitones = group.pitched ? note - sound.sample.rootNote : 0.0;
    voice->step = std::pow (2.0, semitones / 12.0) * sound.rateRatio;
    // Velocities below the top of the layer play it softer, as the samplers do.
    const float v = (float) velocity / (float) juce::jmax (1, sound.sample.velocities.getEnd());
    voice->gain = juce::jlimit (0.0f, 1.0f, v * v);
}

void LibraryPlayer::noteOn (int channel, int note, int velocity)
{
    // A keyswitch picks an articulation and plays nothing.
    for (const int g : library.articulationRows())
        if (library.map.groups[(size_t) g].switchNote() == note)
        {
            articulation = g;
            return;
        }
    lastVelocity[(size_t) note] = velocity;
    std::map<std::tuple<int, int, int>, std::vector<const Sound*>> candidates;
    for (const auto& sound : sounds)
    {
        const auto& s = sound.sample;
        if (! s.release && s.keys.getStart() <= note && note <= s.keys.getEnd()
                && s.velocities.getStart() <= velocity && velocity <= s.velocities.getEnd() && isActiveArticulation (s.group))
            candidates[{ s.group, s.rootNote, s.velocities.getEnd() }].push_back (&sound);
    }
    for (auto& [key, takes] : candidates)
    {
        auto& last = roundRobinCounter[key];
        last = last % (int) takes.size() + 1;
        for (const auto* sound : takes)
            if (sound->sample.roundRobin == last || takes.size() == 1)
            {
                start (*sound, channel, note, velocity, true);
                break;
            }
    }
}

void LibraryPlayer::noteOff (int channel, int note)
{
    for (auto& v : voices)
        if (v.sound != nullptr && v.held && v.note == note && v.channel == channel)
        {
            v.held = false;
            const auto& group = library.map.groups[(size_t) v.sound->sample.group];
            if (group.oneShot)
                continue;   // plays to its end
            v.releasing = true;
            const double seconds = juce::jmax (0.005, library.mainRelease (v.sound->sample.group));
            v.fadePerSample = (float) (1.0 / (seconds * sampleRate));
        }
    // Key-up sounds.
    const int velocity = juce::jmax (1, lastVelocity[(size_t) note]);
    for (const auto& sound : sounds)
    {
        const auto& s = sound.sample;
        if (s.release && s.keys.getStart() <= note && note <= s.keys.getEnd()
            && s.velocities.getStart() <= velocity && velocity <= s.velocities.getEnd() && isActiveArticulation (s.group))
            start (sound, channel, note, velocity, false);
    }
}

void LibraryPlayer::allNotesOff()
{
    for (auto& v : voices)
        v = {};
}

void LibraryPlayer::render (juce::AudioBuffer<float>& audio, const juce::MidiBuffer& midi)
{
    int position = 0;
    auto renderUpTo = [&] (int end)
    {
        for (auto& v : voices)
        {
            if (v.sound == nullptr)
                continue;
            const auto& src = v.sound->audio;
            const auto& s = v.sound->sample;
            const bool looping = s.loops();   // on through the release fade, which a loop's cut end needs
            for (int i = position; i < end; ++i)
            {
                if (looping && v.position > s.loopEnd)
                    v.position -= (s.loopEnd + 1 - s.loopStart);
                const int at = (int) v.position;
                if (at + 1 >= src.getNumSamples() || v.envelope <= 0.0f)
                {
                    v.sound = nullptr;
                    break;
                }
                const float frac = (float) (v.position - at);
                const float level = v.gain * v.envelope;
                for (int ch = 0; ch < juce::jmin (2, audio.getNumChannels()); ++ch)
                {
                    const float* x = src.getReadPointer (ch);
                    audio.addSample (ch, i, level * (x[at] + frac * (x[at + 1] - x[at])));
                }
                v.position += v.step;
                if (v.releasing)
                    v.envelope -= v.fadePerSample;
            }
        }
        position = end;
    };
    for (const auto m : midi)
    {
        renderUpTo (juce::jlimit (0, audio.getNumSamples(), m.samplePosition));
        const auto msg = m.getMessage();
        if (msg.isNoteOn())
            noteOn (msg.getChannel(), msg.getNoteNumber(), msg.getVelocity());
        else if (msg.isNoteOff())
            noteOff (msg.getChannel(), msg.getNoteNumber());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            allNotesOff();
        else if (msg.isController())
            for (const int g : library.articulationRows())
                if (library.map.groups[(size_t) g].switchCc() == std::pair<int, int> { msg.getControllerNumber(), msg.getControllerValue() })
                    articulation = g;
    }
    renderUpTo (audio.getNumSamples());
}
} // namespace syn
