# acdc-patches

[Deutsch](README.de.md)

OWL patches for the **Befaco / Rebel Technology AC/DC** (Eurorack USB audio interface, OpenWare firmware), written in C++ against the OwlProgram SDK. They grew out of [acdc-workbench](https://github.com/m9dfukc/acdc-workbench), where they are included as a submodule at `owl/`. The measurements behind them (D numbers in the comments) are in that repository's lab notebook, `docs/messlog.md`.

## Patches

| File | What it does |
|---|---|
| `Tanh3Patch.hpp` | Triple tanh waveshaper (outputs saturate at ±5 V) with a mix channel on out 4, its level set by knob 4 plus a CV on in 4. 4× oversampling per channel |
| `schliere/` | **Schliere**: a chain of second-order allpass filters (61 stages, 160 in variant L). Frequency-dependent delay, phaser notches, resonator with feedback. Mono in, stereo out. Seven variants, see [schliere/README.md](schliere/README.md) |
| `AcdcOutput.h` | `AcdcLimited<Patch>`: stereo-linked peak limiter with a soft knee on outputs 1/2, wrapped around an existing patch class |
| `AcdcInput.h` | Canceller for the block-rate lines of the AC/DC inputs (6/12/18 kHz, period 8 samples). Learns the pattern in silence |
| `test/SchliereLedTestPatch.hpp` | Measurement patch: switches the LEDs on a fixed schedule, to measure what LED changes do to the outputs |
| `test/SchliereLedPathPatch.hpp` | Measurement patch: switches the LEDs and measures the resulting shift at the raw inputs itself, reports it as a program message |

## Host tests

`test/Patch.h` is a minimal stand-in for the OwlProgram API. The tests compile the patch sources unchanged against it and check levels, saturation, aliasing, the allpass transfer function, feedback, stability, the canceller and the LED schedules.

```bash
test/run.sh
```

Needs a C++17 compiler. Exit code 0 means all cases pass.

## Building and uploading

The patches build with [OwlProgram](https://github.com/RebelTechnology/OwlProgram) and the Arm GNU toolchain. In acdc-workbench, `tools/build_owl_patch.sh` builds a patch into a SysEx file and `tools/owl_write.py` uploads it, for example:

```bash
SRC=owl/schliere tools/build_owl_patch.sh SchliereZ SchlierePatch SchlierePatch.hpp
SRC=owl tools/build_owl_patch.sh Tanh3 Tanh3Patch Tanh3Patch.hpp
```

## License

GPL-3.0-or-later, the license of OwlProgram, which every built patch links against. Full text: [LICENSE](LICENSE).
