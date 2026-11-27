# Tanh3

[English](#english) · [Deutsch](#deutsch)

---

## English

Tanh3 is an OWL patch for the AC/DC: three tanh waveshaper channels plus a mix channel, modelled
on an analog Eurorack module with three tanh channels (waveshaper, limiter, overdrive, feedback
tamer). Everything is DC-coupled, so it shapes CV as well as audio.

### Controls

| | Function |
|---|---|
| Knob 1–3 | Level of channel 1–3 before the tanh: 0.5× at the left stop, ≈ 2.8× at 12 o'clock, 16× at the right stop. As on the model, a channel never goes silent |
| Knob 4 | Level of the mix on out 4, 0…1, linear |
| In 1–3 | Inputs of channel 1–3 |
| In 4 | CV on the mix level, added to knob 4: +5 V = full travel, a negative voltage takes level away. Acts per sample (VCA up to audio rate). Dead below ±0.5 V, so the offset of an open input does not shift the level |
| Out 1–3 | 5 V · tanh(level · in / 5 V): saturates at ±5 V (10 Vpp), small signals pass unchanged at level 1 |
| Out 4 | Sum of out 1–3 times the mix level, through the same tanh |
| LED 1–4 | Output level of each channel relative to saturation, red = deep in saturation. Only without a USB recording stream |

Not modelled: the normalling of the inputs (top → middle → bottom). The AC/DC cannot detect
whether a cable is plugged in.

### Aliasing

At high levels the tanh produces harmonics above fs/2 that fold back as inharmonic lines. The
first version used first-order antiderivative antialiasing. That was not enough: 3 kHz at 6.7×
level left spurious lines at −37 dB below 15 kHz, audible as digital grit (measurement log D56).
Version 2 runs every channel with 4× oversampling: polyphase FIR up to 192 kHz, tanh, FIR down.
The same case is now at −98 dB.

Cost: ≈ 0.25 ms latency (11.75 samples, the same on all channels) and CPU load. With hard-driven
bright signals the band-limited output overshoots 5 V by up to ≈ 10 %.

### On acdc1

Slot 6 on acdc1, version 2, 45 % CPU under firmware fx9a with the current build flags (D94; 48–52 %
before, D56, D62). The LEDs are dimmed with the level. On firmware before
fx7a, dimmed LEDs put the LED PWM line (2321 Hz and multiples) on the outputs (D67).

### Build and test

```bash
SRC=owl/patches/tanh3 tools/build_owl_patch.sh Tanh3 Tanh3Patch Tanh3Patch.hpp
tools/test_owl_dsp.sh
```

Run from acdc-workbench. The build goes to `owl/vendor/builds/Tanh3/` and is byte-identical to slot 6.
In this repository alone, `test/run.sh` runs the host tests (`test/dsp_test.cpp` for Tanh3).
`TANH3_TAPS` (default 48, a multiple of 4) sets the FIR length at 192 kHz.

---

## Deutsch

Tanh3 ist ein OWL-Patch für das AC/DC: drei tanh-Waveshaper-Kanäle und ein Mix-Kanal, nach dem
Vorbild eines analogen Eurorack-Moduls mit drei tanh-Kanälen (Waveshaper, Limiter, Overdrive,
Feedback-Zähmer). Alles ist DC-gekoppelt, der Patch formt CV genauso wie Audio.

### Bedienung

| | Funktion |
|---|---|
| Regler 1–3 | Pegel von Kanal 1–3 vor dem tanh: 0,5-fach am Linksanschlag, ≈ 2,8-fach bei 12 Uhr, 16-fach am Rechtsanschlag. Wie beim Vorbild wird ein Kanal nie stumm |
| Regler 4 | Pegel des Mix an Out 4, 0…1, linear |
| In 1–3 | Eingänge von Kanal 1–3 |
| In 4 | CV auf den Mix-Pegel, addiert zu Regler 4: +5 V = voller Regelweg, negative Spannung nimmt Pegel weg. Wirkt pro Sample (VCA bis Audio-Rate). Unter ±0,5 V tot, damit der Offset eines offenen Eingangs den Pegel nicht verschiebt |
| Out 1–3 | 5 V · tanh(Pegel · In / 5 V): sättigt bei ±5 V (10 Vss), kleine Signale laufen bei Pegel 1 unverändert durch |
| Out 4 | Summe von Out 1–3 mal Mix-Pegel, durch denselben tanh |
| LED 1–4 | Ausgangspegel je Kanal relativ zur Sättigung, Rot = tief in der Sättigung. Nur ohne USB-Aufnahme-Stream |

Nicht nachgebildet: die Normalisierung der Eingänge (oben → Mitte → unten). Das AC/DC erkennt
nicht, ob ein Kabel steckt.

### Aliasing

Bei hohem Pegel erzeugt der tanh Obertöne über fs/2, die als unharmonische Linien zurückfalten.
Die erste Fassung nutzte Antiderivative-Antialiasing erster Ordnung. Das reichte nicht: 3 kHz
bei 6,7-fachem Pegel ließen Störlinien bei −37 dB unter 15 kHz, hörbar als digitales Kratzen
(Messlog D56). Fassung 2 rechnet jeden Kanal mit 4× Oversampling: Polyphasen-FIR auf 192 kHz,
tanh, FIR zurück. Derselbe Fall liegt jetzt bei −98 dB.

Kosten: ≈ 0,25 ms Latenz (11,75 Samples, auf allen Kanälen gleich) und Rechenlast. Bei hart
angefahrenen, hellen Signalen überschwingt der bandbegrenzte Ausgang 5 V um bis zu ≈ 10 %.

### Auf acdc1

Slot 6 auf acdc1, Fassung 2, 45 % CPU unter der Firmware fx9a mit den heutigen Build-Flags (D94, vorher
48–52 %: D56, D62). Die LEDs werden mit dem Pegel gedimmt. Auf Firmware
vor fx7a bringen gedimmte LEDs die LED-PWM-Linie (2321 Hz und Vielfache) an die Ausgänge (D67).

### Bauen und testen

```bash
SRC=owl/patches/tanh3 tools/build_owl_patch.sh Tanh3 Tanh3Patch Tanh3Patch.hpp
tools/test_owl_dsp.sh
```

Aufruf aus der acdc-workbench. Der Build landet in `owl/vendor/builds/Tanh3/` und ist bytegleich
mit Slot 6. Nur in diesem Repo laufen die Host-Tests mit `test/run.sh` (`test/dsp_test.cpp` für
Tanh3). `TANH3_TAPS` (Standard 48, ein Vielfaches von 4) setzt die FIR-Länge bei 192 kHz.
