#ifndef __TANH3_PATCH_HPP__
#define __TANH3_PATCH_HPP__

/* Tanh3: triple tanh waveshaper for the AC/DC, modelled on an analog Eurorack
 * module with three tanh channels (waveshaper, limiter, overdrive, feedback
 * tamer), plus a mix channel (user request 2026-11-17).
 *
 *   Channel 1–3 Out n = SAT · tanh(Level n · In n / SAT)
 *               Knob n = Level (input level before the tanh), 0.5× at the left stop
 *               up to 16× at the right stop, ≈ 2.8× at 12 o'clock. As on the model,
 *               the channel never goes silent.
 *   Channel 4   Out 4 = SAT · tanh(level · (Out 1 + Out 2 + Out 3) / SAT)
 *               Knob 4 = level of the mix (0…1, linear)
 *               In 4 = CV on the level, added to the knob: +5 V ≙ full knob travel,
 *               a negative voltage takes level away. Acts per sample (VCA up to audio rate).
 *               Below ±0.5 V the CV is dead, so that the open input (offset
 *               −0.2…−0.4 V, D36) does not shift the level.
 *
 * Levels: 1.0 = full scale ≈ 10 V. SAT = 0.5: the outputs saturate at ±5 V (10 Vpp).
 * Small signals pass unchanged at Level 1. Everything is DC coupled, so it is
 * also suitable for CV.
 *
 * Not modelled: the normalling of the inputs (top → middle → bottom). The AC/DC
 * cannot detect whether a cable is plugged in.
 *
 * Aliasing: at high Level, tanh produces harmonics above fs/2 that fold back into the
 * audible range as inharmonic lines. The first version used first-order antiderivative
 * antialiasing (like Nonlinearity.hpp from burstMembrane/acdc-patches).
 * That was not enough: 3 kHz at 6.7× Level gave −37 dB of spurious lines below 15 kHz,
 * audible as "digital" noise (user, messlog D56). Now every channel runs
 * with 4× oversampling: FIR up to 192 kHz (polyphase), tanh, FIR down.
 * The same case is now at −98 dB. Cost: ≈ 0.25 ms latency (11.75 samples, the same for
 * all channels) and more CPU load. With hard-driven, bright signals the
 * band-limited output overshoots SAT by up to ≈ 10 %.
 *
 * LEDs (without USB recording): input LED n = output level of the channel relative to SAT,
 * red = deep in saturation (tanh argument above 2.5).
 *
 * basicmaths.h replaces expf() with table approximations. (expf)(x) in parentheses calls
 * the exact libm function.
 */

#include "Patch.h"
#include <math.h>

#ifndef TANH3_TAPS
#define TANH3_TAPS 48 // length of the FIR at 192 kHz, a multiple of 4 (64 brings hardly any gain, D56)
#endif

class Tanh3Patch : public Patch {
  static constexpr float SAT = 0.5f;            // saturation level, 5 V
  static constexpr float LEVEL_MIN = 0.5f;      // Level at the left stop
  static constexpr float LEVEL_LN_RANGE = 3.4657359f; // ln(32): 0.5 … 16
  static constexpr float SLEW = 0.002f;         // knob smoothing per sample, τ ≈ 10 ms
  static constexpr float KNOB_DEAD = 0.03f;     // the left stop reads ≈ 0.016–0.023 (D51)
  static constexpr float CV_DEAD = 0.05f;       // 0.5 V
  static constexpr float CV_SCALE = 2.0f;       // 5 V ≙ 1.0
  static constexpr size_t OS = 4;               // oversampling
  static constexpr size_t TAPS = TANH3_TAPS;
  static constexpr size_t UP_TAPS = TAPS / OS;  // taps per polyphase branch
  static constexpr float CUTOFF = 22000.0f / 192000.0f; // cutoff frequency relative to the high rate
  static constexpr float KAISER_BETA = 9.0f;
  static constexpr float RED_DRIVE = 2.5f;
  static constexpr float LED_DECAY = 0.995f;    // per block

  /* tanh as a continued fraction (Lambert, 7th order). At |x| = 4.97 the approximation
   * reaches exactly 1, above that ±1 applies: continuous, monotonic, error below 1e-4
   * (below 1e-6 up to |x| = 3). Runs four times per sample and channel, hence not tanhf(). */
  static float fastTanh(float x){
    if(x > 4.97f) return 1.0f;
    if(x < -4.97f) return -1.0f;
    const float x2 = x * x;
    const float num = x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
    const float den = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f));
    return num / den;
  }

  /* Filter coefficients, computed once in the constructor: Kaiser-windowed lowpass.
   * up[p][k]: polyphase branch p, each normalized to sum 1 (DC stays exactly the same).
   * down[j]: the same lowpass, sum 1. */
  float up[OS][UP_TAPS];
  float down[TAPS];

  static float besselI0(float x){
    float sum = 1.0f, term = 1.0f;
    for(int k = 1; k < 25; ++k){
      term *= (x / (2.0f * k)) * (x / (2.0f * k));
      sum += term;
    }
    return sum;
  }
  void designFilters(){
    float h[TAPS];
    const float mid = 0.5f * (TAPS - 1);
    for(size_t n = 0; n < TAPS; ++n){
      const float t = n - mid;
      const float arg = 2.0f * 3.14159265f * CUTOFF * t;
      const float sinc = fabsf(t) < 1e-6f ? 1.0f : (sinf)(arg) / arg;
      const float r = t / mid;
      h[n] = sinc * besselI0(KAISER_BETA * (sqrtf)(1.0f - r * r)) / besselI0(KAISER_BETA);
    }
    float total = 0.0f;
    for(size_t n = 0; n < TAPS; ++n) total += h[n];
    for(size_t n = 0; n < TAPS; ++n) down[n] = h[n] / total;
    for(size_t p = 0; p < OS; ++p){
      float sum = 0.0f;
      for(size_t k = 0; k < UP_TAPS; ++k) sum += h[p + OS * k];
      for(size_t k = 0; k < UP_TAPS; ++k) up[p][k] = h[p + OS * k] / sum;
    }
  }

  /* One channel: input and output history as rings written twice, so that the dot
   * products run over a contiguous range. x in units of SAT. */
  struct Shaper {
    float in[2 * UP_TAPS] = {};
    float hi[2 * TAPS] = {};
    size_t inPos = 0;
    size_t hiPos = 0;
    float process(float x, const float (*up)[UP_TAPS], const float* down){
      inPos = inPos == 0 ? UP_TAPS - 1 : inPos - 1;
      in[inPos] = in[inPos + UP_TAPS] = x; // in[inPos + k] = x from k samples ago
      const float* xi = in + inPos;
      for(size_t p = 0; p < OS; ++p){
        float acc = 0.0f;
        for(size_t k = 0; k < UP_TAPS; ++k)
          acc += up[p][k] * xi[k];
        hiPos = hiPos == 0 ? TAPS - 1 : hiPos - 1;
        hi[hiPos] = hi[hiPos + TAPS] = fastTanh(acc);
      }
      const float* z = hi + hiPos;
      float out = 0.0f;
      for(size_t j = 0; j < TAPS; ++j)
        out += down[j] * z[j];
      return out;
    }
  };

  Shaper shaper[4];
  float level[3] = { 1.0f, 1.0f, 1.0f };
  float mixLevel = 0.0f;
  float led[4] = {};
  size_t channels;

public:
  Tanh3Patch(){
    channels = getNumberOfChannels();
    registerParameter(PARAMETER_A, "Level 1");
    registerParameter(PARAMETER_B, "Level 2");
    registerParameter(PARAMETER_C, "Level 3");
    registerParameter(PARAMETER_D, "Mix");
    designFilters();
  }

  void processAudio(AudioBuffer& buffer){
    if(channels < 4)
      return; // intended only for the AC/DC
    const size_t size = buffer.getSize();
    FloatArray io[4] = { buffer.getSamples(0), buffer.getSamples(1),
                         buffer.getSamples(2), buffer.getSamples(3) };
    float target[3];
    for(size_t ch = 0; ch < 3; ++ch)
      target[ch] = LEVEL_MIN * (expf)(LEVEL_LN_RANGE * getParameterValue(PatchParameterId(PARAMETER_A + ch)));
    float mixTarget = (getParameterValue(PARAMETER_D) - KNOB_DEAD) / (1.0f - 2.0f * KNOB_DEAD);
    mixTarget = mixTarget < 0.0f ? 0.0f : (mixTarget > 1.0f ? 1.0f : mixTarget);

    float peak[4] = {};
    float drive[4] = {};
    for(size_t i = 0; i < size; ++i){
      float sum = 0.0f;
      for(size_t ch = 0; ch < 3; ++ch){
        level[ch] += (target[ch] - level[ch]) * SLEW;
        const float x = io[ch][i] * level[ch] * (1.0f / SAT);
        const float y = SAT * shaper[ch].process(x, up, down);
        io[ch][i] = y;
        sum += y;
        if(fabsf(x) > drive[ch]) drive[ch] = fabsf(x);
        if(fabsf(y) > peak[ch]) peak[ch] = fabsf(y);
      }
      mixLevel += (mixTarget - mixLevel) * SLEW;
      float cv = io[3][i];
      cv = cv > CV_DEAD ? cv - CV_DEAD : (cv < -CV_DEAD ? cv + CV_DEAD : 0.0f);
      float gain = mixLevel + CV_SCALE * cv;
      gain = gain < 0.0f ? 0.0f : (gain > 1.0f ? 1.0f : gain);
      const float x = sum * gain * (1.0f / SAT);
      const float y = SAT * shaper[3].process(x, up, down);
      io[3][i] = y;
      if(fabsf(x) > drive[3]) drive[3] = fabsf(x);
      if(fabsf(y) > peak[3]) peak[3] = fabsf(y);
    }

    for(size_t ch = 0; ch < 4; ++ch){
      float value = peak[ch] * (0.97f / SAT);
      if(value > 0.97f) value = 0.97f;
      led[ch] = value > led[ch] * LED_DECAY ? value : led[ch] * LED_DECAY;
      setParameterValue(PatchParameterId(PARAMETER_AA + ch), drive[ch] > RED_DRIVE ? 1.0f : led[ch]);
    }
  }
};

#endif // __TANH3_PATCH_HPP__
