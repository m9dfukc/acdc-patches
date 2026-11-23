#ifndef __ACDC_INPUT_H__
#define __ACDC_INPUT_H__

/* Input stage for FX patches on the AC/DC: canceller for the block-rate lines.
 *
 * Motivation (measurement log D68, D69): the ADC data of the AC/DC carry interference at the
 * rate of the audio blocks, fs/8 = 6000 Hz with multiples at 12, 18 and 24 kHz. The CPU load
 * of a patch raises it (Schliere: +3.8 dB), and an FX patch passes it on to the outputs.
 * Without an input signal it can be heard as a whine.
 *
 * Principle: the interference is sample-synchronous and repeats exactly every 8 samples.
 * For each of the 8 positions the canceller keeps a slow average of the input. The deviation
 * of this average from the mean over the last 8 samples is exactly the component with
 * period 8. It is subtracted. DC and everything else stay untouched: the canceller acts like
 * four very narrow notches at 6, 12, 18 and 24 kHz.
 *
 * Learning happens only in silence. The measure is the difference to the sample 8 samples
 * earlier: it contains neither DC nor the interference, what remains is signal and noise.
 * If the envelope of this difference lies above THRESHOLD (≈ 20 mV), the estimate is held.
 * Loud music therefore cannot skew it, and the notches do not ring on after a loud signal.
 *
 * Limits:
 *   - A tone at exactly 6000.0 Hz (±0.3 Hz, in the device clock) looks like the
 *     interference. The correction is therefore limited to MAX_CORRECTION (≈ 10 mV), such a
 *     tone loses at most 13 mV (host test).
 *   - If the CPU load of the patch changes while it is loud, the estimate fits again only
 *     at the next quiet passage.
 *   - The mixing products of comb and block rate (D69: −104 dB) do not have period 8 and
 *     remain, as does the comb itself.
 *
 * Usage: process(sample) for every sample of a channel, without gaps. The canceller counts
 * the position within the period itself.
 *
 * basicmaths.h replaces expf() with a table approximation. (expf)(x) in parentheses calls
 * the exact libm function.
 */

#include <math.h>
#include <stddef.h>

class AcdcBlockRateCanceller {
public:
  static constexpr size_t PERIOD = 8;            // audio block size of the firmware
  static constexpr float LEARN_S = 0.5f;         // time constant of the averages
  static constexpr float RELEASE_S = 0.05f;      // learning pauses this long after a loud signal
  static constexpr float THRESHOLD = 0.002f;     // 20 mV
  static constexpr float MAX_CORRECTION = 0.001f; // 10 mV

  void setSampleRate(float sr){
    learn = 1.0f - (expf)(-(float)PERIOD / (LEARN_S * sr));
    release = (expf)(-1.0f / (RELEASE_S * sr));
  }

  float process(float x){
    const size_t i = phase;
    phase = (phase + 1) % PERIOD;
    const float change = fabsf(x - last[i]);
    last[i] = x;
    envelope = change > envelope ? change : envelope * release;
    if(envelope < THRESHOLD){
      const float delta = learn * (x - mean[i]);
      mean[i] += delta;
      sum += delta;
      if(++drift >= 4096){ // now and then remove the rounding error of the running sum
        drift = 0;
        sum = 0.0f;
        for(size_t n = 0; n < PERIOD; ++n) sum += mean[n];
      }
    }
    float correction = mean[i] - sum * (1.0f / PERIOD);
    if(correction > MAX_CORRECTION) correction = MAX_CORRECTION;
    else if(correction < -MAX_CORRECTION) correction = -MAX_CORRECTION;
    return x - correction;
  }

private:
  float mean[PERIOD] = {};
  float last[PERIOD] = {};
  float sum = 0.0f;
  float envelope = 1.0f;     // starts "loud": learn only once a full period is known
  float learn = 0.000333f;   // 0.5 s at 48 kHz
  float release = 0.999583f; // 50 ms at 48 kHz
  size_t phase = 0;
  size_t drift = 0;
};

#endif // __ACDC_INPUT_H__
