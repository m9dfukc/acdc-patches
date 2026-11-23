#ifndef __SCHLIERE_LED_PATH_PATCH_HPP__
#define __SCHLIERE_LED_PATH_PATCH_HPP__

/* Measurement patch, not a music patch: where does the shift that the LED current leaves
 * at the output (messlog D73) arise? At the input (ADC), from where a patch passes it on,
 * or only at the output (DAC, output stage)?
 *
 * Sequence, PHASE_S seconds each, starts when the patch is loaded and repeats:
 *   0  Schliere as on slot 6, LEDs off          Reference
 *   1  Schliere, LED 1–3 blink at 40 Hz         As in D73
 *   2  Outputs digitally zero, LEDs off         Reference
 *   3  Outputs zero, LED 1–3 blink              If the blinking remains, it arises at the output
 *   4  Outputs zero, LEDs off
 *   5  Outputs zero, LED 1–4 red                Step when switching on and off
 *   6  Outputs zero, LEDs off
 *   7  Outputs zero, calibration tone at Out 1  CAL_HZ with amplitude CAL_LEVEL, LEDs off
 *   8  All outputs = GAIN × In 1, LEDs off      Reference
 *   9  GAIN × In 1, LED 1–3 blink               If the blinking grows with GAIN, it arises at the input
 *
 * In addition, the patch itself measures at the input, in units of 1e-7 full scale (1 ≙ 1 µV
 * at ±10 V), and reports it as a debug message (tools/owl_read.py shows the last one):
 *   "LEDblink In1-3"  after phase 3: mean of the raw inputs 1–3 with LED on minus LED off,
 *                     excluding the first 4 ms after each change, over all cycles
 *   "LEDstat In1-3"   after phase 6: mean in phase 5 minus mean of phases 4 and 6,
 *                     each excluding the first second, last cycle
 * In phases 2–7 the outputs carry no signal that could cross-talk onto the inputs.
 *
 * "Red" in the firmware means red plus green with duty cycle 1023/1024 (messlog D72). Build:
 *   SRC=owl tools/build_owl_patch.sh SchliereLedPath SchliereLedPathPatch test/SchliereLedPathPatch.hpp
 */

#include "../schliere/SchlierePatch.hpp"

void debugMessage(const char* msg, int, int, int); // message.h of the SDK, a stand-in in the host test

class SchliereLedPathPatch : public SchlierePatch {
public:
  static constexpr float PHASE_S = 5.0f;
  static constexpr size_t PHASES = 10;
  static constexpr size_t FLICKER_BLOCKS = 75; // blocks per half period: 12.5 ms on, 12.5 ms off
  static constexpr size_t SKIP_BLOCKS = 24;    // do not measure for 4 ms after each change
  static constexpr float SETTLE_S = 1.0f;      // static measurement: skip the first second of each phase
  static constexpr float GAIN = 4.0f;
  static constexpr float CAL_HZ = 300.0f;
  static constexpr float CAL_LEVEL = 0.001f;   // −60 dBFS ≙ 10 mV peak at Out 1
  static constexpr float RED = 1.0f;
  static constexpr size_t MAX_BLOCK = 64;

  void processAudio(AudioBuffer& buffer){
    const size_t size = buffer.getSize();
    const float sr = getSampleRate();
    const size_t perPhase = (size_t)(PHASE_S * sr / size);
    const size_t phase = block / perPhase, inPhase = block % perPhase;
    const bool blink = phase == 1 || phase == 3 || phase == 9;
    const bool on = blink && ((inPhase / FLICKER_BLOCKS) & 1);
    FloatArray io[4] = { buffer.getSamples(0), buffer.getSamples(1),
                         buffer.getSamples(2), buffer.getSamples(3) };

    // Measurement at the input, before anything else: the outputs overwrite the buffers
    float sum[3] = { 0.0f, 0.0f, 0.0f };
    for(size_t ch = 0; ch < 3; ++ch)
      for(size_t i = 0; i < size; ++i)
        sum[ch] += io[ch][i];
    if(phase == 3 && inPhase % FLICKER_BLOCKS >= SKIP_BLOCKS){
      for(size_t ch = 0; ch < 3; ++ch) blinkSum[on][ch] += sum[ch];
      blinkCount[on] += size;
    }
    if(phase >= 4 && phase <= 6 && inPhase >= (size_t)(SETTLE_S * sr / size)){
      if(phase == 4 && inPhase == (size_t)(SETTLE_S * sr / size))
        for(size_t n = 0; n < 2; ++n){ statCount[n] = 0; for(size_t ch = 0; ch < 3; ++ch) statSum[n][ch] = 0.0; }
      const size_t lit = phase == 5;
      for(size_t ch = 0; ch < 3; ++ch) statSum[lit][ch] += sum[ch];
      statCount[lit] += size;
    }
    float dry[MAX_BLOCK];
    const size_t n = size < MAX_BLOCK ? size : MAX_BLOCK;
    for(size_t i = 0; i < n; ++i) dry[i] = io[0][i];

    SchlierePatch::processAudio(buffer);

    if(phase >= 2 && phase <= 7){
      for(size_t ch = 0; ch < 4; ++ch)
        for(size_t i = 0; i < size; ++i) io[ch][i] = 0.0f;
      if(phase == 7){
        for(size_t i = 0; i < size; ++i){
          io[0][i] = CAL_LEVEL * (sinf)(calPhase);
          calPhase += 2.0f * 3.14159265f * CAL_HZ / sr;
          if(calPhase > 2.0f * 3.14159265f) calPhase -= 2.0f * 3.14159265f;
        }
      }
    }else if(phase >= 8){
      for(size_t ch = 0; ch < 4; ++ch)
        for(size_t i = 0; i < n; ++i) io[ch][i] = GAIN * dry[i];
    }

    float led[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    if(on) led[0] = led[1] = led[2] = RED;
    if(phase == 5) led[0] = led[1] = led[2] = led[3] = RED;
    for(int i = 0; i < 4; ++i)
      setParameterValue(PatchParameterId(PARAMETER_AA + i), led[i]);

    if(inPhase == perPhase - 1){
      if(phase == 3 && blinkCount[0] && blinkCount[1])
        debugMessage("LEDblink In1-3", unit(blinkSum[1][0] / blinkCount[1] - blinkSum[0][0] / blinkCount[0]),
                     unit(blinkSum[1][1] / blinkCount[1] - blinkSum[0][1] / blinkCount[0]),
                     unit(blinkSum[1][2] / blinkCount[1] - blinkSum[0][2] / blinkCount[0]));
      if(phase == 6 && statCount[0] && statCount[1])
        debugMessage("LEDstat In1-3", unit(statSum[1][0] / statCount[1] - statSum[0][0] / statCount[0]),
                     unit(statSum[1][1] / statCount[1] - statSum[0][1] / statCount[0]),
                     unit(statSum[1][2] / statCount[1] - statSum[0][2] / statCount[0]));
    }
    if(++block >= PHASES * perPhase)
      block = 0;
  }

private:
  static int unit(double x){ // in 1e-7 full scale, rounded
    const double v = x * 1e7;
    return (int)(v < 0.0 ? v - 0.5 : v + 0.5);
  }
  double blinkSum[2][3] = {}, statSum[2][3] = {};
  size_t blinkCount[2] = {}, statCount[2] = {};
  float calPhase = 0.0f;
  size_t block = 0;
};

#endif // __SCHLIERE_LED_PATH_PATCH_HPP__
