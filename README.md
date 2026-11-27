# acdc-patches

[Deutsch](README.de.md)

OWL patches for the **Befaco / Rebel Technology AC/DC** (Eurorack USB audio interface, OpenWare firmware), written in C++ against the OwlProgram SDK. They grew out of [acdc-workbench](https://github.com/m9dfukc/acdc-workbench), where they are included as a submodule at `owl/`. The measurements behind them (D numbers in the comments) are in that repository's lab notebook, `docs/messlog.md`.

## Patches

| File | What it does |
|---|---|
| `tanh3/` | **Tanh3**: triple tanh waveshaper (outputs saturate at ±5 V) with a mix channel on out 4, its level set by knob 4 plus a CV on in 4. 4× oversampling per channel. See [tanh3/README.md](tanh3/README.md) |
| `schliere/` | **Schliere**: a chain of second-order allpass filters (61 stages, 160 in variant L). Frequency-dependent delay, phaser notches, resonator with feedback. Mono in, stereo out. Seven variants, see [schliere/README.md](schliere/README.md) |
| `plateau/` | **Plateau**: port of Valley's Plateau (plate reverb after Dattorro 1997, ValleyRackFree 2.4.5) in float, on four macro knobs (Mix, Space, Decay as RT60 with freeze, Character). Morph CV at in 3, freeze/clear gate at in 4, wet mono and an energy CV on outs 3/4. Runs on the device, 27 % CPU (firmware with caches on, build flags below), not yet heard. See [plateau/README.md](plateau/README.md) |
| `AcdcOutput.h` | `AcdcLimited<Patch>`: stereo-linked peak limiter with a soft knee on outputs 1/2, wrapped around an existing patch class |
| `AcdcInput.h` | Canceller for the block-rate lines of the AC/DC inputs (6/12/18 kHz, period 8 samples). Learns the pattern in silence |
| `test/SchliereLedTestPatch.hpp` | Measurement patch: switches the LEDs on a fixed schedule, to measure what LED changes do to the outputs |
| `test/PlateauProfilePatch.hpp` | Measurement patch: Plateau with cycle counters per section, memory latencies and the cache bits of the MCU, reports them as program messages |
| `test/SchliereLedPathPatch.hpp` | Measurement patch: switches the LEDs and measures the resulting shift at the raw inputs itself, reports it as a program message |

## Host tests

`test/Patch.h` is a minimal stand-in for the OwlProgram API. The tests compile the patch sources unchanged against it and check levels, saturation, aliasing, the allpass transfer function, feedback, stability, the canceller and the LED schedules.

```bash
test/run.sh
```

Needs a C++17 compiler. Exit code 0 means all cases pass. With `VALLEY_SRC=<ValleyRackFree 2.4.5>/src` it also compares the Plateau core with the original code.
If `node` is installed, it also tests the protocol code of the patch loader page (`test/web_protocol_test.mjs`); with `WEB_REF_BUILDS="<build folder> …"` (each with `patch.bin` and a `.syx` of it) it compares the upload packets byte by byte.

## Prebuilt patches

The [releases](https://github.com/m9dfukc/acdc-patches/releases) carry ready-built SysEx files of Tanh3, SchliereS, SchliereL and Plateau, the same builds that are stored on the test module of acdc-workbench. CPU load on the AC/DC with the [firmware fork](https://github.com/m9dfukc/OpenWare) `v22.4.r-fx9a`, knobs near 12 o'clock: Tanh3 45 %, SchliereS 35 %, SchliereL 59 % (depends on the knobs), Plateau 27 %. Plateau is linked to AXI SRAM (`plusram`) and needs about 600 KB of patch memory; it has only been tried on that firmware.

**Patch loader in the browser:** <https://m9dfukc.github.io/acdc-patches/> uploads a `.bin` or `.syx` from your computer over Web MIDI: run from RAM or store in a slot, no toolchain needed. Download the files from a release first. It can also erase a single slot. Before a slot is overwritten or erased, the page downloads the old patch and offers it as a file. It works in Chromium-based browsers (Chrome, Edge, Brave, Opera) on desktop, not in Safari. It sends only upload, run, store, erase of one slot (1–39), slot download, read-only requests and program changes, never an erase of the whole storage, settings, firmware or bootloader commands. Tested only with the AC/DC on firmware `v22.4.r-fx9a` (run, store in an empty slot, download byte-identical). The page lives in [`docs/`](docs/) as plain HTML and JavaScript, GitHub Pages serves it from `main` without a build step.

Each patch comes as `.bin` and as `.syx`. The `.syx` holds only the upload packets, in the same format as the SysEx files of the OWL patch library, with no command to run or store the patch. OwlProgram uploads a `.bin` with `FirmwareSender` (that is what `make load` and `make store` call):

```bash
FirmwareSender -in Tanh3.bin -out "OWL-*" -run        # run from RAM, nothing stored
FirmwareSender -in Tanh3.bin -out "OWL-*" -store 6    # store in slot 6
```

`tools/owl_write.py` from acdc-workbench is tied to that one test module (it checks the device ID). `FirmwareSender` has not been tried with these files. The file upload on the device page of the patch library (patch.rebeltech.org/device) does not help here: it stores a file as a named resource, not as a patch in a slot. Back up your slots before storing.

## Building and uploading

The patches build with [OwlProgram](https://github.com/RebelTechnology/OwlProgram) and the Arm GNU toolchain. Use the compiler flags of OwlProgram `develop` ([PR #132](https://github.com/RebelTechnology/OwlProgram/pull/132)): without `-fno-builtin`, `-ffreestanding` and `-mtune=cortex-m4`, `fabsf`, `sqrtf` and friends become single FPU instructions instead of library calls. On the AC/DC that saves 5–21 % CPU with the same output (acdc-workbench, messlog D92 and D93). In acdc-workbench, `tools/build_owl_patch.sh` builds a patch into a SysEx file and `tools/owl_write.py` uploads it, for example:

```bash
SRC=owl/schliere tools/build_owl_patch.sh SchliereZ SchlierePatch SchlierePatch.hpp
SRC=owl/tanh3 tools/build_owl_patch.sh Tanh3 Tanh3Patch Tanh3Patch.hpp
LINK=plusram SRC=owl/plateau tools/build_owl_patch.sh Plateau PlateauPatch PlateauPatch.hpp
```

## License

GPL-3.0-or-later, the license of OwlProgram, which every built patch links against. Full text: [LICENSE](LICENSE).
