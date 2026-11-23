#ifndef __PLATEAU_PATCH_HPP__
#define __PLATEAU_PATCH_HPP__

/* Plateau for the AC/DC: Valley's Plateau (plate reverb after Dattorro 1997, ValleyRackFree
 * 2.4.5) as an OWL patch on four knobs. Copyright (C) 2020 Valley Audio Soft, Dale Johnson;
 * port 2026. GPL-3.0-or-later. Overview and background: README.md in this folder.
 *
 *   Knob 1   Mix        dry/wet, equal power. Left end dry only, right end wet only (send)
 *   Knob 2   Space      size 0.01 … 4 and pre-delay 0 … 150 ms together
 *   Knob 3   Decay      RT60 0.3 … 30 s, independent of Space. Last few percent: freeze
 *   Knob 4   Character  echo → plate → dark/ambient: diffusion, then modulation, then
 *                       damping (details in PlateauControl.h)
 *
 *   In 1/2   Audio L/R. When in 2 is silent and in 1 is not, R := L (signal detection,
 *            the AC/DC has no jack detect). Both run through the canceller for the
 *            block-rate lines (AcdcInput.h).
 *   In 3     Morph, ±5 V: +5 V towards "Abyss", −5 V towards "Tight", 0 V = the knobs.
 *            Dead below ±0.45 V (open input offset). Mix is left out.
 *   In 4     Gate: held = freeze, a pulse up to 25 ms = clear, below −1 V = clear
 *   Out 1/2  Mix L/R, soft saturation from 7.35 V, at most 9.8 V
 *   Out 3    Wet mono (before the mix)
 *   Out 4    Energy: RMS of the wet signal, 0 … +8 V
 *
 * Flush: turning knob 3 out of the freeze zone to below 5 % within 250 ms clears the reverb.
 *
 * Levels: 1.0 = full scale ≈ 10 V. Plateau feeds volts × 0.1 into the tank and takes wet × 10
 * out of it, so float samples go in and out unscaled.
 *
 * Saturation: Plateau's piecewise tanh (tanhDriveSignal), scaled to unity gain and a ceiling
 * of 0.98. In the original it has a gain of 1.054 and a ceiling of 10 V.
 *
 * At start the patch reports where its buffers landed (program message, read it with
 * owl_read.py): "Plateau KiB I D A S" = ITCM, DTCM, AXI SRAM, SDRAM. If the heap runs out,
 * it reports "Plateau: out of memory" and stays silent.
 *
 * basicmaths.h replaces expf() and friends with table approximations. (expf)(x) in
 * parentheses calls the exact libm function; the patch uses it at block rate only.
 */

#include "Patch.h"
#include "../AcdcInput.h"
#include "PlateauCore.h"
#include "PlateauControl.h"

void debugMessage(const char* msg); // message.h of the SDK, a stand-in in the host test

class PlateauPatch : public Patch {
public:
  static constexpr float KNOB_DEAD = 0.03f;      // the left end stop reads ≈ 0.016–0.023 (D51)
  static constexpr float CV_DEAD = 0.045f;       // in 3: dead up to 0.45 V
  static constexpr float CV_FULL = 0.5f;         // in 3: 5 V ≙ full travel
  static constexpr float KNOB_S = 0.020f;        // knob smoothing
  static constexpr float SPACE_S = 0.080f;       // extra smoothing of Space (Doppler)
  static constexpr float CLEAR_FADE_S = 0.004f;  // fade before and after clear, as Plateau
  static constexpr size_t CLEAR_CHUNK = 8192;    // floats zeroed per block (32 KiB)
  static constexpr float SAT_CEILING = 0.98f;
  static constexpr float ENERGY_GAIN = 2.0f;     // RMS 0.4 (4 V) → 8 V
  static constexpr float ENERGY_MAX = 0.8f;

  PlateauPatch(){
    registerParameter(PARAMETER_A, "Mix");
    registerParameter(PARAMETER_B, "Space");
    registerParameter(PARAMETER_C, "Decay");
    registerParameter(PARAMETER_D, "Character");
    sampleRate = getSampleRate();
    verb.init(sampleRate);
    cleanL.setSampleRate(sampleRate);
    cleanR.setSampleRate(sampleRate);
    gate.setSampleRate(sampleRate);
    normal.setSampleRate(sampleRate);
    energy.setSampleRate(sampleRate);
    leds.setSampleRate(sampleRate);
    envStep = 1.0f / (CLEAR_FADE_S * sampleRate);
  }

  void processAudio(AudioBuffer& buffer){
    const size_t size = buffer.getSize();
    if(buffer.getChannels() < 4 || size == 0)
      return; // intended for the AC/DC only
    FloatArray io[4] = { buffer.getSamples(0), buffer.getSamples(1),
                         buffer.getSamples(2), buffer.getSamples(3) };
    if(!verb.ok()){
      if(!reported){ debugMessage("Plateau: out of memory"); reported = true; }
      for(int ch = 0; ch < 4; ++ch)
        for(size_t i = 0; i < size; ++i) io[ch][i] = 0.0f;
      return;
    }
    if(!reported){
      report();
      reported = true;
    }

    // --- control rate: read everything that the outputs overwrite ---
    if(size != slewSize){
      slewSize = size;
      knobSlew = 1.0f - (expf)(-(float)size / (KNOB_S * sampleRate));
      spaceSlew = 1.0f - (expf)(-(float)size / (SPACE_S * sampleRate));
    }
    const float seconds = (float)size / sampleRate;
    gate.process(&io[3][0], size);
    float cv3 = 0.0f;
    float peaks[4] = {};
    for(int ch = 0; ch < 4; ++ch)
      for(size_t i = 0; i < size; ++i){
        const float a = fabsf(io[ch][i]);
        if(a > peaks[ch]) peaks[ch] = a;
      }
    for(size_t i = 0; i < size; ++i)
      cv3 += io[2][i];
    cv3 /= (float)size;
    const float v = PlateauMacros::clamp(deadzone(cv3), -1.0f, 1.0f);

    float raw[4];
    for(int k = 0; k < 4; ++k)
      raw[k] = knob(PatchParameterId(PARAMETER_A + k));
    const bool first = !primed;
    if(first){
      for(int k = 0; k < 4; ++k) knobs[k] = raw[k];
      flush.prime(raw[2]);
    }
    for(int k = 0; k < 4; ++k)
      knobs[k] += (raw[k] - knobs[k]) * knobSlew;
    morphV += (v - morphV) * (first ? 1.0f : knobSlew);

    const float space = PlateauMacros::morph(knobs[1], PLATEAU_ABYSS.space, PLATEAU_TIGHT.space, morphV);
    const float decayKnob = PlateauMacros::clamp(knobs[2], 0.0f, PlateauMacros::DECAY_TOP);
    const float decay = PlateauMacros::morph(decayKnob, PLATEAU_ABYSS.decay, PLATEAU_TIGHT.decay, morphV);
    const float character = PlateauMacros::morph(knobs[3], PLATEAU_ABYSS.character, PLATEAU_TIGHT.character, morphV);
    spaceSmooth += (space - spaceSmooth) * (first ? 1.0f : spaceSlew);

    PlateauReverb::Params p;
    p.timeScale = PlateauMacros::timeScale(spaceSmooth);
    p.preDelay = PlateauMacros::preDelay(spaceSmooth);
    p.decay = PlateauMacros::decayGain(PlateauMacros::rt60(decay), p.timeScale);
    PlateauMacros::character(character, p);
    verb.setParams(p, size);

    const bool flushed = flush.update(raw[2], seconds);
    const bool frozen = gate.freezeHeld() || flush.inZone();
    verb.setFreeze(frozen);
    if(gate.takeClear() || flushed)
      requestClear();

    float dryT, wetT;
    PlateauMacros::mix(knobs[0], dryT, wetT);
    if(first){ dry = dryT; wet = wetT; }
    const float dryStep = (dryT - dry) / (float)size, wetStep = (wetT - wet) / (float)size;
    normal.update(size);
    primed = true;

    // --- audio rate ---
    for(size_t i = 0; i < size; ++i){
      PLATEAU_PROBE(1);
      const float l = cleanL.process(io[0][i]);
      const float r = normal.process(l, cleanR.process(io[1][i]));
      stepEnvelope();
      float wl = 0.0f, wr = 0.0f;
      if(state != CLEARING){
        verb.process(l * env, r * env, wl, wr);
        wl *= env;
        wr *= env;
      }
      PLATEAU_PROBE(4);
      dry += dryStep;
      wet += wetStep;
      const float wm = 0.5f * (wl + wr);
      io[0][i] = saturate(l * dry + wl * wet);
      io[1][i] = saturate(r * dry + wr * wet);
      io[2][i] = saturate(wm);
      energy.process(wm);
      const float e = ENERGY_GAIN * energy.rms();
      io[3][i] = e < ENERGY_MAX ? e : ENERGY_MAX;
    }
    PLATEAU_PROBE(5);
    dry = dryT;
    wet = wetT;

    if(state == CLEARING && verb.serviceClear(CLEAR_CHUNK))
      state = FADE_IN;
    if(broken(io[0][size - 1]) || broken(io[1][size - 1]) || broken(io[2][size - 1]))
      emergencyClear(io, size);

    // --- LEDs ---
    const float macros[4] = { knobs[0], space, knobs[2], character };
    float led[4];
    leds.levels(peaks, size);
    leds.knobs(raw, first);
    leds.compute(macros, verb.isFrozen(), gate.freezeHeld(), seconds, led);
    for(int k = 0; k < 4; ++k)
      setParameterValue(PatchParameterId(PARAMETER_AA + k), led[k]);
  }

  const PlateauReverb& reverb() const { return verb; } // for the host test

private:
  enum ClearState { IDLE, FADE_OUT, CLEARING, FADE_IN };

  PlateauReverb verb;
  AcdcBlockRateCanceller cleanL, cleanR;
  PlateauGate gate;
  PlateauFlush flush;
  PlateauAutoNormal normal;
  PlateauFollower energy;
  PlateauLeds leds;
  float sampleRate = 48000.0f;
  float knobs[4] = {}, morphV = 0.0f, spaceSmooth = 0.0f;
  float knobSlew = 1.0f, spaceSlew = 1.0f;
  size_t slewSize = 0;
  float dry = 1.0f, wet = 0.0f;
  float env = 1.0f, envStep = 0.0f;
  ClearState state = IDLE;
  bool primed = false, reported = false;

  float knob(PatchParameterId id){
    return PlateauMacros::clamp((getParameterValue(id) - KNOB_DEAD) / (1.0f - 2.0f * KNOB_DEAD), 0.0f, 1.0f);
  }
  /* in 3: 0 up to ±0.45 V, then linear, ±5 V = ±1 */
  static float deadzone(float cv){
    const float a = fabsf(cv);
    if(a <= CV_DEAD) return 0.0f;
    const float d = (a - CV_DEAD) / (CV_FULL - CV_DEAD);
    return cv < 0.0f ? -d : d;
  }
  /* Plateau's tanhDriveSignal: linear up to 0.75, parabola up to 1.25, then 1. Scaled so
   * that the linear part has gain 1 and the ceiling is SAT_CEILING. */
  static inline float saturate(float x){
    const float y = x * (1.0f / SAT_CEILING);
    float s;
    if(y > 1.25f) s = 1.0f;
    else if(y > 0.75f) s = y * (2.5f - y) - 0.5625f;
    else if(y < -1.25f) s = -1.0f;
    else if(y < -0.75f) s = -(-y * (2.5f + y) - 0.5625f);
    else s = y;
    return SAT_CEILING * s;
  }
  static bool broken(float x){ // via the bits: -ffast-math may drop NaN comparisons
    union { float f; uint32_t u; } b = { x };
    return (b.u & 0x7f800000u) == 0x7f800000u;
  }
  void requestClear(){
    if(state == IDLE || state == FADE_IN){
      state = FADE_OUT;
      leds.clearFlash();
    }
  }
  inline void stepEnvelope(){
    if(state == FADE_OUT){
      env -= envStep;
      if(env <= 0.0f){
        env = 0.0f;
        state = CLEARING;
        verb.startClear();
      }
    }else if(state == FADE_IN){
      env += envStep;
      if(env >= 1.0f){
        env = 1.0f;
        state = IDLE;
      }
    }
  }
  void emergencyClear(FloatArray* io, size_t size){
    for(int ch = 0; ch < 3; ++ch)
      for(size_t i = 0; i < size; ++i) io[ch][i] = 0.0f;
    env = 0.0f;
    state = CLEARING;
    verb.startClear();
  }
  void report(){
    static char msg[48];
    const PlateauMemory& m = verb.memory;
    const size_t kib[4] = { m.bytes[PlateauMemory::ITCM], m.bytes[PlateauMemory::DTCM],
                            m.bytes[PlateauMemory::AXI],
                            m.bytes[PlateauMemory::SDRAM] + m.bytes[PlateauMemory::OTHER] };
    char* s = msg;
    for(const char* t = "Plateau KiB"; *t; ) *s++ = *t++;
    for(int k = 0; k < 4; ++k){
      *s++ = ' ';
      unsigned v = (unsigned)((kib[k] + 512) / 1024);
      char digits[8];
      int n = 0;
      do { digits[n++] = (char)('0' + v % 10); v /= 10; } while(v > 0 && n < 7);
      while(n > 0) *s++ = digits[--n];
    }
    *s = '\0';
    debugMessage(msg);
  }
};

#endif
