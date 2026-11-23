#ifndef __SCHLIERE_LED_TEST_PATCH_HPP__
#define __SCHLIERE_LED_TEST_PATCH_HPP__

/* Measurement patch, not a music patch: Schliere with unchanged sound, but the input LEDs
 * step through fixed states by the clock. This makes it possible to measure at the output
 * what the LEDs themselves contribute, without a knob or the tap moving (messlog D72:
 * in Schliere, the LEDs and the tap both depend on knob 3).
 *
 * Sequence, PHASE_S seconds each, starts when the patch is loaded and repeats:
 *   0  All off                        Reference
 *   1  LED 1 red
 *   2  LED 1–3 red
 *   3  LED 1–4 red
 *   4  All off                        Reference
 *   5  LED 1–3 blink red at 40 Hz     Like a flicker at the edge of a hold zone
 *   6  All off                        Reference
 *   7  LED 1–3 green at 60 %          Cross-check: dimmed green demonstrably whines (D67)
 *
 * "Red" in the firmware means red plus green with duty cycle 1023/1024 (messlog D72).
 * Schliere's own display is overwritten. Build:
 *   SRC=owl tools/build_owl_patch.sh SchliereLedTest SchliereLedTestPatch test/SchliereLedTestPatch.hpp
 */

#include "../schliere/SchlierePatch.hpp"

class SchliereLedTestPatch : public SchlierePatch {
public:
  static constexpr float PHASE_S = 5.0f;
  static constexpr size_t PHASES = 8;
  static constexpr size_t FLICKER_BLOCKS = 75; // blocks per half period: 12.5 ms on, 12.5 ms off
  static constexpr float RED = 1.0f;
  static constexpr float GREEN = 0.6f;

  void processAudio(AudioBuffer& buffer){
    SchlierePatch::processAudio(buffer);
    const size_t perPhase = (size_t)(PHASE_S * getSampleRate() / buffer.getSize());
    const size_t phase = block / perPhase;
    float led[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    switch(phase){
    case 1: led[0] = RED; break;
    case 2: led[0] = led[1] = led[2] = RED; break;
    case 3: led[0] = led[1] = led[2] = led[3] = RED; break;
    case 5: if(((block % perPhase) / FLICKER_BLOCKS) & 1) led[0] = led[1] = led[2] = RED; break;
    case 7: led[0] = led[1] = led[2] = GREEN; break;
    default: break;
    }
    for(int i = 0; i < 4; ++i)
      setParameterValue(PatchParameterId(PARAMETER_AA + i), led[i]);
    if(++block >= PHASES * perPhase)
      block = 0;
  }

private:
  size_t block = 0;
};

#endif // __SCHLIERE_LED_TEST_PATCH_HPP__
