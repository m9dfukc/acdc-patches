#ifndef __SCHLIERE_S_PATCH_HPP__
#define __SCHLIERE_S_PATCH_HPP__

/* Schliere S (sanft, "gentle"): SchliereO with very light smoothing of the feedback
 * modulation. User request (2026-11-19): O is almost the favourite, but with feedback
 * modulation it should have a tiny bit less glitch.
 *
 * The CV at In 4 runs through two first-order lowpasses in series (trapezoidal, zero at
 * fs/2) before SchliereO processes it. Everything else is as in SchliereO: dead zone, soft
 * knee and the products at 96 kHz. Here the smoothing happens before the transfer curve, not
 * after it as in SchlierePatch: this way SchliereO stays untouched and byte-identical with its
 * slot (even a template or a change in SchliereOPatch.hpp changes its binary). SchliereO
 * computes the transfer curve at 96 kHz, where its harmonics hardly fold.
 *
 * As in all variants, knob 4 is smoothed only with 10 ms. Measurement: measurement log D80.
 */

#include "SchliereOPatch.hpp"

#ifndef SCHLIERE_S_HZ
#define SCHLIERE_S_HZ 12000.0f // cutoff frequency per pole
#endif

class SchliereSPatch : public SchliereOPatch {
  float s1 = 0.0f, s2 = 0.0f; // state of the two lowpasses for In 4
  float G;
  bool started = false;
public:
  SchliereSPatch(){
    const float g = (tanf)(3.14159265f * SCHLIERE_S_HZ / getSampleRate());
    G = g / (1.0f + g);
  }
  void processAudio(AudioBuffer& buffer){
    FloatArray cv = buffer.getSamples(3);
    const size_t size = buffer.getSize();
    if(!started && size > 0){
      s1 = s2 = cv[0];
      started = true;
    }
    for(size_t i = 0; i < size; ++i)
      cv[i] = lowpass(lowpass(cv[i], s1, G), s2, G);
    SchliereOPatch::processAudio(buffer);
  }
};

#endif // __SCHLIERE_S_PATCH_HPP__
