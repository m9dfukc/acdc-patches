# acdc-patches

[English](README.md)

OWL-Patches für das **Befaco / Rebel Technology AC/DC** (Eurorack-USB-Audiointerface, OpenWare-Firmware), in C++ gegen das OwlProgram-SDK geschrieben. Sie sind in der [acdc-workbench](https://github.com/m9dfukc/acdc-workbench) entstanden und dort als Submodul unter `owl/` eingebunden. Die Messungen dahinter (D-Nummern in den Kommentaren) stehen im Messlog dieses Repos, `docs/messlog.md`.

## Patches

| Datei | Was er tut |
|---|---|
| `Tanh3Patch.hpp` | Dreifacher tanh-Waveshaper (Ausgänge sättigen bei ±5 V) mit Mix-Kanal auf Out 4, Pegel über Regler 4 plus CV an In 4. 4× Oversampling je Kanal |
| `schliere/` | **Schliere**: Kette aus Allpässen 2. Ordnung (61 Stufen, in Variante L 160). Laufzeit je Frequenz, Phaser-Kerben, Resonator mit Feedback. Mono hinein, stereo heraus. Sieben Varianten, siehe [schliere/README.md](schliere/README.md) |
| `AcdcOutput.h` | `AcdcLimited<Patch>`: Stereo-gekoppelter Spitzen-Limiter mit weichem Knie auf Out 1/2, als Hülle um eine vorhandene Patch-Klasse |
| `AcdcInput.h` | Sperre für die Blockraten-Linien der AC/DC-Eingänge (6/12/18 kHz, Periode 8 Samples). Lernt das Muster in der Stille |
| `test/SchliereLedTestPatch.hpp` | Mess-Patch: schaltet die LEDs nach festem Zeitplan, um zu messen, was LED-Wechsel an den Ausgängen anrichten |
| `test/SchliereLedPathPatch.hpp` | Mess-Patch: schaltet die LEDs und misst die Verschiebung an den rohen Eingängen selbst, meldet sie als Program Message |

## Host-Tests

`test/Patch.h` ersetzt die OwlProgram-API im Kleinen. Die Tests kompilieren die Patch-Quellen unverändert dagegen und prüfen Pegel, Sättigung, Aliasing, die Übertragungsfunktion der Allpässe, Feedback, Stabilität, die Sperre und die LED-Zeitpläne.

```bash
test/run.sh
```

Braucht einen C++17-Compiler. Exit-Code 0 heißt: alle Fälle bestanden.

## Bauen und Hochladen

Gebaut wird mit [OwlProgram](https://github.com/RebelTechnology/OwlProgram) und der Arm-GNU-Toolchain. In der acdc-workbench baut `tools/build_owl_patch.sh` einen Patch zur SysEx-Datei, `tools/owl_write.py` lädt ihn hoch, zum Beispiel:

```bash
SRC=owl/schliere tools/build_owl_patch.sh SchliereZ SchlierePatch SchlierePatch.hpp
SRC=owl tools/build_owl_patch.sh Tanh3 Tanh3Patch Tanh3Patch.hpp
```

## Lizenz

GPL-3.0-or-later, die Lizenz von OwlProgram, gegen das jeder gebaute Patch gelinkt wird. Volltext: [LICENSE](LICENSE).
