# SYNChestra

Record your instrument plugins into sample libraries. SYNChestra hosts a VST3 (or AU/LV2)
instrument, plays it note by note at several velocities, and saves the result as a
**Decent Sampler** (`.dspreset`) and **SFZ** (`.sfz`) library: one set of recordings, two
formats, ready to open in Decent Sampler, sforzando, Sfizz, TAL-Sampler and other SFZ players.

The name is a tribute to Devin Townsend's *Synchestra*.

## Recording a library

1. **Find plugins** scans your plugin folders. Each plugin opens in a separate process, so one
   that crashes is just skipped.
2. Pick a plugin and press **Load plugin**. Its window opens: choose the sound you want there.
   Any connected MIDI keyboard plays it, so you can try it first.
3. Describe what to record in the table. Each row is a group:

   | Column | What it does |
   |---|---|
   | Record | Whether the row is recorded |
   | Name | The group's name, used for its folder and in the presets |
   | Lowest / Highest | The keys the group plays, as note numbers or names (C4 is 60) |
   | Every | A recording every this many keys. The sampler pitches each one to fill the keys between |
   | Layers | Velocity layers, spread evenly up to 127 |
   | Round robins | Takes of every note and layer, played in turn so repeats don't sound identical |
   | Hold (s) | How long each note is held before it's released. The recording keeps the release tail too |
   | Release (s) | How long the sampler fades a note after you let go of the key |
   | Pitched | Off for drums and effects: every key plays its own recording, unpitched |
   | One-shot | On for drums and hits: a recording plays to its end, whatever the key does |
   | Channel | The MIDI channel the notes are sent on (10 for most drum plugins) |

   **Add keys** adds a keyboard range (a recording every 3 keys from C2 to C7 by default).
   **Add drum** adds a single unpitched one-shot note. **Listen** plays the selected row's middle
   note, held as it will be recorded.

   The keyboard at the bottom plays the plugin on the selected row's channel, and shades that
   row's keys. **Find notes** plays every key once on that channel and marks the ones that make
   a sound with a green bar, so you can see which keys a patch uses (handy for drum kits and
   multi-sampled patches with gaps). Right-click a key to set it as the row's lowest or highest
   note, add a drum on it, or set the row to the run of sounding keys around it.
4. Tick **Decent Sampler**, **SFZ** or both, pick the sample rate, and press **Record library**.

The library is saved in the **Save to** folder (Documents/SYNChestra by default):

```
My Library/
  My Library.dspreset
  My Library.sfz
  Samples/
    Keys/Keys_C2_v032.wav
    Keys/Keys_C2_v064.wav
    ...
```

Samples are 24-bit stereo WAV files, trimmed at the first sound and at the end of the tail,
with a short fade so they never click. A note that plays nothing is left out (the status line
names it) and its neighbours stretch over its keys.

## Building from source

You need CMake 3.22+, a C++17 compiler, and on Linux the ALSA, X11 and FreeType development
packages. JUCE 8 is downloaded during the configure step.

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target SYNChestra
```

The checks of the recorder and preset writers run against a stand-in synth:

```
cmake --build build --target SYNChestra_tests
build/SYNChestra_tests_artefacts/Release/SYNChestra_tests /tmp/synchestra-test
```

On Arch Linux, `cd packaging/aur && makepkg -si` builds and installs it with its menu entry.

## Licence

SYNChestra is free software under the GNU Affero General Public License v3 (see LICENSE),
the licence JUCE 8's free tier requires. It covers the app's code only: the libraries you
record with it are yours.

SYNChestra grew out of [eMIMICmkr](https://github.com/Sharkuel/eMIMICmkr), which records drum
plugins into Pearl Mimic Pro instruments, and shares its plugin host.
