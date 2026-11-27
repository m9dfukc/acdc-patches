// Host test of the Plateau patch (patches/plateau/PlateauPatch.hpp): memory, silence, decay as RT60
// independent of Space, freeze, gate at in 4 (pulse = clear, held = freeze, negative = clear),
// flush gesture, mix, normalling of in 2, morph at in 3, size sweeps, block sizes, LEDs.
#include "Patch.h"
#include "../patches/plateau/PlateauPatch.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <vector>

static std::string lastMessage;
void debugMessage(const char* msg){ lastMessage = msg; }

static int failures = 0;
static void check(bool ok, const char* name, const char* fmt = "", double a = 0, double b = 0){
  std::printf("%s  %s", ok ? "ok  " : "FAIL", name);
  if(*fmt){ std::printf("  "); std::printf(fmt, a, b); }
  std::printf("\n");
  if(!ok) failures++;
}

static const float SR = 48000.0f;
static const float DEAD = PlateauPatch::KNOB_DEAD;
static float knobRaw(float p){ return DEAD + p * (1.0f - 2.0f * DEAD); }
// knob value for a given RT60 (seconds)
static float decayFor(float rt60){ return 0.93f * std::log(rt60 / 0.3f) / std::log(100.0f); }

struct Rig {
  std::unique_ptr<PlateauPatch> patch{ new PlateauPatch() };
  AudioBuffer buf;
  size_t block;
  explicit Rig(size_t b = 8) : block(b) { buf.ch.assign(4, std::vector<float>(b, 0.0f)); }
  void knobs(float a, float b, float c, float d){
    patch->setParameterValue(PARAMETER_A, knobRaw(a));
    patch->setParameterValue(PARAMETER_B, knobRaw(b));
    patch->setParameterValue(PARAMETER_C, knobRaw(c));
    patch->setParameterValue(PARAMETER_D, knobRaw(d));
  }
  // runs n samples; in(ch, t) gives the input, out collects the four outputs
  template<class F> void run(size_t n, F in, std::vector<float>* out = nullptr){
    for(size_t done = 0; done < n; done += block){
      for(int c = 0; c < 4; ++c)
        for(size_t i = 0; i < block; ++i) buf.ch[c][i] = in(c, t + i);
      patch->processAudio(buf);
      if(out)
        for(int c = 0; c < 4; ++c)
          out[c].insert(out[c].end(), buf.ch[c].begin(), buf.ch[c].end());
      t += block;
    }
  }
  size_t t = 0;
  float led(int i){ return patch->getParameterValue(PatchParameterId(PARAMETER_AA + i)); }
};

static auto silence = [](int, size_t){ return 0.0f; };

static double rms(const std::vector<float>& x, size_t from, size_t to){
  double s = 0; for(size_t i = from; i < to; ++i) s += (double)x[i] * (double)x[i];
  return std::sqrt(s / (double)(to - from));
}
static double db(double x){ return 20.0 * std::log10(x + 1e-30); }
static bool finite(const std::vector<float>& x){ for(float v : x) if(!std::isfinite(v)) return false; return true; }
static float peak(const std::vector<float>& x, size_t from = 0, size_t to = 0){
  if(to == 0) to = x.size(); float p = 0; for(size_t i = from; i < to; ++i) p = std::max(p, std::fabs(x[i])); return p;
}

/* RT60 from the decay of the wet signal (out 3) after a noise burst: slope of the
 * 50 ms RMS between −5 and −35 dB below the level at the end of the burst */
static double measureRt60(float space, float rt60, float character){
  Rig rig;
  rig.knobs(0.5f, space, decayFor(rt60), character);
  std::mt19937 rng(1);
  std::uniform_real_distribution<float> u(-0.3f, 0.3f);
  const size_t burst = (size_t)(0.3f * SR), tail = (size_t)(std::max(4.0f * rt60, 2.0f) * SR);
  std::vector<float> out[4];
  rig.run(burst + tail, [&](int c, size_t t){ return (c < 2 && t < burst) ? u(rng) : 0.0f; }, out);
  const size_t win = (size_t)(0.05f * SR);
  std::vector<double> lv;
  for(size_t s = burst; s + win <= out[2].size(); s += win) lv.push_back(db(rms(out[2], s, s + win)));
  const double ref = lv.empty() ? 0 : lv[0];
  // least squares over the windows in [-5, -35] dB
  double sx = 0, sy = 0, sxx = 0, sxy = 0; int n = 0;
  for(size_t k = 0; k < lv.size(); ++k){
    const double d = lv[k] - ref;
    if(d > -5.0 || d < -35.0) continue;
    const double x = k * 0.05; sx += x; sy += d; sxx += x * x; sxy += x * d; n++;
  }
  if(n < 3) return -1;
  const double slope = (n * sxy - sx * sy) / (n * sxx - sx * sx); // dB per second
  return -60.0 / slope;
}

int main(){
  // --- memory and report ---
  {
    Rig rig;
    rig.knobs(0.5f, 0.5f, 0.5f, 0.4f);
    rig.run(64, silence);
    const PlateauMemory& m = rig.patch->reverb().memory;
    const double kib = m.total() / 1024.0;
    check(rig.patch->reverb().ok(), "allocation");
    check(kib > 570 && kib < 600, "buffers for size 4 and 150 ms pre-delay", "%.1f KiB", kib);
    check(lastMessage.rfind("Plateau KiB ", 0) == 0, "placement report", "", 0, 0);
    std::printf("      message: \"%s\"\n", lastMessage.c_str());
  }

  // --- silence stays silence ---
  {
    Rig rig;
    rig.knobs(0.5f, 0.7f, 0.9f, 0.8f);
    std::vector<float> out[4];
    rig.run((size_t)SR, silence, out);
    float p = 0; bool ok = true;
    for(int c = 0; c < 4; ++c){ p = std::max(p, peak(out[c])); ok = ok && finite(out[c]); }
    check(ok && p < 1e-6f, "silence in, silence out", "peak %.2g", p);
  }

  // --- decay as RT60, independent of Space (where the loop is short enough) ---
  {
    struct Case { float space, rt; } cases[] = { {0.35f, 1.0f}, {0.35f, 3.0f}, {0.35f, 8.0f}, {0.6f, 4.0f},
                                                 {0.85f, 8.0f}, {0.85f, 16.0f} };
    for(const Case& k : cases){
      const double m = measureRt60(k.space, k.rt, 0.45f);
      char name[96];
      std::snprintf(name, sizeof name, "RT60 %.0f s at space %.2f (size %.2f)", (double)k.rt, (double)k.space,
                    (double)PlateauMacros::timeScale(k.space));
      check(m > 0.75 * k.rt && m < 1.3 * k.rt, name, "measured %.2f s", m);
    }
  }

  // --- freeze: the tail holds. Heavy modulation costs a little (linear interpolation),
  // as in Plateau ---
  auto freezeLoss = [](float character, bool tone, bool& red, bool& frozen){
    Rig rig;
    rig.knobs(0.5f, 0.6f, 1.0f, character);
    std::mt19937 rng(2);
    std::uniform_real_distribution<float> u(-0.3f, 0.3f);
    const size_t burst = (size_t)(0.3f * SR), s = (size_t)SR;
    std::vector<float> out[4];
    rig.run(burst + 5 * s, [&](int c, size_t t){
      if(c >= 2 || t >= burst) return 0.0f;
      return tone ? 0.3f * std::sin(0.0392699f * (float)t) : u(rng); }, out); // tone: 300 Hz
    const size_t w = 3 * s / 2; // long windows: tap interference makes the level wander
    red = rig.led(2) == 1.0f;
    frozen = rig.patch->reverb().isFrozen();
    return db(rms(out[2], burst + 7 * s / 2, burst + 7 * s / 2 + w) / rms(out[2], burst + s / 2, burst + s / 2 + w));
  };
  {
    bool red, frozen;
    const double plate = freezeLoss(0.4f, true, red, frozen);
    check(std::fabs(plate) < 1.0, "freeze holds a 300 Hz tail, character 0.4 (0.5–2 s → 3.5–5 s)", "%.2f dB", plate);
    check(red, "LED 3 red while frozen");
    check(frozen, "decay knob at the end stop freezes");
    const double noise = freezeLoss(0.4f, false, red, frozen);
    check(noise > -3.0 && noise < 0.5, "freeze with noise: interpolation dulls the top end", "%.2f dB", noise);
    const double dark = freezeLoss(0.9f, true, red, frozen);
    check(std::fabs(dark) < 1.0, "freeze, 300 Hz, heavy modulation (character 0.9)", "%.2f dB", dark);
  }

  // --- gate at in 4 ---
  auto gateRun = [](size_t pulseLen, float level, double& afterDb, bool& frozenDuring){
    Rig rig;
    rig.knobs(0.5f, 0.6f, decayFor(20.0f), 0.4f);
    std::mt19937 rng(3);
    std::uniform_real_distribution<float> u(-0.3f, 0.3f);
    const size_t burst = (size_t)(0.3f * SR), start = burst + (size_t)(0.2f * SR);
    std::vector<float> out[4];
    frozenDuring = false;
    rig.run(start + (size_t)(0.6f * SR), [&](int c, size_t t){
      if(c < 2) return t < burst ? u(rng) : 0.0f;
      if(c == 3) return (t >= start && t < start + pulseLen) ? level : 0.0f;
      return 0.0f; }, out);
    const double before = rms(out[2], start - 4800, start);
    afterDb = db(rms(out[2], out[2].size() - 4800, out[2].size()) / before);
    frozenDuring = rig.patch->reverb().isFrozen();
  };
  {
    double d; bool f;
    gateRun(48, 0.5f, d, f);                 // 1 ms, +5 V
    check(d < -80.0, "1 ms trigger at in 4 clears", "tail %.1f dB", d);
    gateRun((size_t)(10 * SR), 0.5f, d, f);  // held, still high at the end of the run
    check(f, "held gate freezes");
    check(d > -3.0, "held gate keeps the tail", "%.1f dB", d);
    gateRun((size_t)(0.01f * SR), -0.3f, d, f); // −3 V
    check(d < -80.0, "negative pulse at in 4 clears", "tail %.1f dB", d);
  }
  {
    // released after 200 ms: no clear, the tail decays normally afterwards
    Rig rig;
    rig.knobs(0.5f, 0.6f, decayFor(20.0f), 0.4f);
    std::mt19937 rng(4);
    std::uniform_real_distribution<float> u(-0.3f, 0.3f);
    const size_t burst = (size_t)(0.3f * SR), start = burst + 4800, len = (size_t)(0.2f * SR);
    std::vector<float> out[4];
    rig.run(start + len + (size_t)(0.3f * SR), [&](int c, size_t t){
      if(c < 2) return t < burst ? u(rng) : 0.0f;
      if(c == 3) return (t >= start && t < start + len) ? 0.5f : 0.0f;
      return 0.0f; }, out);
    const double d = db(rms(out[2], out[2].size() - 4800, out[2].size()) / rms(out[2], start - 4800, start));
    check(!rig.patch->reverb().isFrozen() && d > -6.0 && d < 1.0, "gate released after 200 ms: no clear", "%.1f dB", d);
  }

  // --- flush gesture: out of the freeze zone to the left end within 250 ms ---
  {
    Rig rig;
    rig.knobs(0.5f, 0.6f, 1.0f, 0.4f);
    std::mt19937 rng(5);
    std::uniform_real_distribution<float> u(-0.3f, 0.3f);
    const size_t burst = (size_t)(0.3f * SR);
    std::vector<float> out[4];
    rig.run(burst + 9600, [&](int c, size_t t){ return (c < 2 && t < burst) ? u(rng) : 0.0f; }, out);
    const double before = rms(out[2], out[2].size() - 4800, out[2].size());
    rig.knobs(0.5f, 0.6f, 0.5f, 0.4f); rig.run(2400, silence, out); // 50 ms in the middle
    rig.knobs(0.5f, 0.6f, 0.0f, 0.4f); rig.run((size_t)(0.3f * SR), silence, out);
    const double d = db(rms(out[2], out[2].size() - 4800, out[2].size()) / before);
    check(d < -80.0, "flush gesture clears", "tail %.1f dB", d);
    // slowly: no clear
    Rig slow;
    slow.knobs(0.5f, 0.6f, 1.0f, 0.4f);
    std::vector<float> o2[4];
    slow.run(burst + 9600, [&](int c, size_t t){ return (c < 2 && t < burst) ? u(rng) : 0.0f; }, o2);
    slow.knobs(0.5f, 0.6f, 0.5f, 0.4f); slow.run((size_t)(0.4f * SR), silence, o2);
    const double b2 = rms(o2[2], o2[2].size() - 4800, o2[2].size());
    slow.knobs(0.5f, 0.6f, 0.0f, 0.4f); slow.run(4800, silence, o2);
    const double d2 = db(rms(o2[2], o2[2].size() - 2400, o2[2].size()) / b2);
    check(d2 > -40.0, "slow turn to the left end does not clear", "%.1f dB", d2);
  }

  // --- mix ---
  {
    Rig rig;
    rig.knobs(0.0f, 0.5f, 0.5f, 0.4f);
    std::vector<float> out[4];
    auto sine = [](int c, size_t t){ return c == 0 ? 0.5f * std::sin(0.03f * t) : (c == 1 ? 0.4f * std::sin(0.05f * t) : 0.0f); };
    rig.run(24000, sine, out);
    double e = 0; for(size_t i = 4800; i < 24000; ++i) e = std::max(e, (double)std::fabs(out[0][i] - sine(0, i)) + std::fabs(out[1][i] - sine(1, i)));
    check(e < 2e-3, "mix left end: dry only", "max error %.2g", e);
    check(rms(out[2], 4800, 24000) > 0.01, "out 3 carries wet also at mix 0");
    Rig wetRig;
    wetRig.knobs(1.0f, 0.5f, 0.5f, 0.4f);
    std::vector<float> o[4];
    auto quiet = [&](int c, size_t t){ return 0.3f * sine(c, t); };
    wetRig.run(24000, quiet, o);
    double err = 0; for(size_t i = 4800; i < 24000; ++i) err = std::max(err, (double)std::fabs(o[0][i] + o[1][i] - 1.4f * o[2][i]));
    check(err < 1e-4, "mix right end: wet only (out1 + out2 = 2 W · out3)", "%.2g", err);
  }

  // --- normalling of in 2 ---
  {
    Rig rig;
    rig.knobs(0.0f, 0.5f, 0.5f, 0.4f);
    std::vector<float> out[4];
    std::mt19937 rng(6);
    std::normal_distribution<float> n(0.0f, 3e-5f); // open input: −0.3 V offset plus noise
    auto in = [&](int c, size_t t){ return c == 0 ? 0.3f * std::sin(0.02f * t) : (c == 1 ? -0.03f + n(rng) : 0.0f); };
    rig.run((size_t)SR, in, out);
    double e = 0; for(size_t i = 36000; i < 48000; ++i) e = std::max(e, (double)std::fabs(out[1][i] - out[0][i]));
    check(e < 1e-3, "in 2 silent: R follows L", "max diff %.2g", e);
    std::vector<float> o2[4];
    auto both = [&](int c, size_t t){ return c == 0 ? 0.3f * std::sin(0.02f * t) : (c == 1 ? 0.2f * std::sin(0.07f * t) : 0.0f); };
    rig.run((size_t)(0.2f * SR), both, o2);
    double e2 = 0; for(size_t i = 4800; i < o2[1].size(); ++i) e2 = std::max(e2, (double)std::fabs(o2[1][i] - both(1, rig.t - o2[1].size() + i)));
    check(e2 < 2e-3, "signal at in 2: R is in 2 again", "max diff %.2g", e2);
    Rig both2;
    both2.knobs(0.0f, 0.5f, 0.5f, 0.4f);
    std::vector<float> o3[4];
    both2.run((size_t)SR, both, o3);
    double e3 = 0; for(size_t i = 4800; i < o3[1].size(); ++i) e3 = std::max(e3, (double)std::fabs(o3[1][i] - both(1, i)));
    check(e3 < 2e-3, "stereo input stays stereo", "max diff %.2g", e3);
  }

  // --- morph at in 3 ---
  {
    Rig rig;
    rig.knobs(0.5f, 0.0f, 0.3f, 0.2f);
    rig.run((size_t)(0.5f * SR), [](int c, size_t){ return c == 2 ? 0.5f : 0.0f; });
    const float up = rig.patch->reverb().getTimeScale();
    rig.run((size_t)(0.5f * SR), [](int c, size_t){ return c == 2 ? -0.5f : 0.0f; });
    const float down = rig.patch->reverb().getTimeScale();
    const float tight = PlateauMacros::timeScale(PLATEAU_TIGHT.space);
    rig.run((size_t)(0.5f * SR), [](int c, size_t){ return c == 2 ? -0.03f : 0.0f; });
    const float rest = rig.patch->reverb().getTimeScale();
    check(std::fabs(up - PLATEAU_MAX_SIZE) < 0.01f, "in 3 at +5 V: Abyss (size 4)", "size %.3f", up);
    check(std::fabs(down - tight) < 0.01f, "in 3 at −5 V: Tight", "size %.3f, want %.3f", down, tight);
    check(std::fabs(rest - PlateauMacros::MIN_SIZE) < 0.005f, "in 3 open (−0.3 V): the knobs", "size %.4f", rest);
  }

  // --- size sweeps and extreme settings, several block sizes ---
  {
    const size_t blocks[3] = { 8, 16, 64 };
    for(size_t b : blocks){
      Rig rig(b);
      std::mt19937 rng(7);
      std::uniform_real_distribution<float> u(-0.5f, 0.5f);
      std::vector<float> out[4];
      for(int step = 0; step <= 40; ++step){
        const float s = step <= 20 ? step / 20.0f : (40 - step) / 20.0f;
        rig.knobs(1.0f, s, 0.9f, step % 2 ? 1.0f : 0.0f);
        rig.run(2400, [&](int c, size_t){ return c < 2 ? u(rng) : 0.0f; }, out);
      }
      bool ok = true; float p = 0;
      for(int c = 0; c < 4; ++c){ ok = ok && finite(out[c]); p = std::max(p, peak(out[c])); }
      char name[64]; std::snprintf(name, sizeof name, "size sweeps, block %zu: finite, bounded", b);
      check(ok && p <= 0.98f, name, "peak %.3f", p);
    }
  }

  // --- LEDs ---
  {
    Rig rig;
    rig.knobs(0.5f, 0.5f, 0.5f, 0.4f);
    rig.run(4800, [](int c, size_t t){ return c == 0 ? 0.5f * std::sin(0.01f * t) : 0.0f; });
    check(rig.led(0) > 0.5f && rig.led(0) < 0.95f && rig.led(1) < 0.024f, "LED 1 green with the level of in 1, LED 2 dark",
          "%.2f / %.3f", rig.led(0), rig.led(1));
    rig.knobs(0.5f, 0.9f, 0.5f, 0.4f);
    rig.run(4800, silence);
    check(std::fabs(rig.led(1) - 0.9f * 0.9f) < 0.02f, "moved knob 2 shows its value on LED 2", "%.2f", rig.led(1));
    rig.run((size_t)SR, silence);
    check(rig.led(1) < 0.024f, "after 1 s LED 2 shows the level again", "%.3f", rig.led(1));
    rig.run(48, [](int c, size_t){ return c == 3 ? 0.5f : 0.0f; });
    check(rig.led(3) == 1.0f, "LED 4 red while the gate is high");
    rig.run(480, silence);
    check(rig.led(0) == 1.0f && rig.led(1) == 1.0f && rig.led(2) == 1.0f, "all LEDs red right after a clear");
    rig.run(9600, silence);
    check(rig.led(0) < 1.0f && rig.led(3) < 1.0f, "red flash ends");
  }

  // --- rough cost on this host, for orientation only ---
  {
    Rig rig;
    rig.knobs(0.5f, 0.7f, 0.8f, 0.8f);
    std::mt19937 rng(8);
    std::uniform_real_distribution<float> u(-0.5f, 0.5f);
    const auto t0 = std::clock();
    rig.run((size_t)(10 * SR), [&](int c, size_t){ return c < 2 ? u(rng) : 0.0f; });
    const double s = double(std::clock() - t0) / CLOCKS_PER_SEC;
    std::printf("info  host: %.1f %% of real time\n", 100.0 * s / 10.0);
  }

  std::printf(failures ? "%d FAILED\n" : "all cases ok\n", failures);
  return failures ? 1 : 0;
}
