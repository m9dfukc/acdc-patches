#ifndef __SCHLIERE_M_PATCH_HPP__
#define __SCHLIERE_M_PATCH_HPP__

/* SchliereM (2026-11-19): morph variant of Schliere. Tap does not crossfade, it changes the
 * length of the chain continuously. The chain stays a pure allpass at every setting, tap
 * modulation only shifts the delay: slowly a vibrato, fast an FM-like timbre. Notches appear
 * at Out 4 (sum) and through feedback, and they move with the tap instead of only fading in
 * and out. Too tame for the user (2026-11-19), therefore no longer on the module.
 * Measurement: measurement log D80.
 *
 * Assignment as in SchlierePatch (SchlierePatch.hpp), except:
 *   Knob 3     length of the chain, continuous from 2 to 98 poles (L) or 2 to 122 poles (R),
 *              without hold zones. The tap position runs through the same two lowpasses as
 *              the weights in SchlierePatch.
 *   Out 1, 2   L 2 … 98 poles, R 2 … 122 poles, continuous, at the detents as SchlierePatch
 *   Out 4      Sum = (L + R) / 2. R is a quarter longer than L, so the sum has notches whose
 *              number grows with the tap. Because the length changes continuously, they
 *              move as it does (a phaser).
 *
 * Morph stage (SchliereMPatch): for a fractional length p, stages 1 … ⌊p⌋ run fully, stage
 * ⌊p⌋ + 1 runs with a corner frequency between 0 Hz and Frequency. At 0 Hz (g = 0) and with
 * an empty state a stage is exactly transparent, so the stages after it need not be computed.
 * The corner frequency of the partial stage is chosen so that its phase at Frequency changes
 * exactly linearly with the fraction, from −2π (at 0 Hz, equivalent to 0) to −π (textbook
 * formula solved for g'):
 * g' = 2g·sin α / (k·cos α + √(k²cos²α + 4 sin²α)), α = π/2 · fraction. So at the whole stages
 * there is no kink at Frequency. Because the partial stage comes in from below, the phase
 * turns forward while it comes in: a slowly rising tap briefly raises the pitch (most strongly
 * at Frequency and below), a falling one lowers it. This is a delay modulation as in a
 * vibrato, only with a different strength per frequency. When a stage drops out of the chain
 * (g = 0), its residue in the state decays within 5 ms. L and R each need a chain of their own
 * here (up to 49 + 61 stages instead of 61), the CPU load is correspondingly higher.
 */

#include "SchlierePatch.hpp"

/* Morph: tap changes the length of the chain continuously, L and R each with a chain of their own */
class SchliereMPatch : public SchliereBase {
  static constexpr float LEAK_S = 0.005f;   // the residue of a switched-off stage decays this fast
  static constexpr float ZERO = 1e-9f;      // below this a state counts as empty
  /* Below this the partial stage is off. A partial stage with a very small fraction has a
   * corner frequency below 1 Hz and takes seconds until DC gets through: the output
   * creeps. Off means g = 0, the residue in the state decays within 5 ms. */
  static constexpr float FRAC_MIN = 0.002f;
  static constexpr float STEP_L = 8.0f;     // stages per detent: L 1 … 49
  static constexpr float STEP_R = 10.0f;    //                    R 1 … 61
  float stateL[2 * STAGES] = {}, stateR[2 * STAGES] = {};
  size_t topL = 1, topR = 1;   // stages 1 … top have state (top ≥ 1)
  float u1 = 0.0f, u2 = 0.0f;  // state of the two lowpasses for the tap position
  float leak;

  /* one chain with p stages, 1 ≤ p ≤ STAGES, p fractional */
  float chain(float x, float* state, size_t& top, float p){
    const size_t whole = (size_t)p;
    const float frac = p - (float)whole;
    float* s = state;
    size_t n = 1;
    for(; n <= whole; ++n, s += 2)
      x = stage(x, s, a1, a2, g, c);
    size_t used = whole;
    if(frac > FRAC_MIN && whole < STAGES){
      // partial stage: phase at Frequency linear in frac, from −2π (≡ 0) to −π
      const float alpha = 1.5707963f * frac;
      const float sn = sinf(alpha), cs = cosf(alpha);
      const float kc = k * cs;
      const float gp = 2.0f * g * sn / (kc + (sqrtf)(kc * kc + 4.0f * sn * sn));
      const float pa1 = 1.0f / (1.0f + gp * (gp + k));
      x = stage(x, s, pa1, gp * pa1, gp, c);
      ++n; s += 2; used = whole + 1;
    }
    // switched-off stages (g = 0, transparent except for the residue in the state)
    for(; n <= top; ++n, s += 2){
      x -= c * s[0];
      s[0] *= leak;
      s[1] *= leak;
      if(fabsf(s[0]) < ZERO && fabsf(s[1]) < ZERO)
        s[0] = s[1] = 0.0f;
    }
    if(used > top)
      top = used;
    while(top > used && top > 1 && state[2 * (top - 1)] == 0.0f && state[2 * (top - 1) + 1] == 0.0f)
      --top;
    return x;
  }

public:
  SchliereMPatch(){
    static_assert(STAGES >= 61, "SCHLIERE_STAGES must be at least 61");
    leak = (expf)(-1.0f / (LEAK_S * sampleRate));
  }

  void processAudio(AudioBuffer& buffer){
    if(channels < 4)
      return; // intended for the AC/DC only
    const size_t size = buffer.getSize();
    FloatArray io[4] = { buffer.getSamples(0), buffer.getSamples(1),
                         buffer.getSamples(2), buffer.getSamples(3) };
    const bool first = beginBlock(io, size);

    float last = 0.0f;
    for(size_t i = 0; i < size; ++i){
      stepCoefficients();
      const float x = chainInput(io[0][i], io[3][i]);

      // tap position through two lowpasses: the modulation of the length is band-limited
      const float u = tapPosition(io[2][i]);
      if(first && i == 0)
        u1 = u2 = u;
      const float us = clamp(lowpass(lowpass(u, u1, modG), u2, modG), 0.0f, (float)(TAPS - 1));

      const float left = chain(x, stateL, topL, 1.0f + STEP_L * us);
      const float right = chain(x, stateR, topR, 1.0f + STEP_R * us);
      outputs(io, i, left, right, 0.5f * (left - right), 0.5f * (left + right));
      last = left + right;
    }

    if(resetIfBroken(io, size, last)){
      for(size_t n = 0; n < 2 * STAGES; ++n) stateL[n] = stateR[n] = 0.0f;
      topL = topR = 1;
    }
    leds(size);
  }
};

#endif // __SCHLIERE_M_PATCH_HPP__
