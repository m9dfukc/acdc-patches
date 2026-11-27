# acdc-patches

[English](README.md)

OWL-Patches für das **Befaco / Rebel Technology AC/DC** (Eurorack-USB-Audiointerface, OpenWare-Firmware), in C++ gegen das OwlProgram-SDK geschrieben. Sie sind in der [acdc-workbench](https://github.com/m9dfukc/acdc-workbench) entstanden und dort als Submodul unter `owl/` eingebunden. Die Messungen dahinter (D-Nummern in den Kommentaren) stehen im Messlog dieses Repos, `docs/messlog.md`.

## Patches

| Datei | Was er tut |
|---|---|
| `patches/tanh3/` | **Tanh3**: dreifacher tanh-Waveshaper (Ausgänge sättigen bei ±5 V) mit Mix-Kanal auf Out 4, Pegel über Regler 4 plus CV an In 4. 4× Oversampling je Kanal. Siehe [patches/tanh3/README.md](patches/tanh3/README.md) |
| `patches/schliere/` | **Schliere**: Kette aus Allpässen 2. Ordnung (61 Stufen, in Variante L 160). Laufzeit je Frequenz, Phaser-Kerben, Resonator mit Feedback. Mono hinein, stereo heraus. Sieben Varianten, siehe [patches/schliere/README.md](patches/schliere/README.md) |
| `patches/plateau/` | **Plateau**: Port von Valleys Plateau (Plattenhall nach Dattorro 1997, ValleyRackFree 2.4.5) in float, auf vier Makro-Reglern (Mix, Space, Decay als RT60 mit Freeze, Character). Morph-CV an In 3, Freeze/Clear-Gate an In 4, Wet mono und eine Energie-CV an Out 3/4. Läuft am Gerät, 27 % CPU (Firmware mit eingeschalteten Caches, Build-Flags siehe unten), noch nicht gehört. Siehe [patches/plateau/README.md](patches/plateau/README.md) |
| `include/AcdcOutput.h` | `AcdcLimited<Patch>`: Stereo-gekoppelter Spitzen-Limiter mit weichem Knie auf Out 1/2, als Hülle um eine vorhandene Patch-Klasse |
| `include/AcdcInput.h` | Sperre für die Blockraten-Linien der AC/DC-Eingänge (6/12/18 kHz, Periode 8 Samples). Lernt das Muster in der Stille |
| `test/SchliereLedTestPatch.hpp` | Mess-Patch: schaltet die LEDs nach festem Zeitplan, um zu messen, was LED-Wechsel an den Ausgängen anrichten |
| `test/PlateauProfilePatch.hpp` | Mess-Patch: Plateau mit Taktzählern je Abschnitt, Zugriffszeiten der Speicher und den Cache-Bits der MCU, meldet sie als Program Message |
| `test/SchliereLedPathPatch.hpp` | Mess-Patch: schaltet die LEDs und misst die Verschiebung an den rohen Eingängen selbst, meldet sie als Program Message |

## Host-Tests

`test/Patch.h` ersetzt die OwlProgram-API im Kleinen. Die Tests kompilieren die Patch-Quellen unverändert dagegen und prüfen Pegel, Sättigung, Aliasing, die Übertragungsfunktion der Allpässe, Feedback, Stabilität, die Sperre und die LED-Zeitpläne.

```bash
test/run.sh
```

Braucht einen C++17-Compiler. Exit-Code 0 heißt: alle Fälle bestanden. Mit `VALLEY_SRC=<ValleyRackFree 2.4.5>/src` vergleicht es zusätzlich den Plateau-Kern mit dem Original-Code.
Ist `node` installiert, testet es auch den Protokoll-Code der Ladeseite im Browser (`test/web_protocol_test.mjs`). Mit `WEB_REF_BUILDS="<Build-Ordner> …"` (je mit `patch.bin` und einer `.syx` davon) vergleicht es die Upload-Pakete Byte für Byte.

## Fertige Patches

Die [Releases](https://github.com/m9dfukc/acdc-patches/releases) enthalten fertig gebaute SysEx-Dateien von Tanh3, SchliereS, SchliereL und Plateau, genau die Builds, die auf dem Testmodul der acdc-workbench liegen. Rechenlast am AC/DC mit dem [Firmware-Fork](https://github.com/m9dfukc/OpenWare) `v22.4.r-fx9a`, Regler etwa auf 12 Uhr: Tanh3 45 %, SchliereS 35 %, SchliereL 59 % (hängt von den Reglern ab), Plateau 27 %. Plateau ist ins AXI-SRAM gelinkt (`plusram`) und braucht etwa 600 KB Patch-Speicher, ausprobiert nur mit dieser Firmware.

**Ladeseite im Browser:** <https://m9dfukc.github.io/acdc-patches/> lädt eine `.bin` oder `.syx` vom eigenen Rechner per Web MIDI hoch: aus dem RAM starten oder in einem Slot speichern, ohne Toolchain. Die Dateien vorher aus einem Release herunterladen. Sie kann auch einen einzelnen Slot löschen. Bevor ein Slot überschrieben oder gelöscht wird, lädt die Seite den alten Patch herunter und bietet ihn als Datei an. Sie läuft in Chromium-Browsern (Chrome, Edge, Brave, Opera) am Desktop, nicht in Safari. Sie schickt nur Upload, Start, Speichern, Löschen eines Slots (1–39), Slot-Download, lesende Abfragen und Program Changes, nie ein Löschen des ganzen Speichers, Settings, Firmware oder Bootloader. Getestet nur mit dem AC/DC und der Firmware `v22.4.r-fx9a` (Start, Speichern in einen leeren Slot, Download bytegleich). Die Seite liegt als reines HTML und JavaScript in [`docs/`](docs/), GitHub Pages liefert sie ohne Build-Schritt aus `main` aus.

Jeder Patch liegt als `.bin` und als `.syx` bei. Die `.syx` enthält nur die Upload-Pakete, im selben Format wie die SysEx-Dateien der OWL-Patch-Library, ohne Befehl zum Starten oder Speichern. OwlProgram lädt eine `.bin` mit `FirmwareSender` hoch (das rufen `make load` und `make store` auf):

```bash
FirmwareSender -in Tanh3.bin -out "OWL-*" -run        # aus dem RAM starten, nichts gespeichert
FirmwareSender -in Tanh3.bin -out "OWL-*" -store 6    # in Slot 6 speichern
```

`tools/owl_write.py` aus der acdc-workbench ist an dieses eine Testmodul gebunden (es prüft die Geräte-ID). Mit diesen Dateien ist `FirmwareSender` nicht ausprobiert. Der Datei-Upload auf der Geräteseite der Patch-Library (patch.rebeltech.org/device) hilft hier nicht: Er speichert eine Datei als benannte Ressource, nicht als Patch in einem Slot. Vor dem Speichern die Slots sichern.

## Bauen und Hochladen

Gebaut wird mit [OwlProgram](https://github.com/RebelTechnology/OwlProgram) und der Arm-GNU-Toolchain. Dabei die Compiler-Flags von OwlProgram-`develop` verwenden ([PR #132](https://github.com/RebelTechnology/OwlProgram/pull/132)): Ohne `-fno-builtin`, `-ffreestanding` und `-mtune=cortex-m4` werden `fabsf`, `sqrtf` und Co. zu einzelnen FPU-Befehlen statt zu Bibliotheksaufrufen. Am AC/DC spart das 5–21 % CPU bei gleichem Ausgang (acdc-workbench, Messlog D92 und D93). In der acdc-workbench baut `tools/build_owl_patch.sh` einen Patch zur SysEx-Datei, `tools/owl_write.py` lädt ihn hoch, zum Beispiel:

```bash
SRC=owl/patches/schliere tools/build_owl_patch.sh SchliereZ SchlierePatch SchlierePatch.hpp
SRC=owl/patches/tanh3 tools/build_owl_patch.sh Tanh3 Tanh3Patch Tanh3Patch.hpp
LINK=plusram SRC=owl/patches/plateau tools/build_owl_patch.sh Plateau PlateauPatch PlateauPatch.hpp
```

## Lizenz

GPL-3.0-or-later, die Lizenz von OwlProgram, gegen das jeder gebaute Patch gelinkt wird. Volltext: [LICENSE](LICENSE).
