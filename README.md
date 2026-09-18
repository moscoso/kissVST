# Kiss

A VST3 delay + reverb plugin, built with [JUCE](https://juce.com) as a learning project.

```
in --> [ Delay: Time / Feedback / Mix ] --> [ Reverb: Size / Damping / Width / Mix ] --> out
```

## Layout

| File | What it is |
|---|---|
| `CMakeLists.txt` | Build definition. Declares the plugin (name, formats, codes) and links the JUCE modules it uses. |
| `Source/PluginProcessor.h/.cpp` | The **audio** side: parameter definitions and the DSP that runs on the audio thread. |
| `Source/PluginEditor.h/.cpp` | The **UI** side: the window with knobs. Talks to the processor only through parameters. |
| `../JUCE` | The JUCE framework checkout (sibling folder, not part of this repo). |

## Building

Requirements: Visual Studio 2022 Build Tools (C++ workload), CMake 3.22+, JUCE checked out at `../JUCE`.

Configure once (generates a Visual Studio solution in `build/`):

```bash
cmake -B build -G "Visual Studio 17 2022" -A x64
```

Build (repeat after every code change):

```bash
cmake --build build --config Release
```

Outputs land in `build/Kiss_artefacts/Release/`:

- `VST3/Kiss.vst3` — the plugin. It's also copied to `C:\Program Files\Common Files\VST3\` automatically
  (turn that off with `-DKISS_COPY_PLUGIN=OFF` at configure time).
- `Standalone/Kiss.exe` — the same plugin wrapped as a normal app, with its own audio device settings.
  Fastest way to hear a change without opening Ableton.

Use `--config Debug` instead of `Release` for a debuggable build (much slower audio processing, but
JUCE's internal `jassert` checks are on and will catch mistakes like reading a parameter that doesn't exist).

## Loading in Ableton Live

Live 10.1 or newer: Preferences → Plug-Ins → turn on **Use VST3 Plug-In System Folders**, then **Rescan**.
"Kiss" shows up in the browser under Plug-Ins → VST3 → Chris.

After rebuilding, Live needs to reload the plugin: remove and re-add it on the track, or rescan.
Live keeps the .vst3 file open while the plugin is on a track, so if the post-build copy fails with
"access denied", remove the plugin from the track (or close Live) and rebuild.

## Where to go from here

Ideas roughly in order of difficulty, all confined to the files above:

1. **Change the look** — colours in `PluginEditor::paint()`, sizes in `resized()`.
2. **Add a parameter** — e.g. an output gain: add it in `createParameterLayout()`, read it in
   `processBlock()`, add a `Knob` for it in the editor. Three places, always the same three.
3. **Custom knob drawing** — subclass `juce::LookAndFeel_V4` and override `drawRotarySlider()`.
4. **Tempo-synced delay** — read the host's BPM with `getPlayHead()->getPosition()` and offer
   note-length choices (1/4, 1/8, dotted...) via a `juce::AudioParameterChoice`.
5. **Ping-pong delay** — cross-feed the left channel's echo into the right delay line and vice versa.
6. **Your own reverb** — replace `juce::dsp::Reverb` with a hand-built comb/allpass network.
