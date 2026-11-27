#ifndef __SCHLIERE_PATCH_HPP__
#define __SCHLIERE_PATCH_HPP__

/* Schliere: long allpass chain for the AC/DC, modelled on a digital Eurorack module with
 * 98 allpass poles per section (user request 2026-11-18). One section is reproduced:
 * mono in, stereo out, plus two derived signals.
 *
 * Overview of all variants (A, Z, W, O, M): README.md in this folder.
 * This file contains SchlierePatch (built as "SchliereZ") and, via SchliereWPatch.hpp,
 * SchliereWPatch. SchliereOPatch.hpp builds on it.
 *
 *   SchlierePatch   Tap crossfades between seven fixed taps of the chain, like the
 *                   model. Between two taps, notches appear as in a phaser.
 *                   Modulating the tap fades the notches in and out: that is an
 *                   amplitude modulation and sounds metallic at audio rate, like a ring mod.
 *   SchliereWPatch  (via SchliereWPatch.hpp) like SchlierePatch, but the feedback amount
 *                   is smoothed at only 12 kHz instead of 1.5 kHz (wild). With audio at In 4,
 *                   e.g. an output of the patch, almost as many new overtones appear as in
 *                   version 3, there is 9–13 dB less folding, and the feedback amount is not
 *                   hard-clipped (measurement log D80, second addendum). The user found
 *                   version 4 too tame.
 *
 * Principle: identical second-order allpasses in series (1 stage = 2 poles). Each stage
 * passes all frequencies at the same level and delays them by different amounts. Taken
 * together this yields an audible, frequency-dependent delay: transients smear into
 * a chirp, with feedback a resonator emerges.
 *
 *   Knob 1     Frequency  20 Hz … 20 kHz: where the delay acts
 *   Knob 2     Resonance  Q 0.01 … 16: low = bass arrives late, high = a narrow
 *                         band around Frequency rings on
 *   Knob 3     Tap        tap position in the chain, seven detents with hold zones. Between
 *                         two detents it crossfades, which creates notches (one notch
 *                         per 2 poles of distance).
 *   Knob 4     Feedback   0 … 95 %, Out 1 back to the input. With CV up to 97 %,
 *                         soft knee from 90 % (knob and CV combined)
 *
 *   In 1       Audio (or CV: the signal path is DC-coupled)
 *   In 2       Frequency CV, 1 V/oct, added to the knob. Not calibrated, no dead zone:
 *              the offset of the open input (−0.2…−0.4 V, D36) detunes only slightly.
 *   In 3       Tap CV, ±5 V = full travel, acts per sample (up to audio rate)
 *   In 4       Feedback CV, ±5 V = full travel, acts per sample. Below zero the loop
 *              inverts (negative feedback, peaks and troughs swap places).
 *              In 3 and In 4 are dead up to ±0.45 V so that the open input does not
 *              shift anything. Up to ±0.65 V the CV sets in softly, above that it acts
 *              linearly, offset by 0.55 V. The soft edge matters for bipolar modulation:
 *              a hard kink at the zero crossing acts like crossover distortion and brings
 *              harmonics of the modulator, which fold above fs/2 at audio rate.
 *
 *   Out 1 (L)  Taps at 2, 18, 34, 50, 66, 82,  98 poles (stages 1, 9 … 49)
 *   Out 2 (R)  Taps at 2, 22, 42, 62, 82, 102, 122 poles (stages 1, 11 … 61)
 *              At the lowest detent L and R are equal (mono, almost unprocessed).
 *              The higher the tap, the further the two drift apart: each ear
 *              gets all frequencies at the same level, but at different times.
 *   Out 3      Diff = (L − R) / 2: what distinguishes the two sides. Has peaks
 *              where the mono sum L + R has notches.
 *   Out 4      Tail = end of the chain (122 poles), independent of the Tap knob. Together
 *              with Out 1 you can crossfade between early and late yourself in the rack.
 *
 * Tap modulation (version 4, 2026-11-19): with audio rate at the Tap CV, version 3 folded
 * sidebands above fs/2. In the simulation with 5 kHz at the input and 441 Hz at the Tap CV,
 * the folded lines at ±3 V were only 34 dB below the signal. The causes were the hard
 * dead zone, the kinks at the edge of the hold zones and the unsmoothed CV. Version 4 lets
 * the CV set in softly (see In 3), crossfades at the edge of the hold zones with an S-curve
 * and smooths the tap weights with two first-order lowpasses in series (3 kHz each,
 * ≈ 1.9 kHz combined, trapezoidal with a zero at fs/2). It is the weights that are
 * smoothed, not the CV: the kinks only arise in the mapping CV → weight. Because all
 * weights pass through the same linear filter, their sum stays 1. Modulation above
 * ≈ 2 kHz is attenuated as a result; the model smooths as well (forum, thread 296092 #36).
 * The same applies to the feedback amount (In 4): with an output of the patch patched into
 * In 4, version 3 sounded like hard clipping (user, 2026-11-19). It now has a soft knee
 * instead of the hard limit and two lowpasses at 1.5 kHz each (measurement log D80).
 *
 * Feedback: Out 1 (before the output stage) goes through a highpass (10 Hz) and a
 * soft limiter (from 5 V, at most 10 V) back to the input. The highpass sits only
 * in the loop: the forward path stays DC-coupled, but feedback does not amplify the
 * input offset (without the highpass it would be tenfold at 90 %). The loop does not
 * oscillate on its own (gain below 1). The input is attenuated by √(1 − fb²) so that
 * the loudness stays roughly the same with a lot of feedback.
 *
 * Filter form: state-variable filter with trapezoidal integration, allpass = input
 * − 2k · bandpass. The direct form with two multiplications would be cheaper, but in
 * float it is 20 to 60 dB noisier at low frequencies and high Q (host comparison,
 * 61 stages: 100 Hz, Q 16 → −71 vs. −115 dB). All full stages share the
 * coefficients. Frequency and Resonance are computed once per block and ramped linearly
 * across the block, which smooths very fast modulation of the frequency.
 *
 * Input: In 1 first runs through the canceller for the block-rate lines (AcdcInput.h).
 * The ADC data carry interference at 6, 12 and 18 kHz, the CPU load of this patch
 * raises it by 3.8 dB (measurement log D69), and without the canceller it would reach the
 * outputs with a factor of 1 (D68). The canceller learns the interference in silence and
 * subtracts it.
 *
 * Level: 1.0 = full scale ≈ 10 V. The outputs are linear up to 7.5 V, above that a
 * soft knee bends off, no more than 9.8 V comes out.
 *
 * LEDs (without USB recording): LEDs 1–3 show detent 1–7 of knob 3 in binary, in red
 * (LED 1 = 1); between two detents they are off. From version 4 on only the knob counts,
 * not the CV, with hysteresis (on at ±12 % of the spacing, off from ±18 %): with tap
 * modulation the LEDs would otherwise switch at block rate and cause clicks (D73). LED 4
 * lights red while the limiter in the loop is working. The LEDs are only fully off or red,
 * never dimmed: green is dimmed by the firmware via PWM (2320.54 Hz up to fx6a, 48 kHz
 * from fx7a), red is a static pin. The first version kept the LEDs lit at 60 % all the
 * time, and the user heard a whine (measurement log D66, addendum). Every change of an
 * LED shifts the output (D73).
 *
 * Not reproduced: second section and series connection of both, attenuverters,
 * Resonance CV, adjustable taps.
 *
 * basicmaths.h replaces expf() with table approximations. (expf)(x) in parentheses calls
 * the exact libm function.
 */

#include "Patch.h"
#include "../../include/AcdcInput.h"
#include <math.h>
#include <stdint.h>

#ifndef SCHLIERE_STAGES
#define SCHLIERE_STAGES 61 // length of the chain in stages, at least 61 (last tap of R)
#endif

/* Part shared by both patches: knobs, coefficients, input, feedback, outputs, LEDs */
class SchliereBase : public Patch {
protected:
  static constexpr size_t STAGES = SCHLIERE_STAGES;
  static constexpr size_t TAPS = 7;
  static constexpr float KNOB_DEAD = 0.03f;      // the left end stop reads ≈ 0.016–0.023 (D51)
  static constexpr float CV_DEAD_LO = 0.045f;    // dead up to 0.45 V
  static constexpr float CV_DEAD_HI = 0.065f;    // linear from 0.65 V
  static constexpr float CV_SCALE = 2.0f;        // 5 V ≙ full travel
  static constexpr float OCT_PER_FS = 10.0f;     // In 2: 1.0 ≙ 10 V ≙ 10 octaves
  static constexpr float F_CENTER = 632.4555f;   // knob 1 at 12 o'clock: √(20 · 20000) Hz
  static constexpr float F_LN_RANGE = 7.6009025f; // ln(2000): half an octave of headroom per side
  static constexpr float F_MIN = 20.0f;
  static constexpr float F_MAX = 20000.0f;
  static constexpr float LN2 = 0.69314718f;
  static constexpr float FB_MAX = 0.97f;         // limit of the feedback amount (reachable only with CV)
  static constexpr float FB_KNEE = 0.9f;         // linear up to here, soft knee above
  static constexpr float HOLD = 0.15f;           // hold zone per side, fraction of the spacing of two detents
  static constexpr float SMOOTH_S = 0.010f;      // knob smoothing, τ = 10 ms
  static constexpr float MOD_HZ = 3000.0f;       // smoothing of the tap modulation, per pole (2 poles)
  static constexpr float FB_MOD_HZ = 1500.0f;    // smoothing of the feedback amount, per pole (2 poles)
  static constexpr float FB_MOD_HZ_WILD = 12000.0f; // the same for SchliereWPatch
  static constexpr float LOOP_HP_HZ = 10.0f;
  static constexpr float LOOP_KNEE = 0.5f;       // loop: linear up to 5 V
  static constexpr float LOOP_CEILING = 1.0f;
  static constexpr float OUT_KNEE = 0.75f;       // outputs: linear up to 7.5 V
  static constexpr float OUT_CEILING = 0.98f;
  static constexpr float LED_RED = 1.0f;         // from 4090 of 4095 the firmware switches to red
  static constexpr float RED_HOLD_S = 0.3f;
  static constexpr float LED_HYST = 0.03f;       // hysteresis of the detent display, fraction of the spacing

  /* linear up to knee, above that a tanh knee with asymptote ceiling */
  static float soft(float x, float knee, float ceiling){
    const float a = fabsf(x);
    if(a <= knee)
      return x;
    const float range = ceiling - knee;
    const float y = knee + range * (tanhf)((a - knee) / range);
    return x < 0.0f ? -y : y;
  }
  /* Dead zone with a soft edge: 0 up to LO, in between y³ − y⁴/2 (slope and curvature
   * continuous), linear from HI. Offset after that (LO + HI) / 2. */
  static float deadzone(float cv){
    const float a = fabsf(cv);
    if(a <= CV_DEAD_LO)
      return 0.0f;
    float d;
    if(a >= CV_DEAD_HI){
      d = a - 0.5f * (CV_DEAD_LO + CV_DEAD_HI);
    }else{
      const float w = CV_DEAD_HI - CV_DEAD_LO, y = (a - CV_DEAD_LO) / w;
      d = w * y * y * y * (1.0f - 0.5f * y);
    }
    return cv < 0.0f ? -d : d;
  }
  static float clamp(float x, float lo, float hi){
    return x < lo ? lo : (x > hi ? hi : x);
  }
  /* Inf or NaN. Checked via the bits: with -ffast-math the compiler may optimise away
   * comparisons with NaN. */
  static bool broken(float x){
    union { float f; uint32_t u; } v = { x };
    return (v.u & 0x7f800000u) == 0x7f800000u;
  }
  /* Resonance knob → ln(Q): straight lines between the points 0.01 / 0.026 / 0.51 /
   * 3.81 / 16 at 0, 25, 50, 75, 100 % */
  static float lnQ(float p){
    static const float table[5] = { -4.6051702f, -3.6496587f, -0.6733446f, 1.3376292f, 2.7725887f };
    const float u = clamp(p, 0.0f, 1.0f) * 4.0f;
    const int i = u >= 4.0f ? 3 : (int)u;
    return table[i] + (table[i + 1] - table[i]) * (u - i);
  }
  /* First-order lowpass, trapezoidal: zero at fs/2. A simple lowpass
   * (y += (x − y)·a) attenuates only 14 dB per pole there, too little against folding. */
  static inline float lowpass(float x, float& s, float G){
    const float v = (x - s) * G;
    const float y = v + s;
    s = y + v;
    return y;
  }
  /* one stage: state-variable filter (trapezoidal), allpass = x − c · bandpass */
  static inline float stage(float x, float* s, float a1, float a2, float g, float c){
    const float v3 = x - s[1];
    const float v1 = a1 * s[0] + a2 * v3;
    const float v2 = s[1] + g * v1;
    s[0] = 2.0f * v1 - s[0];
    s[1] = 2.0f * v2 - s[1];
    return x - c * v1;
  }
  float knob(PatchParameterId id){
    return clamp((getParameterValue(id) - KNOB_DEAD) / (1.0f - 2.0f * KNOB_DEAD), 0.0f, 1.0f);
  }

  float g = 0.0f, k = 2.0f;     // coefficients, advanced per sample
  float a1 = 0.0f, a2 = 0.0f, c = 0.0f;
  float gStep = 0.0f, kStep = 0.0f;
  float freqKnob = 0.5f, resKnob = 0.5f, tapKnob = 0.0f, fbKnob = 0.0f; // smoothed
  float targetTap = 0.0f, targetFb = 0.0f;
  AcdcBlockRateCanceller inputClean; // canceller for the block-rate lines on In 1
  float loopLp = 0.0f;          // lowpass for the highpass in the loop
  float loopState = 0.0f;       // limited feedback signal of the last sample
  float loopPeak = 0.0f, fbNow = 0.0f;
  float fb1 = 0.0f, fb2 = 0.0f; // state of the two lowpasses for the feedback amount
  float sampleRate;
  float loopHp;
  float modG, fbG;              // coefficients of the lowpasses for tap and feedback amount
  float slewBlock = 0.0f, slewSample;
  size_t slewSize = 0;
  size_t redBlocks = 0;
  int ledPoint = 0;             // displayed detent 1–7, 0 = none
  size_t channels;
  bool primed = false;

  void coefficients(float knobF, float knobQ, float octaves, float& gOut, float& kOut){
    float f = F_CENTER * (expf)(F_LN_RANGE * (knobF - 0.5f) + LN2 * octaves);
    f = clamp(f, F_MIN, F_MAX);
    gOut = (tanf)(3.14159265f * f / sampleRate);
    kOut = (expf)(-lnQ(knobQ));
  }

  SchliereBase(){
    channels = getNumberOfChannels();
    sampleRate = getSampleRate();
    inputClean.setSampleRate(sampleRate);
    loopHp = 1.0f - (expf)(-2.0f * 3.14159265f * LOOP_HP_HZ / sampleRate);
    const float gm = (tanf)(3.14159265f * MOD_HZ / sampleRate);
    modG = gm / (1.0f + gm);
    const float gf = (tanf)(3.14159265f * FB_MOD_HZ / sampleRate);
    fbG = gf / (1.0f + gf);
    slewSample = 1.0f - (expf)(-1.0f / (SMOOTH_S * sampleRate));
    registerParameter(PARAMETER_A, "Frequency");
    registerParameter(PARAMETER_B, "Resonance");
    registerParameter(PARAMETER_C, "Tap");
    registerParameter(PARAMETER_D, "Feedback");
  }

  /* Block start: read the knobs, compute Frequency and Resonance for the end of the block
   * and ramp them linearly across the block. Reads all of In 2 before the outputs are
   * written. Returns true in the first block (take over the smoothed values without
   * settling). */
  bool beginBlock(FloatArray* io, size_t size){
    if(size != slewSize){
      slewSize = size;
      slewBlock = 1.0f - (expf)(-(float)size / (SMOOTH_S * sampleRate));
    }
    const float targetF = knob(PARAMETER_A), targetQ = knob(PARAMETER_B);
    targetTap = knob(PARAMETER_C);
    targetFb = knob(PARAMETER_D);
    const bool first = !primed;
    if(first){
      freqKnob = targetF; resKnob = targetQ; tapKnob = targetTap; fbKnob = targetFb;
      fb1 = fb2 = FB_MAX * soft(targetFb, FB_KNEE, 1.0f);
    }
    freqKnob += (targetF - freqKnob) * slewBlock;
    resKnob += (targetQ - resKnob) * slewBlock;
    float octaves = 0.0f;
    for(size_t i = 0; i < size; ++i)
      octaves += io[1][i];
    octaves *= OCT_PER_FS / size;
    float gTarget, kTarget;
    coefficients(freqKnob, resKnob, octaves, gTarget, kTarget);
    if(first){
      g = gTarget; k = kTarget; primed = true;
    }
    gStep = (gTarget - g) / size;
    kStep = (kTarget - k) / size;
    loopPeak = 0.0f;
    return first;
  }
  void stepCoefficients(){
    g += gStep;
    k += kStep;
    a1 = 1.0f / (1.0f + g * (g + k));
    a2 = g * a1;
    c = 2.0f * k;
  }
  /* Input of the chain: canceller, then feedback (In 4 and knob 4). The feedback amount
   * multiplies the loop signal. With audio at In 4 (e.g. an output of the patch itself)
   * that would be a per-sample ring modulation: a hard limit clips the CV, and sum
   * frequencies above fs/2 fold back. Hence a soft knee instead of a limit, followed by
   * two lowpasses as for the tap, here at 1.5 kHz each (≈ 1 kHz combined). */
  float chainInput(float in1, float cv4){
    fbKnob += (targetFb - fbKnob) * slewSample;
    const float target = FB_MAX * soft(fbKnob + CV_SCALE * deadzone(cv4), FB_KNEE, 1.0f);
    const float fb = lowpass(lowpass(target, fb1, fbG), fb2, fbG);
    fbNow = fb;
    return (sqrtf)(1.0f - fb * fb) * inputClean.process(in1) + fb * loopState;
  }
  /* tap position 0 … TAPS − 1 from knob 3 (smoothed) and In 3 */
  float tapPosition(float cv3){
    tapKnob += (targetTap - tapKnob) * slewSample;
    return clamp(tapKnob + CV_SCALE * deadzone(cv3), 0.0f, 1.0f) * (TAPS - 1);
  }
  /* loop (highpass, soft limiting) and the four outputs */
  void outputs(FloatArray* io, size_t i, float left, float right, float out3, float out4){
    loopLp += (left - loopLp) * loopHp;
    const float loop = left - loopLp;
    loopState = soft(loop, LOOP_KNEE, LOOP_CEILING);
    if(fabsf(loop) > loopPeak) loopPeak = fabsf(loop);
    io[0][i] = soft(left, OUT_KNEE, OUT_CEILING);
    io[1][i] = soft(right, OUT_KNEE, OUT_CEILING);
    io[2][i] = soft(out3, OUT_KNEE, OUT_CEILING);
    io[3][i] = soft(out4, OUT_KNEE, OUT_CEILING);
  }
  /* Garbage in the state (should never happen): loop and outputs to zero. The patch
   * resets the chain itself. */
  bool resetIfBroken(FloatArray* io, size_t size, float probe){
    if(!broken(probe) && !broken(loopState) && !broken(loopLp))
      return false;
    loopLp = loopState = 0.0f;
    for(size_t ch = 0; ch < 4; ++ch)
      for(size_t i = 0; i < size; ++i) io[ch][i] = 0.0f;
    return true;
  }
  /* LEDs: detent 1–7 in binary, LED 4 = limiter in the loop. The detent comes only
   * from knob 3, not from the CV, and has hysteresis: if the LEDs followed the CV,
   * they would switch at block rate under modulation, and every change of an LED gives a
   * click at the output (D73). */
  void leds(size_t size){
    const float u = tapKnob * (TAPS - 1);
    if(ledPoint == 0 || fabsf(u - (float)(ledPoint - 1)) > HOLD + LED_HYST){
      const int nearest = (int)(u + 0.5f);
      ledPoint = fabsf(u - (float)nearest) <= HOLD - LED_HYST ? nearest + 1 : 0;
    }
    for(int bit = 0; bit < 3; ++bit)
      setParameterValue(PatchParameterId(PARAMETER_AA + bit), (ledPoint >> bit) & 1 ? LED_RED : 0.0f);
    if(loopPeak > LOOP_KNEE && fabsf(fbNow) > 1e-4f)
      redBlocks = (size_t)(RED_HOLD_S * sampleRate / size); // red is held, otherwise it would only flicker
    else if(redBlocks > 0)
      redBlocks--;
    setParameterValue(PARAMETER_AD, redBlocks > 0 ? LED_RED : 0.0f);
  }
};

/* Crossfade between seven fixed taps (like the model) */
class SchlierePatch : public SchliereBase {
  float state[2 * STAGES] = {}; // the two integrators of each stage
  float node[STAGES + 1] = {};  // signal after k stages, node[0] = input of the chain
  float w1[TAPS] = {}, w2[TAPS] = {}; // state of the two lowpasses per weight

public:
  SchlierePatch(){
    static_assert(STAGES >= 61, "SCHLIERE_STAGES must be at least 61");
  }

  void processAudio(AudioBuffer& buffer){
    if(channels < 4)
      return; // intended for the AC/DC only
    static const uint8_t tapsL[TAPS] = { 1, 9, 17, 25, 33, 41, 49 };
    static const uint8_t tapsR[TAPS] = { 1, 11, 21, 31, 41, 51, 61 };
    const size_t size = buffer.getSize();
    FloatArray io[4] = { buffer.getSamples(0), buffer.getSamples(1),
                         buffer.getSamples(2), buffer.getSamples(3) };
    bool first = beginBlock(io, size);

    for(size_t i = 0; i < size; ++i){
      stepCoefficients();
      float x = chainInput(io[0][i], io[3][i]);

      // the chain
      node[0] = x;
      float* s = state;
      for(size_t n = 1; n <= STAGES; ++n, s += 2){
        x = stage(x, s, a1, a2, g, c);
        node[n] = x;
      }

      // taps: hold zone, S-curve in between
      const float u = tapPosition(io[2][i]);
      const int index = u >= (float)(TAPS - 1) ? (int)TAPS - 2 : (int)u;
      float t = clamp((u - index - HOLD) / (1.0f - 2.0f * HOLD), 0.0f, 1.0f);
      t = t * t * (3.0f - 2.0f * t);

      // weights per tap, passed through two lowpasses: this makes the crossfade
      // band-limited, also when moving across a detent
      float left = 0.0f, right = 0.0f;
      for(int j = 0; j < (int)TAPS; ++j){
        const float target = j == index ? 1.0f - t : (j == index + 1 ? t : 0.0f);
        if(first)
          w1[j] = w2[j] = target;
        const float w = lowpass(lowpass(target, w1[j], modG), w2[j], modG);
        left += w * node[tapsL[j]];
        right += w * node[tapsR[j]];
      }
      first = false;

      outputs(io, i, left, right, 0.5f * (left - right), node[STAGES]);
    }

    if(resetIfBroken(io, size, node[STAGES])){
      for(size_t n = 0; n < 2 * STAGES; ++n) state[n] = 0.0f;
      for(size_t n = 0; n <= STAGES; ++n) node[n] = 0.0f;
    }
    leds(size);
  }
};

#ifdef SCHLIERE_WILD_PATCH
/* Like SchlierePatch, feedback modulation with more bandwidth (wild). Only via
 * SchliereWPatch.hpp: even a derived class changes the binary of SchlierePatch,
 * and that should stay byte-identical with the build on the module (SchliereZ). */
class SchliereWPatch : public SchlierePatch {
public:
  SchliereWPatch(){
    const float gf = (tanf)(3.14159265f * FB_MOD_HZ_WILD / sampleRate);
    fbG = gf / (1.0f + gf);
  }
};
#endif

#endif // __SCHLIERE_PATCH_HPP__
