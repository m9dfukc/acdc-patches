#ifndef __SCHLIERE_A_PATCH_HPP__
#define __SCHLIERE_A_PATCH_HPP__

/* SchliereA = Schliere version 3 (2026-11-18, measurement log D70/D71), unchanged except for
 * the class name. Built as "SchliereA", byte-identical with the build on slot 6. Overview of
 * all variants: README.md in this folder.
 *
 * Schliere: long allpass chain for the AC/DC, modelled on a digital Eurorack module with
 * 98 allpass poles per section (user request 2026-11-18). One section is reproduced: mono
 * in, stereo out, plus two derived signals.
 *
 * Principle: 61 identical second-order allpasses in series (1 stage = 2 poles). Every stage
 * passes all frequencies at the same level and delays them by different amounts. Together
 * this gives an audible, frequency-dependent delay: transients smear into a chirp, with
 * feedback a resonator emerges.
 *
 *   Knob 1     Frequency  20 Hz … 20 kHz: where the delay acts
 *   Knob 2     Resonance  Q 0.01 … 16: low = bass arrives late, high = a narrow band
 *                         around Frequency rings on
 *   Knob 3     Tap        tap position in the chain, seven detents with hold zones.
 *                         Between two detents the taps are crossfaded, which creates
 *                         notches as in a phaser (one notch per 2 poles of distance).
 *   Knob 4     Feedback   0 … 97 %, output 1 back to the input
 *
 *   In 1       Audio (or CV: the signal path is DC-coupled)
 *   In 2       Frequency CV, 1 V/oct, added to the knob. Not calibrated, no dead zone:
 *              the offset of the open input (−0.2…−0.4 V, D36) detunes only slightly.
 *   In 3       Tap CV, ±5 V = full travel, acts per sample (up to audio rate)
 *   In 4       Feedback CV, ±5 V = full travel, acts per sample. Below zero the loop
 *              inverts (negative feedback, peaks and troughs swap).
 *              In 3 and In 4 are dead below ±0.5 V, so that an open input does not
 *              shift anything.
 *
 *   Out 1 (L)  Taps  2, 18, 34, 50, 66, 82,  98 poles (stages 1, 9, 17 … 49)
 *   Out 2 (R)  Taps  2, 22, 42, 62, 82, 102, 122 poles (stages 1, 11, 21 … 61)
 *              At the lowest detent L and R are equal (mono, almost unprocessed).
 *              The higher the tap, the further the two drift apart: each ear gets
 *              all frequencies at the same level, but at different times.
 *   Out 3      Diff = (L − R) / 2: what distinguishes the two sides. Has peaks where
 *              the mono sum L + R has notches.
 *   Out 4      Tail = end of the chain (122 poles), independent of the tap knob. Together
 *              with Out 1 you can crossfade between early and late in the rack yourself.
 *
 * Feedback: Out 1 (before the output stage) goes through a highpass (10 Hz) and a soft
 * limiter (from 5 V, at most 10 V) back to the input. The highpass sits only in the loop:
 * the forward path stays DC-coupled, but feedback does not amplify the input offset
 * (without the highpass it would be tenfold at 90 %). The loop does not oscillate on its
 * own (gain below 1). The input is attenuated by √(1 − fb²) so that the loudness stays
 * about the same with a lot of feedback.
 *
 * Filter structure: state-variable filter with trapezoidal integration, allpass = input
 * − 2k · bandpass. The direct form with two multiplications would be cheaper, but in
 * float it is 20 to 60 dB noisier at low frequencies and high Q (host comparison,
 * 61 stages: 100 Hz, Q 16 → −71 vs. −115 dB). All stages share the coefficients.
 * Frequency and Resonance are computed once per block and ramped linearly across the
 * block, which smooths very fast modulation of the frequency.
 *
 * Input: In 1 first passes through the canceller for the block-rate lines (AcdcInput.h).
 * The ADC data carry interference at 6, 12 and 18 kHz, the CPU load of this patch raises
 * it by 3.8 dB (measurement log D69), and without the canceller it would reach the outputs
 * with factor 1 (D68). The canceller learns the interference in silence and subtracts it.
 *
 * Levels: 1.0 = full scale ≈ 10 V. The outputs are linear up to 7.5 V, above that a soft
 * knee bends off, no more than 9.8 V comes out.
 *
 * LEDs (without USB recording): LED 1–3 show detent 1–7 in binary in red (LED 1 = 1),
 * between two detents they are off. LED 4 lights red while the limiter in the loop is
 * working. The LEDs are only fully off or red, never dimmed: green is dimmed by the
 * firmware via PWM (2320.54 Hz, duty cycle = value), red is a static pin. The first
 * version kept the LEDs lit at 60 % all the time, and the user heard a whine (measurement
 * log D66, addendum). The LED PWM is known as a source of interference (H3 in docs/02),
 * otherwise visible in the recordings only with a signal, because the LEDs are otherwise
 * dark.
 *
 * Not reproduced: second section and the series connection of both, attenuverters,
 * resonance CV, adjustable taps.
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

class SchliereAPatch : public Patch {
  static constexpr size_t STAGES = SCHLIERE_STAGES;
  static constexpr size_t TAPS = 7;
  static constexpr float KNOB_DEAD = 0.03f;      // the left stop reads ≈ 0.016–0.023 (D51)
  static constexpr float CV_DEAD = 0.05f;        // 0.5 V
  static constexpr float CV_SCALE = 2.0f;        // 5 V ≙ full travel
  static constexpr float OCT_PER_FS = 10.0f;     // In 2: 1.0 ≙ 10 V ≙ 10 octaves
  static constexpr float F_CENTER = 632.4555f;   // knob 1 at 12 o'clock: √(20 · 20000) Hz
  static constexpr float F_LN_RANGE = 7.6009025f; // ln(2000): half an octave of headroom on each side
  static constexpr float F_MIN = 20.0f;
  static constexpr float F_MAX = 20000.0f;
  static constexpr float LN2 = 0.69314718f;
  static constexpr float FB_MAX = 0.97f;
  static constexpr float HOLD = 0.15f;           // hold zone per side, as a fraction of the distance between two detents
  static constexpr float SMOOTH_S = 0.010f;      // knob smoothing, τ = 10 ms
  static constexpr float LOOP_HP_HZ = 10.0f;
  static constexpr float LOOP_KNEE = 0.5f;       // loop: linear up to 5 V
  static constexpr float LOOP_CEILING = 1.0f;
  static constexpr float OUT_KNEE = 0.75f;       // outputs: linear up to 7.5 V
  static constexpr float OUT_CEILING = 0.98f;
  static constexpr float LED_RED = 1.0f;         // from 4090 of 4095 on the firmware switches to red
  static constexpr float RED_HOLD_S = 0.3f;

  /* linear up to knee, above it a tanh knee with asymptote ceiling */
  static float soft(float x, float knee, float ceiling){
    const float a = fabsf(x);
    if(a <= knee)
      return x;
    const float range = ceiling - knee;
    const float y = knee + range * (tanhf)((a - knee) / range);
    return x < 0.0f ? -y : y;
  }
  static float deadzone(float cv){
    return cv > CV_DEAD ? cv - CV_DEAD : (cv < -CV_DEAD ? cv + CV_DEAD : 0.0f);
  }
  static float clamp(float x, float lo, float hi){
    return x < lo ? lo : (x > hi ? hi : x);
  }
  /* Inf or NaN. Checked via the bits: with -ffast-math the compiler may optimize away
   * comparisons with NaN. */
  static bool broken(float x){
    union { float f; uint32_t u; } v = { x };
    return (v.u & 0x7f800000u) == 0x7f800000u;
  }
  /* Resonance knob → ln(Q): straight lines between the breakpoints 0.01 / 0.026 / 0.51 /
   * 3.81 / 16 at 0, 25, 50, 75, 100 % */
  static float lnQ(float p){
    static const float table[5] = { -4.6051702f, -3.6496587f, -0.6733446f, 1.3376292f, 2.7725887f };
    const float u = clamp(p, 0.0f, 1.0f) * 4.0f;
    const int i = u >= 4.0f ? 3 : (int)u;
    return table[i] + (table[i + 1] - table[i]) * (u - i);
  }
  float knob(PatchParameterId id){
    return clamp((getParameterValue(id) - KNOB_DEAD) / (1.0f - 2.0f * KNOB_DEAD), 0.0f, 1.0f);
  }

  float state[2 * STAGES] = {}; // the two integrators of each stage
  float node[STAGES + 1] = {};  // signal after k stages, node[0] = input of the chain
  float g = 0.0f, k = 2.0f;     // coefficients at the end of the last block
  float freqKnob = 0.5f, resKnob = 0.5f, tapKnob = 0.0f, fbKnob = 0.0f; // smoothed
  AcdcBlockRateCanceller inputClean; // canceller for the block-rate lines on In 1
  float loopLp = 0.0f;          // lowpass for the highpass in the loop
  float loopState = 0.0f;       // limited feedback signal of the last sample
  float sampleRate;
  float loopHp;
  float slewBlock = 0.0f, slewSample;
  size_t slewSize = 0;
  size_t redBlocks = 0;
  size_t channels;
  bool primed = false;

  void coefficients(float knobF, float knobQ, float octaves, float& gOut, float& kOut){
    float f = F_CENTER * (expf)(F_LN_RANGE * (knobF - 0.5f) + LN2 * octaves);
    f = clamp(f, F_MIN, F_MAX);
    gOut = (tanf)(3.14159265f * f / sampleRate);
    kOut = (expf)(-lnQ(knobQ));
  }

public:
  SchliereAPatch(){
    static_assert(STAGES >= 61, "SCHLIERE_STAGES must be at least 61");
    channels = getNumberOfChannels();
    sampleRate = getSampleRate();
    inputClean.setSampleRate(sampleRate);
    loopHp = 1.0f - (expf)(-2.0f * 3.14159265f * LOOP_HP_HZ / sampleRate);
    slewSample = 1.0f - (expf)(-1.0f / (SMOOTH_S * sampleRate));
    registerParameter(PARAMETER_A, "Frequency");
    registerParameter(PARAMETER_B, "Resonance");
    registerParameter(PARAMETER_C, "Tap");
    registerParameter(PARAMETER_D, "Feedback");
  }

  void processAudio(AudioBuffer& buffer){
    if(channels < 4)
      return; // intended for the AC/DC only
    static const uint8_t tapsL[TAPS] = { 1, 9, 17, 25, 33, 41, 49 };
    static const uint8_t tapsR[TAPS] = { 1, 11, 21, 31, 41, 51, 61 };
    const size_t size = buffer.getSize();
    FloatArray io[4] = { buffer.getSamples(0), buffer.getSamples(1),
                         buffer.getSamples(2), buffer.getSamples(3) };
    if(size != slewSize){
      slewSize = size;
      slewBlock = 1.0f - (expf)(-(float)size / (SMOOTH_S * sampleRate));
    }

    // Frequency and Resonance: once per block, ramped linearly across the block
    const float targetF = knob(PARAMETER_A), targetQ = knob(PARAMETER_B);
    const float targetTap = knob(PARAMETER_C), targetFb = knob(PARAMETER_D);
    if(!primed){ // first block: take over the knobs without settling
      freqKnob = targetF; resKnob = targetQ; tapKnob = targetTap; fbKnob = targetFb;
    }
    freqKnob += (targetF - freqKnob) * slewBlock;
    resKnob += (targetQ - resKnob) * slewBlock;
    float octaves = 0.0f;
    for(size_t i = 0; i < size; ++i)
      octaves += io[1][i];
    octaves *= OCT_PER_FS / size;
    float gTarget, kTarget;
    coefficients(freqKnob, resKnob, octaves, gTarget, kTarget);
    if(!primed){
      g = gTarget; k = kTarget; primed = true;
    }
    const float gStep = (gTarget - g) / size, kStep = (kTarget - k) / size;

    float loopPeak = 0.0f, fbNow = 0.0f, tapNow = 0.0f;
    int tapIndex = -1;
    for(size_t i = 0; i < size; ++i){
      g += gStep;
      k += kStep;
      const float a1 = 1.0f / (1.0f + g * (g + k));
      const float a2 = g * a1;
      const float c = 2.0f * k;

      fbKnob += (targetFb - fbKnob) * slewSample;
      const float fb = FB_MAX * clamp(fbKnob + CV_SCALE * deadzone(io[3][i]), -1.0f, 1.0f);
      float x = (sqrtf)(1.0f - fb * fb) * inputClean.process(io[0][i]) + fb * loopState;

      // the chain
      node[0] = x;
      float* s = state;
      for(size_t n = 1; n <= STAGES; ++n, s += 2){
        const float v3 = x - s[1];
        const float v1 = a1 * s[0] + a2 * v3;
        const float v2 = s[1] + g * v1;
        s[0] = 2.0f * v1 - s[0];
        s[1] = 2.0f * v2 - s[1];
        x -= c * v1;
        node[n] = x;
      }

      // taps: hold zone, linear crossfade in between
      tapKnob += (targetTap - tapKnob) * slewSample;
      const float u = clamp(tapKnob + CV_SCALE * deadzone(io[2][i]), 0.0f, 1.0f) * (TAPS - 1);
      const int index = u >= (float)(TAPS - 1) ? (int)TAPS - 2 : (int)u;
      const float t = clamp((u - index - HOLD) / (1.0f - 2.0f * HOLD), 0.0f, 1.0f);
      const float la = node[tapsL[index]], ra = node[tapsR[index]];
      const float left = la + t * (node[tapsL[index + 1]] - la);
      const float right = ra + t * (node[tapsR[index + 1]] - ra);

      // loop: highpass, then soft limiting
      loopLp += (left - loopLp) * loopHp;
      const float loop = left - loopLp;
      loopState = soft(loop, LOOP_KNEE, LOOP_CEILING);
      if(fabsf(loop) > loopPeak) loopPeak = fabsf(loop);

      io[0][i] = soft(left, OUT_KNEE, OUT_CEILING);
      io[1][i] = soft(right, OUT_KNEE, OUT_CEILING);
      io[2][i] = soft(0.5f * (left - right), OUT_KNEE, OUT_CEILING);
      io[3][i] = soft(node[STAGES], OUT_KNEE, OUT_CEILING);
      fbNow = fb;
      tapNow = t;
      tapIndex = index;
    }

    // garbage in the state (should never happen): reset everything instead of getting stuck
    if(broken(node[STAGES]) || broken(loopState) || broken(loopLp)){
      for(size_t n = 0; n < 2 * STAGES; ++n) state[n] = 0.0f;
      for(size_t n = 0; n <= STAGES; ++n) node[n] = 0.0f;
      loopLp = loopState = 0.0f;
      for(size_t ch = 0; ch < 4; ++ch)
        for(size_t i = 0; i < size; ++i) io[ch][i] = 0.0f;
    }

    // LEDs: detent in binary, only in the hold zone
    int number = 0;
    if(tapNow <= 0.0f) number = tapIndex + 1;
    else if(tapNow >= 1.0f) number = tapIndex + 2;
    for(int bit = 0; bit < 3; ++bit)
      setParameterValue(PatchParameterId(PARAMETER_AA + bit), (number >> bit) & 1 ? LED_RED : 0.0f);
    if(loopPeak > LOOP_KNEE && fbNow != 0.0f)
      redBlocks = (size_t)(RED_HOLD_S * sampleRate / size); // red is held, otherwise it only flickers
    else if(redBlocks > 0)
      redBlocks--;
    setParameterValue(PARAMETER_AD, redBlocks > 0 ? LED_RED : 0.0f);
  }
};

#endif // __SCHLIERE_A_PATCH_HPP__
