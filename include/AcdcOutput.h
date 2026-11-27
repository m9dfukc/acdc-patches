#ifndef __ACDC_OUTPUT_H__
#define __ACDC_OUTPUT_H__

/* Output stage for FX patches on the AC/DC: limiter on audio outputs 1/2.
 *
 * Motivation (user, 2026-11-17): some patches overdrive the output, e.g. Grainz
 * (overlapping grains, feedback). The DAC clips hard at full scale.
 *
 * Structure:
 *   1. Peak limiter, both channels linked (the stereo image stays in place):
 *      envelope over max(|L|, |R|), attack 1 ms, release 150 ms. If it lies above
 *      THRESHOLD, the gain is reduced down to THRESHOLD. Below that the signal stays
 *      unchanged.
 *   2. Soft curve for whatever slips through during the attack time: from THRESHOLD
 *      a tanh knee bends off, nothing gets beyond CEILING.
 *
 * Levels: 1.0 = full scale ≈ 10 V peak at the output (AUDIO_OUTPUT_GAIN 125).
 * THRESHOLD 0.5 = 5 V peak = 10 Vpp, the usual Eurorack audio level.
 *
 * Channels 3/4 stay untouched, the FX patches output CV there.
 *
 * Integration without touching third-party sources: AcdcLimited<XyzPatch> inherits from the
 * patch and appends the limiter after processAudio(). tools/build_owl_patch.sh generates the
 * wrapper with LIMIT=1.
 *
 * basicmaths.h replaces expf() with a table approximation. (expf)(x) in parentheses calls
 * the exact libm function.
 */

#include "Patch.h"
#include <math.h>

class AcdcOutputLimiter {
public:
  static constexpr float THRESHOLD = 0.5f;
  static constexpr float CEILING = 0.75f;
  static constexpr float ATTACK_S = 0.001f;
  static constexpr float RELEASE_S = 0.150f;

  void setSampleRate(float sr){
    attack = 1.0f - (expf)(-1.0f / (ATTACK_S * sr));
    release = 1.0f - (expf)(-1.0f / (RELEASE_S * sr));
  }

  void process(FloatArray left, FloatArray right){
    const size_t size = left.getSize();
    for(size_t i = 0; i < size; ++i){
      float l = left[i];
      float r = right[i];
      // NaN or garbage from the patch: silence instead of a stuck envelope (check each
      // channel, a comparison with NaN is always false)
      if(!(fabsf(l) < 100.0f))
        l = 0.0f;
      if(!(fabsf(r) < 100.0f))
        r = 0.0f;
      const float peak = fabsf(l) > fabsf(r) ? fabsf(l) : fabsf(r);
      envelope += (peak - envelope) * (peak > envelope ? attack : release);
      if(envelope > THRESHOLD){
        const float gain = THRESHOLD / envelope;
        l *= gain;
        r *= gain;
      }
      left[i] = knee(l);
      right[i] = knee(r);
    }
  }

  /* linear up to THRESHOLD, above it a tanh knee with asymptote CEILING */
  static float knee(float x){
    const float a = fabsf(x);
    if(a <= THRESHOLD)
      return x;
    const float range = CEILING - THRESHOLD;
    const float y = THRESHOLD + range * (tanhf)((a - THRESHOLD) / range);
    return x < 0.0f ? -y : y;
  }

private:
  float envelope = 0.0f;
  float attack = 0.0206f;   // 1 ms at 48 kHz
  float release = 0.000139f; // 150 ms at 48 kHz
};

template<class BasePatch>
class AcdcLimited : public BasePatch {
public:
  AcdcLimited(){
    limiter.setSampleRate(this->getSampleRate());
  }
  void processAudio(AudioBuffer& buffer) override {
    BasePatch::processAudio(buffer);
    if(buffer.getChannels() >= 2)
      limiter.process(buffer.getSamples(0), buffer.getSamples(1));
  }
private:
  AcdcOutputLimiter limiter;
};

#endif // __ACDC_OUTPUT_H__
