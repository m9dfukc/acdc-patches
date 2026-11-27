// Plateau core (patches/plateau/PlateauCore.h, float) against the original (ValleyRackFree 2.4.5,
// src/Plateau/Dattorro.cpp, double). Needs the original sources: VALLEY_SRC=<…/src> test/run.sh
// The core runs with compat245 = true (tank at 44.1 kHz, LFO phases 0, 1 s freeze fade, as 2.4.5).
// Built twice by run.sh:
//   -DPLATEAU_SAMPLE=double  must match the original up to rounding (< −95 dB): checks the logic
//   float (as on the device) differs more, because a float delay time of ~10000 samples is only
//                            accurate to 1e-3 samples and the error recirculates (< −45 dB)
// With compat245 = false the core must differ clearly from 2.4.5.
#include "../patches/plateau/PlateauCore.h"
#include "Plateau/Dattorro.hpp"
#include <cmath>
#include <cstdio>
#include <vector>

static int failures = 0;
static void check(bool ok, const char* name, double v){
  std::printf("%s  %s  %.1f dB\n", ok ? "ok  " : "FAIL", name, v);
  if(!ok) failures++;
}

struct Setting {
  const char* name;
  double size, preDelay, decay, diffusion; // diffusion 0 … 10 as in Plateau
  double highPitch, lowPitch;              // Plateau's 0 … 10, 440 Hz · 2^(p − 5)
  double speed, depth, shape;
  int freezeAt;                            // sample index, −1 = never
};

static double pitchHz(double p){ return 440.0 * std::pow(2.0, p - 5.0); }

/* error energy relative to the signal energy, dB */
static double compare(const Setting& s, bool compat245, double& level){
  const double fs = 48000.0;
  Dattorro ref(192000.0, 16.0, 4.0);
  ref.setSampleRate(fs);
  PlateauReverb core;
  core.init((PlateauSample)fs, 4.0f, 1.0f, compat245);
  PlateauReverb::Params p;
  p.timeScale = (PlateauSample)s.size;
  p.preDelay = (PlateauSample)s.preDelay;
  p.inputDiffusion = 1.0f;
  p.decay = (PlateauSample)s.decay;
  p.diffusion = (PlateauSample)(s.diffusion / 10.0);
  p.highCut = (PlateauSample)pitchHz(s.highPitch);
  p.lowCut = (PlateauSample)pitchHz(s.lowPitch);
  p.modSpeed = (PlateauSample)s.speed;
  p.modDepth = (PlateauSample)s.depth;
  p.modShape = (PlateauSample)s.shape;
  core.setParams(p, 1);
  const size_t n = (size_t)(2.0 * fs);
  double err = 0, sig = 0;
  for(size_t t = 0; t < n; ++t){
    // what Plateau::process does every sample
    ref.setTimeScale(s.size);
    ref.setPreDelay(s.preDelay);
    ref.setInputFilterLowCutoffPitch(0.0);   // input filters fully open
    ref.setInputFilterHighCutoffPitch(10.0);
    ref.enableInputDiffusion(true);
    ref.setDecay(s.decay);
    ref.setTankDiffusion(s.diffusion);
    ref.setTankFilterLowCutFrequency(s.lowPitch);
    ref.setTankFilterHighCutFrequency(s.highPitch);
    ref.setTankModSpeed(s.speed);
    ref.setTankModDepth(s.depth);
    ref.setTankModShape(s.shape);
    if((int)t == s.freezeAt){ ref.freeze(true); core.setFreeze(true); }
    // input: a short noise-like burst (deterministic), then silence
    const double x = t < 2400 ? 0.3 * std::sin(0.37 * t) * std::sin(0.011 * t) : 0.0;
    ref.process(x, x);
    PlateauSample l, r;
    core.process((PlateauSample)x, (PlateauSample)x, l, r);
    const double dl = l - ref.getLeftOutput(), dr = r - ref.getRightOutput();
    err += dl * dl + dr * dr;
    sig += ref.getLeftOutput() * ref.getLeftOutput() + ref.getRightOutput() * ref.getRightOutput();
  }
  level = 10.0 * std::log10(sig / (2.0 * n) + 1e-30);
  return 10.0 * std::log10(err / sig + 1e-30);
}

int main(){
  const Setting settings[] = {
    { "defaults (size 1, decay 0.9, diffusion 10, filters open, depth 0.5)",
      1.0, 0.0, 0.9, 10.0, 10.0, 0.0, 1.0, 0.5, 0.5, -1 },
    { "size 0.5, pre-delay 20 ms, dark, fast deep modulation, diffusion 6",
      0.5, 0.02, 0.95, 6.0, 7.0, 3.0, 10.0, 4.0, 0.3, -1 },
    { "size 3.5, long decay, depth 12, shape 0.8",
      3.5, 0.0, 0.99, 10.0, 8.5, 1.0, 3.0, 12.0, 0.8, -1 },
    { "freeze after 0.25 s",
      1.2, 0.0, 0.7, 10.0, 6.0, 2.0, 1.0, 1.0, 0.5, 12000 },
  };
  const bool dbl = sizeof(PlateauSample) == sizeof(double);
  std::printf("core in %s\n", dbl ? "double" : "float");
  for(const Setting& s : settings){
    double level;
    const double e = compare(s, true, level);
    char name[160];
    std::snprintf(name, sizeof name, "2.4.5 mode vs. original: %s (signal %.0f dBFS)", s.name, level);
    check(e < (dbl ? -95.0 : -45.0), name, e);
  }
  double level;
  const double d = compare(settings[0], false, level);
  check(d > -20.0, "corrected 48 kHz tank differs from 2.4.5 (defaults)", d);
  std::printf(failures ? "%d FAILED\n" : "all cases ok\n", failures);
  return failures ? 1 : 0;
}
