#ifndef __PLATEAU_CONTROL_H__
#define __PLATEAU_CONTROL_H__

/* Plateau on four knobs: macros, morph, gate at in 4, flush gesture, normalling of in 2,
 * energy follower, LEDs. Everything here runs once per block or is cheap per sample.
 * GPL-3.0-or-later, part of the Plateau port (see PlateauCore.h).
 *
 * Knob values p are 0 … 1 after the dead zone at both end stops. The mapping:
 *   Mix        dry = cos(π/2·p), wet = W·sin(π/2·p), W = 0.7. Below 2 % dry only, above
 *              98 % wet only (send).
 *   Space      size = 0.01 + (S_max − 0.01)·p² (Plateau's curve, S_max = PLATEAU_MAX_SIZE),
 *              pre-delay = 150 ms·p².
 *   Decay      up to 93 %: RT60 = 0.3 s · 100^(p/0.93), 0.3 … 30 s, independent of size.
 *              From 96 % freeze, back below 93 %.
 *   Character  0–30 %   echo → plate: tank diffusion 0 → full, input diffusion fades in
 *                       between 5 and 15 %
 *              30–55 %  plate: mod depth 0.5 → 3, speed ×1 → ×2, filters open
 *              55–100 % dark/ambient: high cut 14.08 kHz → 1.8 kHz, low cut 13.75 → 120 Hz
 *                       (both exponential), depth 3 → 10, speed ×2 → ×6, shape 0.5 → 0.7
 *
 * Decay as RT60: the tank applies the decay gain g four times per round trip of the figure
 * eight (twice per half). One round trip at size 1 is 21589 Dattorro samples = 0.725 s, so
 * g = 10^(−3 · T_loop / (4 · κ · RT60)), T_loop = 0.725 s · size. κ corrects for what the
 * model leaves out (diffusion, filters, taps); PLATEAU_DECAY_KAPPA. Host test with noise
 * bursts (κ = 1): measured/target 0.8 … 1.2 wherever RT60 ≥ 2 · T_loop. Shorter than about
 * one round trip (up to 2.9 s at size 4) is not possible: the first pass through the taps
 * already lasts that long, even with g = 0.
 */

#include "PlateauCore.h"
#include <math.h>
#include <stddef.h>
#include <stdint.h>

#ifndef PLATEAU_DECAY_KAPPA
#define PLATEAU_DECAY_KAPPA 1.0f
#endif

struct PlateauMacros {
  static constexpr float WET = 0.7f;
  static constexpr float MIN_SIZE = 0.01f;
  static constexpr float RT60_MIN = 0.3f;
  static constexpr float RT60_RANGE = 100.0f;    // 0.3 … 30 s
  static constexpr float DECAY_TOP = 0.93f;      // end of the RT60 range, freeze off below
  static constexpr float FREEZE_ON = 0.96f;
  static constexpr float GAIN_MIN = 0.0f;        // Plateau stops at 0.19; RT60 needs less at large sizes
  static constexpr float GAIN_MAX = 0.9999f;
  static constexpr float LOOP_SECONDS = PlateauReverb::LOOP_SAMPLES / PlateauReverb::DATTORRO_RATE;
  static constexpr float OPEN_HIGH = 14080.0f;   // 440 Hz · 2^5, Plateau's damping fully open
  static constexpr float OPEN_LOW = 13.75f;      // 440 Hz · 2^−5
  static constexpr float DARK_HIGH = 1800.0f;
  static constexpr float DARK_LOW = 120.0f;

  static float clamp(float x, float lo, float hi){ return x < lo ? lo : (x > hi ? hi : x); }

  static float timeScale(float space, float maxSize = PLATEAU_MAX_SIZE){
    const float p = clamp(space, 0.0f, 1.0f);
    return MIN_SIZE + (maxSize - MIN_SIZE) * p * p;
  }
  static float preDelay(float space){
    const float p = clamp(space, 0.0f, 1.0f);
    return PLATEAU_MAX_PREDELAY * p * p;
  }
  static float rt60(float decay){
    const float p = clamp(decay, 0.0f, DECAY_TOP);
    return RT60_MIN * (expf)(4.6051702f * p / DECAY_TOP); // 100^x = e^(x·ln 100)
  }
  static float decayGain(float rt60Seconds, float size, float kappa = PLATEAU_DECAY_KAPPA){
    const float loop = LOOP_SECONDS * size;
    const float g = (expf)(-6.9077553f * loop / (4.0f * kappa * rt60Seconds)); // 10^−3 = e^−6.908
    return clamp(g, GAIN_MIN, GAIN_MAX);
  }
  static void mix(float p, float& dry, float& wet){
    if(p < 0.02f){ dry = 1.0f; wet = 0.0f; return; }
    if(p > 0.98f){ dry = 0.0f; wet = WET; return; }
    const float a = 1.57079633f * p;
    dry = (cosf)(a);
    wet = WET * (sinf)(a);
  }
  /* Character macro into the tank parameters (size, pre-delay and decay stay untouched) */
  static void character(float c, PlateauReverb::Params& p){
    c = clamp(c, 0.0f, 1.0f);
    p.inputDiffusion = clamp((c - 0.05f) / 0.10f, 0.0f, 1.0f);
    p.diffusion = clamp(c / 0.30f, 0.0f, 1.0f);
    p.highCut = OPEN_HIGH;
    p.lowCut = OPEN_LOW;
    p.modDepth = 0.5f;
    p.modSpeed = 1.0f;
    p.modShape = 0.5f;
    if(c > 0.30f && c <= 0.55f){
      const float t = (c - 0.30f) / 0.25f;
      p.modDepth = 0.5f + 2.5f * t;
      p.modSpeed = 1.0f + t;
    }else if(c > 0.55f){
      const float t = (c - 0.55f) / 0.45f;
      p.highCut = OPEN_HIGH * (expf)(t * (logf)(DARK_HIGH / OPEN_HIGH));
      p.lowCut = OPEN_LOW * (expf)(t * (logf)(DARK_LOW / OPEN_LOW));
      p.modDepth = 3.0f + 7.0f * t;
      p.modSpeed = 2.0f + 4.0f * t;
      p.modShape = 0.5f + 0.2f * t;
    }
  }
  /* Morph in macro space: v = −1 … 1, the knob value moves towards snapshot B (v > 0) or
   * C (v < 0) */
  static float morph(float knob, float b, float c, float v){
    const float s = v >= 0.0f ? b : c;
    return knob + fabsf(v) * (s - knob);
  }
};

/* Snapshots of the morph input: Space, Decay, Character */
struct PlateauSnapshot { float space, decay, character; };
static constexpr PlateauSnapshot PLATEAU_ABYSS = { 1.0f, 0.92f, 0.9f };  // in 3 at +5 V
static constexpr PlateauSnapshot PLATEAU_TIGHT = { 0.15f, 0.25f, 0.35f }; // in 3 at −5 V

/* In 4: Schmitt trigger. Gate held = freeze (momentary, from the rising edge on). Falls
 * again within 25 ms: no freeze, clear instead. Below −1 V: clear as well. Works per sample,
 * so 1 ms triggers count at any block size. */
class PlateauGate {
public:
  static constexpr float ON = 0.1f;   // 1 V
  static constexpr float OFF = 0.05f; // 0.5 V
  static constexpr float PULSE_S = 0.025f;

  void setSampleRate(float sr){ pulseMax = (uint32_t)(PULSE_S * sr); }
  void process(const float* x, size_t n){
    for(size_t i = 0; i < n; ++i){
      const float v = x[i];
      if(!high){
        if(v > ON){ high = true; count = 0; }
      }else{
        if(count < 0xffffffffu) count++;
        if(v < OFF){
          high = false;
          if(count <= pulseMax) clearEvent = true;
        }
      }
      if(!low){
        if(v < -ON){ low = true; clearEvent = true; }
      }else if(v > -OFF){
        low = false;
      }
    }
  }
  bool freezeHeld() const { return high; }
  bool takeClear(){ const bool c = clearEvent; clearEvent = false; return c; }

private:
  uint32_t pulseMax = 1200, count = 0;
  bool high = false, low = false, clearEvent = false;
};

/* Freeze zone at the end of the decay knob, and the flush gesture: out of the zone and
 * below 5 % within 250 ms = clear. Works on the knob alone, not on the morphed value. */
class PlateauFlush {
public:
  static constexpr float FLUSH_BELOW = 0.05f;
  static constexpr float FLUSH_S = 0.25f;

  /* once per block with the knob value (after the dead zone) and the block duration */
  bool update(float knob, float seconds){
    bool event = false;
    if(zone){
      if(knob < PlateauMacros::DECAY_TOP){ zone = false; since = 0.0f; armed = true; }
    }else if(knob > PlateauMacros::FREEZE_ON){
      zone = true;
      armed = false;
    }
    if(!zone && armed){
      since += seconds;
      if(knob < FLUSH_BELOW){ event = true; armed = false; }
      else if(since > FLUSH_S) armed = false;
    }
    return event;
  }
  bool inZone() const { return zone; }
  void prime(float knob){ zone = knob > PlateauMacros::FREEZE_ON; armed = false; }

private:
  bool zone = false, armed = false;
  float since = 0.0f;
};

/* Normalling of in 2 by signal detection: no jack detect on the AC/DC. In 2 counts as
 * unpatched when its level (after a 20 Hz highpass, the open inputs sit at −0.2…−0.4 V)
 * stays below −66 dBFS for 300 ms while in 1 carries more than that. Back above −54 dBFS. */
class PlateauAutoNormal {
public:
  static constexpr float SILENT = 5.0e-4f;  // −66 dBFS
  static constexpr float ACTIVE = 2.0e-3f;  // −54 dBFS
  static constexpr float HOLD_S = 0.3f;
  static constexpr float ENV_S = 0.05f;
  static constexpr float FADE_S = 0.01f;

  void setSampleRate(float sr){
    hpL.set(20.0f, sr);
    hpR.set(20.0f, sr);
    env = 1.0f - (expf)(-1.0f / (ENV_S * sr));
    fade = 1.0f / (FADE_S * sr);
    rate = sr;
  }
  /* per sample: returns the right input to use */
  inline float process(float l, float r){
    const float a = hpL.process(l), b = hpR.process(r);
    powL += (a * a - powL) * env;
    powR += (b * b - powR) * env;
    mixPos += mixPos < target ? fade : (mixPos > target ? -fade : 0.0f);
    if(mixPos < 0.0f) mixPos = 0.0f;
    if(mixPos > 1.0f) mixPos = 1.0f;
    return r + mixPos * (l - r);
  }
  /* once per block */
  void update(size_t n){
    const float rmsR = (sqrtf)(powR), rmsL = (sqrtf)(powL);
    if(normal){
      if(rmsR > ACTIVE){ normal = false; quiet = 0.0f; }
    }else if(rmsR < SILENT && rmsL > SILENT){
      quiet += (float)n / rate;
      if(quiet >= HOLD_S) normal = true;
    }else{
      quiet = 0.0f;
    }
    target = normal ? 1.0f : 0.0f;
  }
  bool isNormalled() const { return normal; }

private:
  PlateauHighpass hpL, hpR;
  float env = 0.0f, fade = 0.0f, rate = 48000.0f;
  float powL = 0.0f, powR = 0.0f, quiet = 0.0f, mixPos = 0.0f, target = 0.0f;
  bool normal = false;
};

/* RMS follower, attack 10 ms, release 250 ms (energy output, like the Erbe-Verb) */
class PlateauFollower {
public:
  void setSampleRate(float sr, float attackS = 0.010f, float releaseS = 0.250f){
    att = 1.0f - (expf)(-1.0f / (attackS * sr));
    rel = 1.0f - (expf)(-1.0f / (releaseS * sr));
  }
  inline float process(float x){
    const float p = x * x;
    power += (p - power) * (p > power ? att : rel);
    return power;
  }
  float rms() const { return (sqrtf)(power); }

private:
  float att = 0.0f, rel = 0.0f, power = 0.0f;
};

/* Input LEDs (the output LEDs belong to the firmware, it shows the output levels there).
 * At rest LED n shows the level of input n in green (−48 … 0 dBFS). Moving knob n shows its
 * macro value on LED n for 1 s. Red is for events: LED 3 freeze, LED 4 gate at in 4,
 * all four for 100 ms on clear. Green stays below 0.9: from 4090/4096 the firmware turns red. */
class PlateauLeds {
public:
  static constexpr float GREEN_MAX = 0.9f;
  static constexpr float RED = 1.0f;
  static constexpr float SHOW_S = 1.0f;
  static constexpr float CLEAR_S = 0.1f;
  static constexpr float MOVE = 0.02f;
  static constexpr float RELEASE_S = 0.3f;

  void setSampleRate(float sr){ rate = sr; }
  /* per block: peaks of the four inputs in this block */
  void levels(const float* peaks, size_t n){
    const float r = (expf)(-(float)n / (RELEASE_S * rate));
    for(int i = 0; i < 4; ++i)
      level[i] = peaks[i] > level[i] ? peaks[i] : level[i] * r;
  }
  void knobs(const float* k, bool first){
    for(int i = 0; i < 4; ++i){
      if(first || fabsf(k[i] - ref[i]) > MOVE){
        if(!first) show[i] = SHOW_S;
        ref[i] = k[i];
      }
    }
  }
  void clearFlash(){ flash = CLEAR_S; }
  /* values for PARAMETER_AA … AD */
  void compute(const float* macro, bool frozen, bool gate, float seconds, float* out){
    if(flash > 0.0f) flash -= seconds;
    for(int i = 0; i < 4; ++i){
      float v;
      if(show[i] > 0.0f){
        show[i] -= seconds;
        v = GREEN_MAX * PlateauMacros::clamp(macro[i], 0.0f, 1.0f);
      }else{
        const float db = level[i] > 1e-6f ? 20.0f * (log10f)(level[i]) : -120.0f;
        v = GREEN_MAX * PlateauMacros::clamp((db + 48.0f) / 48.0f, 0.0f, 1.0f);
      }
      out[i] = v;
    }
    if(frozen) out[2] = RED;
    if(gate) out[3] = RED;
    if(flash > 0.0f)
      for(int i = 0; i < 4; ++i) out[i] = RED;
  }

private:
  float rate = 48000.0f;
  float level[4] = {}, ref[4] = {}, show[4] = {};
  float flash = 0.0f;
};

#endif
