#ifndef __SCHLIERE_L_PATCH_HPP__
#define __SCHLIERE_L_PATCH_HPP__

/* Schliere L (lang, "long"): SchliereS with a chain of 160 instead of 61 stages (320 instead
 * of 122 poles). User request (2026-11-21): at least as many stages as the model. According to
 * its manual (v1.2.1) a section has 2 to 98 poles, up to 160 poles with the config file, and in
 * normal operation both sections run in series. The maximum is therefore 320 poles per channel,
 * reached here at the end of the chain (R and tail).
 *
 *   Out 1 (L)  Taps 2, 44, 86, 128, 170, 212, 254 poles (stages 1, 22 … 127, every 21)
 *   Out 2 (R)  Taps 2, 56, 108, 162, 214, 268, 320 poles (stages 1, 28, 54, 81, 107, 134, 160,
 *              alternately 27 and 26 stages apart)
 *   Out 4      Tail = end of the chain, 320 poles
 *
 * As in the other variants, L and R are roughly in the ratio 4 : 5. The distances between the
 * taps are 2.6 times larger: between two detents L gets 21 notches instead of 8, R 26 or 27
 * instead of 10 (one notch per 2 poles of distance). The delay at each tap is proportional to
 * the number of poles: at 1 kHz and Q 16 about 10.2 ms per stage, i.e. 1.3 s at L and 1.6 s
 * at R (SchliereS: 0.5 and 0.6 s).
 *
 * Otherwise as SchliereS: feedback CV at In 4 lightly smoothed (two lowpasses at 12 kHz each),
 * input of the chain at 96 kHz (half-band filters from SchliereOPatch.hpp), tap smoothing,
 * outputs and LEDs from SchliereBase. The class stands on its own so that SchliereO and
 * SchliereS stay byte-identical with their slots (even a template changes their binary). The
 * length is fixed here, not set via SCHLIERE_STAGES, so all variants can run side by side in
 * one host test.
 *
 * CPU load on the device 70 % (SchliereS: 39 %, measurement log D81). The estimate from D80
 * was about 0.43 % per stage, i.e. about 82 %.
 */

#include "SchliereOPatch.hpp"

#ifndef SCHLIERE_S_HZ
#define SCHLIERE_S_HZ 12000.0f // cutoff frequency per pole, as in SchliereSPatch
#endif

class SchliereLPatch : public SchliereBase {
public:
  static constexpr size_t LONG = 160; // length of the chain in stages
private:
  float state[2 * LONG] = {};
  float node[LONG + 1] = {};
  float w1[TAPS] = {}, w2[TAPS] = {};
  SchliereUp2x upIn, upCv, upLoop;
  SchliereDown2x down;
  float s1 = 0.0f, s2 = 0.0f; // state of the two lowpasses for In 4 (as in SchliereS)
  float cvG;
  bool cvStarted = false;

  /* input of the chain at 96 kHz (as in SchliereO) */
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
  SchliereLPatch(){
    const float gs = (tanf)(3.14159265f * SCHLIERE_S_HZ / getSampleRate());
    cvG = gs / (1.0f + gs);
  }

  void processAudio(AudioBuffer& buffer){
    if(channels < 4)
      return; // intended for the AC/DC only
    static const uint8_t tapsL[TAPS] = { 1, 22, 43, 64, 85, 106, 127 };
    static const uint8_t tapsR[TAPS] = { 1, 28, 54, 81, 107, 134, 160 };
    static_assert(LONG >= 160 && LONG < 256, "Taps do not fit the length of the chain");
    const size_t size = buffer.getSize();
    FloatArray io[4] = { buffer.getSamples(0), buffer.getSamples(1),
                         buffer.getSamples(2), buffer.getSamples(3) };

    // smooth the feedback CV lightly (as in SchliereS) before beginBlock reads the inputs
    if(!cvStarted && size > 0){
      s1 = s2 = io[3][0];
      cvStarted = true;
    }
    for(size_t i = 0; i < size; ++i)
      io[3][i] = lowpass(lowpass(io[3][i], s1, cvG), s2, cvG);

    bool first = beginBlock(io, size);

    for(size_t i = 0; i < size; ++i){
      stepCoefficients();
      float x = chainInput2x(io[0][i], io[3][i]);

      node[0] = x;
      float* s = state;
      for(size_t n = 1; n <= LONG; ++n, s += 2){
        x = stage(x, s, a1, a2, g, c);
        node[n] = x;
      }

      // taps: hold zone, S-curve, weights through two lowpasses (as in SchliereO)
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

      outputs(io, i, left, right, 0.5f * (left - right), node[LONG]);
    }

    if(resetIfBroken(io, size, node[LONG])){
      for(size_t n = 0; n < 2 * LONG; ++n) state[n] = 0.0f;
      for(size_t n = 0; n <= LONG; ++n) node[n] = 0.0f;
      upIn = SchliereUp2x(); upCv = SchliereUp2x(); upLoop = SchliereUp2x(); down = SchliereDown2x();
    }
    leds(size);
  }
};

#endif // __SCHLIERE_L_PATCH_HPP__
