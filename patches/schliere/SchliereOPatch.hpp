#ifndef __SCHLIERE_O_PATCH_HPP__
#define __SCHLIERE_O_PATCH_HPP__

/* Schliere O: crossfade as in SchlierePatch (version 4), but the input of the chain is
 * computed at twice the sample rate (96 kHz). User request (2026-11-19): version 3 had a
 * wild feedback, version 4 sounds too tame.
 *
 * The feedback amount multiplies the loop signal. In version 3 this happened unsmoothed at
 * 48 kHz: the sum frequencies above 24 kHz folded back, and on top of that a hard limit
 * clipped the feedback amount. Version 4 smooths the feedback amount (1.5 kHz) and thereby
 * also removes the new overtones, SchliereW (12 kHz) lies in between (measurement log D80).
 *
 * Here the feedback amount is not smoothed (apart from the knob smoothing). Input (after
 * the block-rate canceller), feedback CV (In 4) and loop signal are upsampled to 96 kHz,
 * where dead zone, soft knee and both products (√(1 − fb²) · input and fb · loop) run,
 * then it goes back down to 48 kHz. What would lie above fs/2 at 48 kHz is still below
 * fs/2 at 96 kHz and is removed by the lowpass before downsampling.
 *
 * Filter: polyphase IIR halfband, two branches of first-order allpasses (design after
 * Valenzuela/Constantinides, elliptic, six coefficients, transition 0.045). Passband up to
 * 20 kHz without ripple, stopband from 28 kHz at least 77 dB. The IIR form was chosen because
 * it adds little delay: per direction ≈ 1.4 samples at 48 kHz (1 kHz), 1.7 (10 kHz), 4.5
 * (20 kHz). This makes the loop ≈ 3 samples longer than in SchlierePatch, so the feedback
 * peaks sit slightly differently (at 1 kHz ≈ 0.4 rad more phase).
 *
 * Everything else (chain, taps, tap smoothing, outputs, LEDs) as in SchlierePatch. The
 * crossfade is copied here instead of inherited: even a derived class changes the binary
 * of SchlierePatch, and that should stay byte-identical with the build on the module.
 */

#include "SchlierePatch.hpp"

/* Halfband filter 2×: two branches of first-order allpasses, each at the low rate.
 * H(z) = ½ (A0(z²) + z⁻¹ A1(z²)), A0 with coefficients 0, 2, 4, A1 with 1, 3, 5. */
class SchliereHalfband {
  static constexpr int N = 6;
  float xs[N] = {}, ys[N] = {};
protected:
  float branch(float x, int first){
    static const float coef[N] = { 0.06396838f, 0.22734146f, 0.42965512f,
                                   0.62193724f, 0.78589107f, 0.92904445f };
    for(int i = first; i < N; i += 2){
      const float y = coef[i] * (x - ys[i]) + xs[i];
      xs[i] = x;
      ys[i] = y;
      x = y;
    }
    return x;
  }
};
/* upsample: one sample at 48 kHz → two at 96 kHz (a first) */
class SchliereUp2x : public SchliereHalfband {
public:
  void process(float x, float& a, float& b){
    a = branch(x, 0);
    b = branch(x, 1);
  }
};
/* downsample: two samples at 96 kHz → one at 48 kHz */
class SchliereDown2x : public SchliereHalfband {
  float odd = 0.0f;
public:
  float process(float a, float b){
    const float y = 0.5f * (branch(a, 0) + branch(odd, 1));
    odd = b;
    return y;
  }
};

class SchliereOPatch : public SchliereBase {
  float state[2 * STAGES] = {};
  float node[STAGES + 1] = {};
  float w1[TAPS] = {}, w2[TAPS] = {};
  SchliereUp2x upIn, upCv, upLoop;
  SchliereDown2x down;

  /* Input of the chain at 96 kHz: canceller, then feedback (In 4 and knob 4) */
  float chainInput2x(float in1, float cv4){
    fbKnob += (targetFb - fbKnob) * slewSample;
    float in[2], cv[2], lp[2], x[2];
    upIn.process(inputClean.process(in1), in[0], in[1]);
    upCv.process(cv4, cv[0], cv[1]);
    upLoop.process(loopState, lp[0], lp[1]);
    float fb = 0.0f;
    for(int h = 0; h < 2; ++h){
      fb = FB_MAX * soft(fbKnob + CV_SCALE * deadzone(cv[h]), FB_KNEE, 1.0f);
      x[h] = (sqrtf)(1.0f - fb * fb) * in[h] + fb * lp[h];
    }
    fbNow = fb;
    return down.process(x[0], x[1]);
  }

public:
  SchliereOPatch(){
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
      float x = chainInput2x(io[0][i], io[3][i]);

      // the chain (as in SchlierePatch)
      node[0] = x;
      float* s = state;
      for(size_t n = 1; n <= STAGES; ++n, s += 2){
        x = stage(x, s, a1, a2, g, c);
        node[n] = x;
      }

      // taps (as in SchlierePatch): hold zone, S-curve, weights through two lowpasses
      const float u = tapPosition(io[2][i]);
      const int index = u >= (float)(TAPS - 1) ? (int)TAPS - 2 : (int)u;
      float t = clamp((u - index - HOLD) / (1.0f - 2.0f * HOLD), 0.0f, 1.0f);
      t = t * t * (3.0f - 2.0f * t);
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
      upIn = SchliereUp2x(); upCv = SchliereUp2x(); upLoop = SchliereUp2x(); down = SchliereDown2x();
    }
    leds(size);
  }
};

#endif // __SCHLIERE_O_PATCH_HPP__
