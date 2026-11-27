# Plateau

[English](#english) · [Deutsch](#deutsch)

---

## English

A port of **Plateau** by Valley Audio (Dale Johnson), the plate reverb after Jon Dattorro's
1997 design, to the AC/DC as an OWL patch. The reverb itself is a float port of
`src/Plateau/Dattorro.{hpp,cpp}` from [ValleyRackFree](https://github.com/ValleyAudio/ValleyRackFree)
2.4.5. The 13 controls of the original are mapped onto four knobs as macros. Stereo in, stereo
out, plus wet mono and an energy CV.

**Status:** host tests pass (see below). Ran on the device from RAM (2026-11-23, measurement log
D82): memory as planned, **CPU 51 %** under fx7a, **35 %** with the instruction cache on (fx8a, D84). Not yet heard, not stored
in a slot.

### Controls

| | Function | What it does |
|---|---|---|
| Knob 1 | **Mix** | dry/wet with equal power. Below 2 % dry only, above 98 % wet only (send) |
| Knob 2 | **Space** | size 0.01 … 4 (Plateau's curve, size = 0.01 + 3.99·p²) and pre-delay 0 … 150 ms (·p²) together. Smoothed (80 ms) against Doppler steps |
| Knob 3 | **Decay** | RT60 0.3 … 30 s, independent of Space. Above 96 %: freeze, back below 93 % |
| Knob 4 | **Character** | 0–30 % echo → plate (tank diffusion up, input diffusion fades in at 5–15 %). 30–55 % plate (modulation depth 0.5 → 3, speed ×1 → ×2). 55–100 % dark/ambient (high cut 14 kHz → 1.8 kHz, low cut 14 → 120 Hz, depth 3 → 10, speed ×2 → ×6, shape 0.5 → 0.7) |
| In 1 / In 2 | **Audio L / R** | When in 2 stays below −66 dBFS for 300 ms while in 1 carries signal, R := L (back above −54 dBFS). The AC/DC has no jack detect. Both inputs go through the block-rate canceller (`../../include/AcdcInput.h`) |
| In 3 | **Morph** | ±5 V. +5 V → snapshot "Abyss" (Space 1.0, Decay 0.92, Character 0.9), −5 V → "Tight" (0.15, 0.25, 0.35), 0 V → the knobs. Interpolates in macro space, Mix is left out. Dead below ±0.45 V |
| In 4 | **Freeze / clear** | Schmitt trigger at +1 V / +0.5 V. Held gate = freeze (from the rising edge). A pulse up to 25 ms = clear instead. Below −1 V = clear |
| Out 1 / Out 2 | **Mix L / R** | Plateau's saturation, linear up to 7.35 V, at most 9.8 V |
| Out 3 | **Wet mono** | (L + R) / 2 of the reverb, before the mix |
| Out 4 | **Energy** | RMS of the wet signal (attack 10 ms, release 250 ms), 0 … +8 V |

**Flush:** turning knob 3 out of the freeze zone to below 5 % within 250 ms clears the reverb
(no cable needed). Clear fades the reverb out in 4 ms, zeroes the buffers in 32 KiB chunks per
audio block (≈ 3 ms in total) and fades back in.

**Freeze:** like Plateau, the input keeps feeding the frozen loop, the saturation keeps it in
check. The tank filters crossfade out of the loop in 0.1 s.

**Decay as RT60:** the tank applies the decay gain g four times per round trip of its figure
eight, one round trip is 0.725 s · size. The patch sets g = 10^(−3 · T_loop / (4 · RT60)). In the
host test the measured RT60 lies within 0.8 … 1.2 × the target wherever RT60 ≥ 2 · T_loop. A decay
shorter than about one round trip (up to 2.9 s at size 4) is not possible: the first pass through
the output taps lasts that long. Plateau's own decay is a feedback gain, longer at larger sizes.

### LEDs

The patch drives the input LEDs only; the firmware drives the output LEDs itself with the output
levels (mix L/R, wet, energy).

- At rest input LED n shows the level of input n in green (−48 … 0 dBFS).
- Moving knob n shows its value on LED n for 1 s.
- Red: LED 3 = freeze, LED 4 = gate at in 4 high, all four for 100 ms = clear.

Every lit or changing LED shifts the inputs and outputs a little (measurement log D73, D74). If
that bothers you, the LED mode of the firmware (boot menu, knob 2) switches them off.

### Memory

Plateau sizes its buffers for the largest size. At 48 kHz and size 4 the tank needs 551 KiB,
the input allpasses 6 KiB, the pre-delay (150 ms) 28 KiB: 585 KiB in total. The patch heap of
OpenWare is first fit over ITCM (39.2 KB on the fx firmware), DTCM (80 KB), AXI SRAM (512 KB
minus the patch image) and SDRAM (32 MB, probably uncached). The allocation order decides the
placement, so `PlateauCore.h` allocates small hot buffers first. With `LINK=plusram` (patch image
in AXI, 33.8 KB) the plan is:

| Segment | Buffers | KiB |
|---|---|---|
| ITCM | input allpasses, right allpass 1 | 30.3 |
| DTCM | left allpass 2, left allpass 1 | 65.6 |
| AXI | the four tank delays, right allpass 2 | 461.2 |
| SDRAM | pre-delay | 28.1 |

At start the patch reports the actual placement as a program message: `Plateau KiB I D A S`
(read it with `tools/owl_read.py`). If the heap runs out, it reports `Plateau: out of memory` and
stays silent. `PLATEAU_MAX_SIZE` (default 4) caps the size and with it the memory; 3.0 needs 415
KiB for the tank.

### Changes against the original

- float instead of double, 32-bit indices, 64-bit LFO phase. Parameters per block (Size,
  pre-delay, input diffusion and decay ramped per sample), no allocation after start.
- Real 48 kHz everywhere. Plateau 2.4.5 clamps the tank to 44.1 kHz and never gives its filters
  and LFOs a sample rate: at 48 kHz its tank is 8.1 % short and its LFOs 8.8 % fast.
- LFO start phases 0, ¼, ½, ¾ as intended (2.4.5 sets them but never reads them). Freeze
  crossfade 0.1 s as in v1 (2.4.5: 1 s).
- Tank diffusion 0.7 for both allpasses as in 2.x (v1 and Dattorro: 0.5 for allpass 2,
  `PLATEAU_MAX_DIFFUSION2`).
- Input filters fixed open (13.75 Hz … 14.08 kHz), no tuned mode, input sensitivity 0 dB.
- Saturation scaled to unity gain (the original has a gain of 1.054) and a ceiling of 9.8 V.
- Clear in chunks over several blocks instead of in one sample.

### Files

| File | Contents |
|---|---|
| `PlateauCore.h` | the reverb: delay, allpass, filters, LFO, memory, tank. `init(fs, maxSize, maxPreDelay, compat245)`: `compat245 = true` reproduces 2.4.5 (tank at 44.1 kHz, LFO phases 0, 1 s freeze fade), only for the comparison with the original |
| `PlateauControl.h` | macros, morph, gate at in 4, flush gesture, normalling of in 2, energy follower, LEDs |
| `PlateauPatch.hpp` | the patch, class `PlateauPatch` |

### Tests

`test/run.sh` runs `test/plateau_test.cpp`: memory and report, silence, RT60 at six points
against Space, freeze (holds a 300 Hz tail within 1 dB over 3 s; with noise, the linear
interpolation dulls the top end by about 2 dB, as in the original), gate (1 ms trigger clears,
held gate freezes, −3 V clears, release after 200 ms does not clear), flush and a slow turn that
must not clear, mix ends, normalling of in 2 in both directions, morph, size sweeps at block sizes
8/16/64, LEDs.

With `VALLEY_SRC=<ValleyRackFree 2.4.5>/src` it also runs `test/plateau_ref_test.cpp`: the core
in 2.4.5 mode against the original code. Built with `-DPLATEAU_SAMPLE=double` the two agree to
−100 … −133 dB (the logic is the same), in float to −59 … −82 dB (a float delay time of 10000
samples is only accurate to 1e-3 samples, and the error recirculates).

### Build and upload

In acdc-workbench:

```bash
LINK=plusram SRC=owl/patches/plateau tools/build_owl_patch.sh Plateau PlateauPatch PlateauPatch.hpp
```

Upload first with `tools/owl_write.py owl-patches/builds/Plateau/Plateau.syx --run` (RAM only),
then read the placement report and the CPU load with `tools/owl_read.py`.

### Open on the device

1. ~~Placement report~~: as planned, `Plateau KiB 30 66 461 28`, only the pre-delay in SDRAM (D82).
2. CPU load: 51 % instead of the estimated 5–15 % (D82). Cause found with the measurement patch
   `test/PlateauProfilePatch.hpp` (D83): the firmware runs with the instruction and data caches
   of the MCU switched off, every section is about five times slower than necessary. The
   SDRAM pre-delay costs about 1 %. Then the block-rate lines under this load (D69).
3. RT60 and freeze by ear; κ (`PLATEAU_DECAY_KAPPA`) if the decay sounds off.
4. Thresholds of the normalling of in 2 against the noise of the open input.
5. Gate at in 4 with real 1 ms triggers and held gates.

### License

GPL-3.0-or-later. Derived from ValleyRackFree, Copyright (C) 2020 Valley Audio Soft, Dale Johnson,
GPL-3.0-or-later.

---

## Deutsch

Ein Port von **Plateau** von Valley Audio (Dale Johnson), dem Plattenhall nach Jon Dattorros
Entwurf von 1997, als OWL-Patch für das AC/DC. Der Hall selbst ist ein Float-Port von
`src/Plateau/Dattorro.{hpp,cpp}` aus [ValleyRackFree](https://github.com/ValleyAudio/ValleyRackFree)
2.4.5. Die 13 Regler des Originals liegen als Makros auf vier Potis. Stereo hinein, stereo
heraus, dazu Wet mono und eine Energie-CV.

**Stand:** Host-Tests bestanden (siehe unten). Am Gerät aus dem RAM gelaufen (2026-11-23, Messlog
D82): Speicher wie geplant, **CPU 51 %** unter fx7a, **35 %** mit eingeschaltetem Befehls-Cache (fx8a, D84). Noch nicht gehört, in
keinem Slot gespeichert.

### Bedienung

| | Funktion | Was sie tut |
|---|---|---|
| Regler 1 | **Mix** | Dry/Wet mit gleicher Leistung. Unter 2 % nur Dry, über 98 % nur Wet (Send) |
| Regler 2 | **Space** | Size 0,01 … 4 (Plateaus Kurve, Size = 0,01 + 3,99·p²) und Pre-Delay 0 … 150 ms (·p²) gemeinsam. Geglättet (80 ms) gegen Doppler-Stufen |
| Regler 3 | **Decay** | RT60 0,3 … 30 s, unabhängig von Space. Über 96 %: Freeze, unter 93 % wieder aus |
| Regler 4 | **Character** | 0–30 % Echo → Plate (Tank-Diffusion steigt, Eingangs-Diffusion blendet bei 5–15 % ein). 30–55 % Plate (Modulationstiefe 0,5 → 3, Tempo ×1 → ×2). 55–100 % dunkel/Ambient (High Cut 14 kHz → 1,8 kHz, Low Cut 14 → 120 Hz, Tiefe 3 → 10, Tempo ×2 → ×6, Form 0,5 → 0,7) |
| In 1 / In 2 | **Audio L / R** | Bleibt In 2 300 ms lang unter −66 dBFS, während In 1 Signal führt, gilt R := L (zurück über −54 dBFS). Das AC/DC erkennt keine gesteckten Kabel. Beide Eingänge laufen durch die Sperre für die Blockraten-Linien (`../../include/AcdcInput.h`) |
| In 3 | **Morph** | ±5 V. +5 V → Snapshot „Abyss“ (Space 1,0, Decay 0,92, Character 0,9), −5 V → „Tight“ (0,15, 0,25, 0,35), 0 V → die Regler. Interpoliert im Makroraum, Mix bleibt außen vor. Tot unter ±0,45 V |
| In 4 | **Freeze / Clear** | Schmitt-Trigger bei +1 V / +0,5 V. Gehaltenes Gate = Freeze (ab der steigenden Flanke). Ein Puls bis 25 ms = stattdessen Clear. Unter −1 V = Clear |
| Out 1 / Out 2 | **Mix L / R** | Plateaus Sättigung, linear bis 7,35 V, höchstens 9,8 V |
| Out 3 | **Wet mono** | (L + R) / 2 des Halls, vor dem Mix |
| Out 4 | **Energie** | RMS des Wet-Signals (Attack 10 ms, Release 250 ms), 0 … +8 V |

**Flush:** Regler 3 innerhalb von 250 ms aus der Freeze-Zone unter 5 % drehen löscht den Hall
(ohne Kabel). Clear blendet den Hall in 4 ms aus, nullt die Puffer in Stücken von 32 KiB je
Audioblock (zusammen ≈ 3 ms) und blendet wieder ein.

**Freeze:** Wie bei Plateau speist der Eingang weiter in die eingefrorene Schleife, die Sättigung
hält sie im Zaum. Die Tank-Filter blenden in 0,1 s aus der Schleife.

**Decay als RT60:** Der Tank wendet den Decay-Faktor g viermal pro Umlauf seiner Acht an, ein
Umlauf dauert 0,725 s · Size. Der Patch setzt g = 10^(−3 · T_Umlauf / (4 · RT60)). Im Host-Test
liegt der gemessene RT60 bei 0,8 … 1,2 × Soll, überall wo RT60 ≥ 2 · T_Umlauf. Kürzer als etwa
ein Umlauf (bis 2,9 s bei Size 4) geht nicht: So lange dauert schon der erste Durchgang durch die
Ausgangs-Taps. Plateaus eigenes Decay ist ein Rückkopplungsfaktor, bei großer Size länger.

### LEDs

Der Patch steuert nur die Eingangs-LEDs, die Ausgangs-LEDs steuert die Firmware selbst mit den
Ausgangspegeln (Mix L/R, Wet, Energie).

- In Ruhe zeigt Eingangs-LED n den Pegel von Eingang n in Grün (−48 … 0 dBFS).
- Wer Regler n bewegt, sieht seinen Wert 1 s lang auf LED n.
- Rot: LED 3 = Freeze, LED 4 = Gate an In 4 hoch, alle vier für 100 ms = Clear.

Jede leuchtende oder wechselnde LED verschiebt Ein- und Ausgänge ein wenig (Messlog D73, D74).
Stört das, schaltet der LED-Modus der Firmware (Boot-Menü, Regler 2) sie ab.

### Speicher

Plateau legt seine Puffer für die größte Size an. Bei 48 kHz und Size 4 braucht der Tank 551 KiB,
die Eingangs-Allpässe 6 KiB, der Pre-Delay (150 ms) 28 KiB: zusammen 585 KiB. Der Patch-Heap von
OpenWare ist First-Fit über ITCM (39,2 KB auf der fx-Firmware), DTCM (80 KB), AXI-SRAM (512 KB
minus Patch-Image) und SDRAM (32 MB, vermutlich ohne Cache). Die Reihenfolge der Allokationen
entscheidet über die Lage, deshalb legt `PlateauCore.h` kleine, heiße Puffer zuerst an. Mit
`LINK=plusram` (Patch-Image im AXI, 33,8 KB) ist der Plan:

| Segment | Puffer | KiB |
|---|---|---|
| ITCM | Eingangs-Allpässe, rechter Allpass 1 | 30,3 |
| DTCM | linker Allpass 2, linker Allpass 1 | 65,6 |
| AXI | die vier Tank-Delays, rechter Allpass 2 | 461,2 |
| SDRAM | Pre-Delay | 28,1 |

Beim Start meldet der Patch die tatsächliche Lage als Program Message: `Plateau KiB I D A S`
(mit `tools/owl_read.py` lesen). Reicht der Heap nicht, meldet er `Plateau: out of memory` und
bleibt stumm. `PLATEAU_MAX_SIZE` (Standard 4) deckelt die Size und damit den Speicher; bei 3,0
braucht der Tank 415 KiB.

### Änderungen gegenüber dem Original

- float statt double, 32-Bit-Indizes, 64-Bit-LFO-Phase. Parameter je Block (Size, Pre-Delay,
  Eingangs-Diffusion und Decay je Sample gerampt), keine Allokation nach dem Start.
- Echte 48 kHz überall. Plateau 2.4.5 klemmt den Tank auf 44,1 kHz und gibt seinen Filtern und
  LFOs nie eine Abtastrate: Bei 48 kHz ist sein Tank 8,1 % zu kurz, seine LFOs 8,8 % zu schnell.
- LFO-Startphasen 0, ¼, ½, ¾ wie gemeint (2.4.5 setzt sie, liest sie aber nie). Freeze-Überblendung
  0,1 s wie in v1 (2.4.5: 1 s).
- Tank-Diffusion 0,7 für beide Allpässe wie in 2.x (v1 und Dattorro: 0,5 für Allpass 2,
  `PLATEAU_MAX_DIFFUSION2`).
- Eingangsfilter fest offen (13,75 Hz … 14,08 kHz), kein Tuned Mode, Eingangsempfindlichkeit 0 dB.
- Sättigung auf Verstärkung 1 skaliert (das Original verstärkt um 1,054) und auf 9,8 V gedeckelt.
- Clear in Stücken über mehrere Blöcke statt in einem Sample.

### Dateien

| Datei | Inhalt |
|---|---|
| `PlateauCore.h` | der Hall: Delay, Allpass, Filter, LFO, Speicher, Tank. `init(fs, maxSize, maxPreDelay, compat245)`: `compat245 = true` bildet 2.4.5 nach (Tank bei 44,1 kHz, LFO-Phasen 0, 1 s Freeze-Überblendung), nur für den Vergleich mit dem Original |
| `PlateauControl.h` | Makros, Morph, Gate an In 4, Flush-Geste, Normalisierung von In 2, Energie-Follower, LEDs |
| `PlateauPatch.hpp` | der Patch, Klasse `PlateauPatch` |

### Tests

`test/run.sh` führt `test/plateau_test.cpp` aus: Speicher und Meldung, Stille, RT60 an sechs
Punkten gegen Space, Freeze (hält einen 300-Hz-Nachhall über 3 s auf 1 dB; mit Rauschen dämpft die
lineare Interpolation die Höhen um etwa 2 dB, wie im Original), Gate (1-ms-Trigger löscht,
gehaltenes Gate friert ein, −3 V löscht, Loslassen nach 200 ms löscht nicht), Flush und eine
langsame Drehung, die nicht löschen darf, Mix-Enden, Normalisierung von In 2 in beide Richtungen,
Morph, Size-Fahrten bei Blockgrößen 8/16/64, LEDs.

Mit `VALLEY_SRC=<ValleyRackFree 2.4.5>/src` läuft zusätzlich `test/plateau_ref_test.cpp`: der
Kern im 2.4.5-Modus gegen den Original-Code. Mit `-DPLATEAU_SAMPLE=double` gebaut stimmen beide
auf −100 … −133 dB überein (die Logik ist dieselbe), in float auf −59 … −82 dB (eine float-Delayzeit
von 10000 Samples ist nur auf 1e-3 Samples genau, und der Fehler läuft im Kreis).

### Bauen und Hochladen

In der acdc-workbench:

```bash
LINK=plusram SRC=owl/patches/plateau tools/build_owl_patch.sh Plateau PlateauPatch PlateauPatch.hpp
```

Hochladen zuerst mit `tools/owl_write.py owl-patches/builds/Plateau/Plateau.syx --run` (nur RAM),
dann die Speichermeldung und die Rechenlast mit `tools/owl_read.py` lesen.

### Offen am Gerät

1. ~~Speichermeldung~~: wie geplant, `Plateau KiB 30 66 461 28`, nur der Pre-Delay im SDRAM (D82).
2. Rechenlast: 51 % statt der geschätzten 5–15 % (D82). Ursache mit dem Mess-Patch
   `test/PlateauProfilePatch.hpp` gefunden (D83): Die Firmware läuft mit ausgeschaltetem Befehls-
   und Daten-Cache der MCU, jeder Abschnitt ist etwa fünfmal langsamer als nötig. Der Pre-Delay im
   SDRAM kostet etwa 1 %. Danach die Blockraten-Linien bei dieser Last (D69).
3. RT60 und Freeze nach Gehör; κ (`PLATEAU_DECAY_KAPPA`), falls das Decay nicht passt.
4. Schwellen der Normalisierung von In 2 gegen das Rauschen des offenen Eingangs.
5. Gate an In 4 mit echten 1-ms-Triggern und gehaltenen Gates.

### Lizenz

GPL-3.0-or-later. Abgeleitet von ValleyRackFree, Copyright (C) 2020 Valley Audio Soft, Dale Johnson,
GPL-3.0-or-later.
