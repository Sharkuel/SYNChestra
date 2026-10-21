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
   | Loop | Sustained recordings get a seamless loop, so a key can be held for as long as you like |
   | Channel | The MIDI channel the notes are sent on (10 for most drum plugins) |
   | Key-up | Also keep the sound of letting go of a key (dampers, fret noise, the room) and play it on key-up |
   | Keyswitch | A note (C1) or CC (CC32=10) sent before each note, to record one articulation |

   **Add keys** adds a keyboard range (a recording every 3 keys from C2 to C7 by default).
   **Add drum** adds a single unpitched one-shot note. **Listen** plays the selected row's middle
   note, held as it will be recorded. **Measure** holds the row's lowest note (it rings longest)
   for up to 20 seconds, then taps it, and sets the row to suit what it heard:

   - a note that keeps ringing after key-up (a drum, a pluck, a bell) becomes a one-shot, so each
     recording keeps its whole ring;
   - a note that dies away while held (a piano, a guitar) gets a Hold long enough to die away;
   - a note that sustains (a pad, an organ, strings) gets a 4 s Hold and Loop.

   Release is set to how long the note takes to fade after key-up.

   With **Loop** on, SYNChestra looks for a loop in the held part of each recording (after the
   attack, before key-up), picks the start whose sound best matches the end, and blends the two so
   the jump back can't be heard. The recording then stops at the loop's end; the sampler's Release
   fades it out after key-up. Notes that die away while held, and holds under a second, aren't
   looped. The loop is written to the .sfz, the .dspreset and the WAV file itself. Recordings
   that aren't looped always keep the whole tail after the hold.

   The keyboard at the bottom plays the plugin on the selected row's channel, and shades that
   row's keys. **Find notes** plays every key once on that channel and marks the ones that make
   a sound with a green bar, so you can see which keys a patch uses (handy for drum kits and
   multi-sampled patches with gaps). It then offers to fill the table: each run of 4 or more
   sounding keys becomes a key row and lone keys become drums, or every key becomes a drum when
   the selected row is a drum or uses channel 10. You can replace the rows or add to them.

   Right-click a key to set it as the row's lowest or highest note, add a drum on it, set the
   row to the run of sounding keys around it, or add a new key row for that run. To draw a key
   row yourself, right-click its first key and choose **Start a new key row**, then right-click
   its last key and choose **End the new key row here**.
   **Key-up sounds.** With **Key-up** ticked, what the plugin plays after each note's key-up is
   also saved as its own sample (`..._rel.wav`, one per note and layer). The library plays it
   when a key is let go, and fades the held sound out quickly so the two don't double up.

   **Articulations.** To record a plugin's articulations (sustain, staccato, pizzicato...), add
   a row for each with the same keys, and type the plugin's keyswitch for it in **Keyswitch**: a
   note such as `C1`, or a CC such as `CC32=10` for plugins that switch with a controller. Each
   row is recorded after its switch. In the library, the first articulation plays to start with;
   Decent Sampler gets a menu to pick another and answers the same keyswitch notes, and the .sfz
   switches with the same notes (or CC values). Selecting a row also sends its keyswitch to the
   plugin, so the on-screen keyboard plays that articulation.
   **Skip same layers** (on by default) compares each velocity layer with the louder one kept
   before it. When the plugin only turned the volume down, the softer layer is left out, the
   louder recording covers its velocities, and the sampler plays it softer. Layers whose sound
   really changes (a brighter attack, a different articulation) are all kept.

4. Tick **Decent Sampler**, **SFZ** or both, pick the sample rate, and press **Record library**.

   The Decent Sampler panel has Volume and Release knobs. With **Effect knobs** ticked it also
   gets Tone (a low-pass filter), Chorus, Delay, Reverb and Room knobs, all off to start so the
   library sounds as recorded. **Background...** picks a picture for the panel; it's copied to
   `Images/background.png`, scaled and cropped to the panel's 812 x 375 pixels. SFZ has no
   standard panel, so these only apply to the .dspreset.

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
