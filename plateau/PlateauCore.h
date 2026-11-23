#ifndef __PLATEAU_CORE_H__
#define __PLATEAU_CORE_H__

/* Plateau core: float port of the plate reverb in Valley's Plateau.
 *
 * Source: ValleyRackFree 2.4.5, src/Plateau/Dattorro.{hpp,cpp} and the helpers it uses from
 * src/dsp (InterpDelay, AllpassFilter, OnePoleLPFilter, OnePoleHPFilter, TriSawLFO).
 * Copyright (C) 2020 Valley Audio Soft, Dale Johnson. GPL-3.0-or-later.
 * Port to the AC/DC (OWL patch, 48 kHz, float) 2026, same license.
 *
 * Topology unchanged (Jon Dattorro 1997, "Effect Design Part 1", with Plateau's own extras):
 *   in L, R → 20 Hz DC blockers → mono sum → input low cut 13.75 Hz → input high cut 14.08 kHz
 *   → pre-delay → 4 input allpasses (crossfade "diffuse input") → tank, a figure eight:
 *   allpass 1 (modulated) → delay 1 → high cut → low cut → × decay → allpass 2 (modulated)
 *   → delay 2 → × decay → other half. Both outputs are sums of 7 taps from both halves
 *   (Plateau mirrors the taps of the left side for the right one), each through a 20 Hz
 *   DC blocker, × 0.5. Size scales the eight tank delays, not the taps.
 *
 * Changes against the original:
 *   - float instead of double (PLATEAU_SAMPLE, double only in the host test against the
 *     original), int32 instead of int64 indices. LFO phase as a 64-bit accumulator: a float
 *     phase would drift by up to 1 % in frequency at 0.1 Hz.
 *   - No allocation after init(): all buffers in one go (PlateauMemory), sized for the
 *     largest size factor. Every line is at least as long as its longest output tap.
 *   - Parameters per block, not per sample. Size, pre-delay, input diffusion and decay are
 *     ramped linearly across the block; filters, LFOs and diffusion gains change per block.
 *   - Real sample rate everywhere. Plateau 2.4.5 clamps the tank to 44.1 kHz and never gives
 *     the tank filters and LFOs a sample rate: at 48 kHz its tank times are 8.1 % short and
 *     its LFOs 8.8 % fast. compat245 = true in init() reproduces that (host test against
 *     the original only).
 *   - LFO start phases 0, ¼, ½, ¾ as written (2.4.5 sets them but never reads them).
 *   - Freeze crossfade (tank filters out of the loop) 0.1 s as in Plateau v1, 2.4.5 has 1 s.
 *   - Clear in chunks over several blocks (startClear/serviceClear), not in one sample.
 *
 * Memory: the patch heap of OpenWare (FreeRTOS heap_5) is first fit over the segments in
 * address order: ITCM (CCMRAM, 39.2 KB on the fx firmware), DTCM (PATCHRAM, 80 KB), AXI
 * SRAM (512 KB, minus the patch image when linked there), SDRAM (32 MB, probably uncached).
 * There is no API to choose the segment: the order of the allocations decides. init()
 * allocates small hot buffers first, so they land in the fast TCMs, the long delays follow
 * in AXI, and the pre-delay comes last, it is the first to spill into SDRAM.
 * PlateauMemory counts the bytes per segment; the patch reports them.
 */

#include <math.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#ifdef ARM_CORTEX
extern "C" void* pvPortMalloc(size_t size); // heap_5 of OwlProgram, what operator new calls
extern "C" void vPortFree(void* p);
#else
#include <stdlib.h>
#endif

#ifndef PLATEAU_MAX_SIZE
#define PLATEAU_MAX_SIZE 4.0f       // largest size factor (Plateau: 4), decides the memory
#endif
#ifndef PLATEAU_MAX_PREDELAY
#define PLATEAU_MAX_PREDELAY 0.150f // seconds
#endif
#ifndef PLATEAU_SAMPLE
#define PLATEAU_SAMPLE float        // double only for the host test against the original
#endif
typedef PLATEAU_SAMPLE PlateauSample;
#ifndef PLATEAU_MAX_DIFFUSION2
#define PLATEAU_MAX_DIFFUSION2 0.7f // gain of allpass 2 at full diffusion: 2.x 0.7, v1 0.5
#endif
#ifndef PLATEAU_PROBE
#define PLATEAU_PROBE(section) // cycle probes, empty except in test/PlateauProfilePatch.hpp
#endif

/* Float buffers from the patch heap, zeroed, with the bytes counted per memory segment */
class PlateauMemory {
public:
  enum Region { ITCM, DTCM, AXI, SDRAM, OTHER, REGIONS };
  static constexpr size_t MAX_BLOCKS = 16;

  PlateauMemory(){}
  PlateauMemory(const PlateauMemory&) = delete;
  PlateauMemory& operator=(const PlateauMemory&) = delete;
  ~PlateauMemory(){
    for(size_t i = 0; i < count; ++i)
#ifdef ARM_CORTEX
      vPortFree(blocks[i]);
#else
      free(blocks[i]);
#endif
  }

  PlateauSample* allocate(size_t n){
    if(count >= MAX_BLOCKS){
      failed = true;
      return NULL;
    }
#ifdef ARM_CORTEX
    PlateauSample* p = (PlateauSample*)pvPortMalloc(n * sizeof(PlateauSample));
#else
    PlateauSample* p = (PlateauSample*)malloc(n * sizeof(PlateauSample));
#endif
    if(p == NULL){
      failed = true;
      return NULL;
    }
    memset(p, 0, n * sizeof(PlateauSample));
    blocks[count] = p;
    sizes[count] = n;
    count++;
    bytes[region(p)] += n * sizeof(PlateauSample);
    return p;
  }
  // read access for the measurement patch (test/PlateauProfilePatch.hpp)
  size_t blockCount() const { return count; }
  PlateauSample* blockData(size_t i) const { return blocks[i]; }
  size_t blockLength(size_t i) const { return sizes[i]; }

  static Region region(const void* p){
    const uintptr_t a = (uintptr_t)p;
    if(a < 0x00010000u) return ITCM;
    if(a >= 0x20000000u && a < 0x20020000u) return DTCM;
    if(a >= 0x24000000u && a < 0x24080000u) return AXI;
    if(a >= 0xc0000000u && a < 0xc2000000u) return SDRAM;
    return OTHER;
  }
  size_t total() const {
    size_t t = 0;
    for(size_t i = 0; i < REGIONS; ++i) t += bytes[i];
    return t;
  }

  /* Zero all buffers in chunks: at most maxFloats per call. Returns true when done. */
  void startClear(){ clearBlock = 0; clearPos = 0; }
  bool serviceClear(size_t maxFloats){
    while(maxFloats > 0 && clearBlock < count){
      size_t n = sizes[clearBlock] - clearPos;
      if(n > maxFloats) n = maxFloats;
      memset(blocks[clearBlock] + clearPos, 0, n * sizeof(PlateauSample));
      clearPos += n;
      maxFloats -= n;
      if(clearPos == sizes[clearBlock]){
        clearBlock++;
        clearPos = 0;
      }
    }
    return clearBlock >= count;
  }

  size_t bytes[REGIONS] = {};
  bool failed = false;

private:
  PlateauSample* blocks[MAX_BLOCKS] = {};
  size_t sizes[MAX_BLOCKS] = {};
  size_t count = 0;
  size_t clearBlock = 0, clearPos = 0;
};

/* Delay line with linear interpolation, as Valley's InterpDelay: write first, then read,
 * delay time 0 returns the sample just written. tap(i) reads the sample written i samples
 * ago (i ≥ 1), independent of the delay time. */
struct PlateauDelay {
  PlateauSample* buf = NULL;
  int32_t len = 0, w = 0, t = 0;
  PlateauSample f = 0.0f, out = 0.0f;

  bool init(PlateauMemory& mem, int32_t length){
    len = length;
    buf = mem.allocate((size_t)length);
    return buf != NULL;
  }
  inline void setTime(PlateauSample time){
    const PlateauSample last = (PlateauSample)(len - 1);
    if(time > last) time = last;
    if(time < 0.0f) time = 0.0f;
    t = (int32_t)time;
    f = time - (PlateauSample)t;
  }
  inline PlateauSample process(PlateauSample in){
    buf[w] = in;
    int32_t r = w - t;
    if(r < 0) r += len;
    if(++w == len) w = 0;
    int32_t u = r - 1;
    if(u < 0) u += len;
    out = buf[r] + f * (buf[u] - buf[r]);
    return out;
  }
  inline PlateauSample tap(int32_t i) const {
    int32_t j = w - i;
    if(j < 0) j += len;
    return buf[j];
  }
};

/* Schroeder allpass around a PlateauDelay, as Valley's AllpassFilter. The feedback reads the
 * output of the previous sample, so the loop is one sample longer than the delay time. */
struct PlateauAllpass {
  PlateauDelay delay;
  PlateauSample gain = 0.0f, out = 0.0f;

  inline PlateauSample process(PlateauSample in){
    const PlateauSample sum = in + delay.out * gain;
    out = delay.out - sum * gain;
    delay.process(sum);
    return out;
  }
};

/* One-pole lowpass, as Valley's OnePoleLPFilter */
struct PlateauLowpass {
  PlateauSample a = 1.0f, b = 0.0f, z = 0.0f, hz = -1.0f;
  void set(PlateauSample cutoff, PlateauSample rate){
    if(cutoff == hz) return;
    hz = cutoff;
    b = (expf)(-6.28318531f * cutoff / rate);
    a = 1.0f - b;
  }
  inline PlateauSample process(PlateauSample x){
    z = a * x + b * z;
    return z;
  }
};

/* One-pole highpass, as Valley's OnePoleHPFilter */
struct PlateauHighpass {
  PlateauSample a0 = 1.0f, a1 = -1.0f, b1 = 0.0f, x1 = 0.0f, y1 = 0.0f, hz = -1.0f;
  void set(PlateauSample cutoff, PlateauSample rate){
    if(cutoff == hz) return;
    hz = cutoff;
    b1 = (expf)(-6.28318531f * cutoff / rate);
    a0 = 0.5f * (1.0f + b1);
    a1 = -a0;
  }
  inline PlateauSample process(PlateauSample x){
    const PlateauSample y = a0 * x + a1 * x1 + b1 * y1;
    x1 = x;
    y1 = y;
    return y;
  }
};

/* Triangle/saw LFO, as Valley's TriSawLFO: rises from −1 to 1 until the reversal point,
 * then falls back. The phase is a 64-bit accumulator: with 32 bits the increment at 0.1 Hz
 * is only accurate to 3e-5, enough to shift the modulated delay times measurably. */
struct PlateauLfo {
  uint64_t phase = 0, inc = 0;
  PlateauSample rev = 0.5f, rise = 2.0f, fall = 2.0f;

  void setPhase(double p){ phase = (uint64_t)(p * (double)18446744073709551616.0f); }
  void setFrequency(double hz, double rate){ inc = (uint64_t)(hz / rate * (double)18446744073709551616.0f); }
  void setShape(PlateauSample r){
    if(r < 0.0001f) r = 0.0001f;
    if(r > 0.999f) r = 0.999f;
    if(r == rev) return;
    rev = r;
    rise = 1.0f / r;
    fall = 1.0f / (1.0f - r);
  }
  inline PlateauSample process(){
    PlateauSample s;
    if(sizeof(PlateauSample) == sizeof(float))
      s = (PlateauSample)(uint32_t)(phase >> 32) * (PlateauSample)2.3283064365386963e-10; // one VCVT
    else
      s = (PlateauSample)(phase >> 11) * (PlateauSample)1.1102230246251565e-16;
    phase += inc;
    const PlateauSample o = s < rev ? s * rise : (1.0f - s) * fall;
    return 2.0f * o - 1.0f;
  }
};

class PlateauReverb {
public:
  /* Values per block. Plateau's units where possible. */
  struct Params {
    PlateauSample timeScale = 1.0f;      // size factor, Plateau 0.01 … 4 (tuned mode from 0.0025)
    PlateauSample preDelay = 0.0f;       // seconds
    PlateauSample inputDiffusion = 1.0f; // 0 = input allpasses bypassed, 1 = on (Plateau: switch)
    PlateauSample decay = 0.5f;          // gain per half loop, applied twice per half (0 … 1)
    PlateauSample diffusion = 1.0f;      // tank diffusion 0 … 1 (Plateau 0 … 10)
    PlateauSample highCut = 14080.0f;    // tank high cut, Hz
    PlateauSample lowCut = 13.75f;       // tank low cut, Hz
    PlateauSample modSpeed = 1.0f;       // LFO rate factor, Plateau 1 … 100
    PlateauSample modDepth = 0.5f;       // Plateau 0 … 16
    PlateauSample modShape = 0.5f;       // reversal point of the LFOs, 0.001 … 0.999
  };

  static constexpr PlateauSample DATTORRO_RATE = 29761.0f;
  static constexpr PlateauSample LOOP_SAMPLES = 21589.0f; // figure eight at size 1, Dattorro samples
  static constexpr PlateauSample LFO_EXCURSION = 16.0f;   // Dattorro samples per unit of mod depth
  static constexpr PlateauSample MAX_MOD_DEPTH = 16.0f;
  static constexpr PlateauSample MAX_DIFFUSION1 = 0.7f;
  static constexpr PlateauSample MAX_DIFFUSION2 = PLATEAU_MAX_DIFFUSION2;
  static constexpr int32_t GUARD = 8;

  PlateauMemory memory;

  /* Allocates everything. Returns false if the heap ran out (memory.failed). */
  bool init(PlateauSample fs, PlateauSample maxTimeScale = PLATEAU_MAX_SIZE, PlateauSample maxPreDelay = PLATEAU_MAX_PREDELAY,
            bool compat245 = false){
    sampleRate = fs;
    tankRate = compat245 && fs > 44100.0f ? 44100.0f : fs;
    srs = tankRate / DATTORRO_RATE;
    maxScale = maxTimeScale;
    maxPre = maxPreDelay;
    const PlateauSample inScale = fs / DATTORRO_RATE;
    for(int k = 0; k < 7; ++k)
      taps[k] = (int32_t)((PlateauSample)OUTPUT_TAP[k] * srs);

    // allocation order = placement, see the top of this file
    static const PlateauSample inTime[4] = { 141.0f, 107.0f, 379.0f, 277.0f };
    static const PlateauSample inGain[4] = { 0.75f, 0.75f, 0.625f, 0.625f };
    for(int k = 0; k < 4; ++k){
      inApf[k].delay.init(memory, (int32_t)(inTime[k] * inScale) + GUARD);
      inApf[k].delay.setTime(inTime[k] * inScale);
      inApf[k].gain = inGain[k];
    }
    ra1.delay.init(memory, lineLength(RA1, 0, true));
    la2.delay.init(memory, lineLength(LA2, taps[2], true));
    la1.delay.init(memory, lineLength(LA1, 0, true));
    ld1.init(memory, lineLength(LD1, taps[1], false));
    rd1.init(memory, lineLength(RD1, taps[1], false));
    ld2.init(memory, lineLength(LD2, taps[3], false));
    rd2.init(memory, lineLength(RD2, taps[3], false));
    ra2.delay.init(memory, lineLength(RA2, taps[2], true));
    pre.init(memory, (int32_t)(maxPreDelay * fs) + GUARD);
    if(memory.failed)
      return false;

    // input DC blockers: 2.4.5 computes them for 44.1 kHz and never updates them
    inDcL.set(20.0f, compat245 ? 44100.0f : fs);
    inDcR.set(20.0f, compat245 ? 44100.0f : fs);
    inLp.set(14080.0f, fs);  // Plateau's input filters fully open: 440 Hz · 2^(±5)
    inHp.set(13.75f, fs);
    outDcL.set(20.0f, tankRate);
    outDcR.set(20.0f, tankRate);
    fadeStep = compat245 ? 1.0f / tankRate : 1.0f / (0.1f * tankRate);
    if(!compat245){
      lfo[1].setPhase(0.25f);
      lfo[2].setPhase(0.5f);
      lfo[3].setPhase(0.75f);
    }
    return true;
  }
  bool ok() const { return !memory.failed && pre.buf != NULL; }

  /* Once per block, before the n samples of the block. The first call takes the values
   * over without a ramp. */
  void setParams(const Params& p, size_t n){
    const PlateauSample ts = clamp(p.timeScale, 0.0001f, maxScale);
    const PlateauSample pd = clamp(p.preDelay, 0.0f, maxPre) * sampleRate;
    const PlateauSample id = clamp(p.inputDiffusion, 0.0f, 1.0f);
    const PlateauSample dc = clamp(p.decay, 0.0f, 1.0f);
    if(!primed || n == 0){
      timeScale = ts; preTime = pd; diffuse = id; decay = dc;
      tsStep = preStep = diffuseStep = decayStep = 0.0f;
      primed = true;
    }else{
      const PlateauSample r = 1.0f / (PlateauSample)n;
      tsStep = (ts - timeScale) * r;
      preStep = (pd - preTime) * r;
      diffuseStep = (id - diffuse) * r;
      decayStep = (dc - decay) * r;
    }
    const PlateauSample diff = clamp(p.diffusion, 0.0f, 1.0f);
    la1.gain = ra1.gain = -MAX_DIFFUSION1 * diff;
    la2.gain = ra2.gain = MAX_DIFFUSION2 * diff;
    const PlateauSample nyquist = 0.5f * tankRate - 1.0f;
    hcL.set(clamp(p.highCut, 1.0f, nyquist), tankRate);
    hcR.set(clamp(p.highCut, 1.0f, nyquist), tankRate);
    lcL.set(clamp(p.lowCut, 1.0f, nyquist), tankRate);
    lcR.set(clamp(p.lowCut, 1.0f, nyquist), tankRate);
    static const PlateauSample lfoHz[4] = { 0.10f, 0.15f, 0.12f, 0.18f };
    for(int k = 0; k < 4; ++k){
      lfo[k].setFrequency(lfoHz[k] * p.modSpeed, tankRate);
      lfo[k].setShape(p.modShape);
    }
    excursion = clamp(p.modDepth, 0.0f, MAX_MOD_DEPTH) * LFO_EXCURSION * srs;
  }

  /* Freeze: decay 1, tank filters crossfade out of the loop. The input keeps feeding. */
  void setFreeze(bool f){
    frozen = f;
    fadeDir = f ? -1.0f : 1.0f;
  }
  bool isFrozen() const { return frozen; }

  /* One sample. outL/outR in Plateau's units (tank output, before the wet gain). */
  inline void process(PlateauSample inL, PlateauSample inR, PlateauSample& outL, PlateauSample& outR){
    timeScale += tsStep;
    preTime += preStep;
    diffuse += diffuseStep;
    decay += decayStep;
    PLATEAU_PROBE(2);

    const PlateauSample sum = inDcL.process(inL) + inDcR.process(inR);
    const PlateauSample filtered = inHp.process(inLp.process(sum));
    pre.setTime(preTime);
    const PlateauSample p = pre.process(filtered);
    PlateauSample x = p;
    for(int k = 0; k < 4; ++k)
      x = inApf[k].process(x);
    PLATEAU_PROBE(3);
    tank(p + diffuse * (x - p), outL, outR);
  }

  /* Clear: the caller stops calling process() (output silent), calls serviceClear() once per
   * block until it returns true, then the tank starts empty. */
  void startClear(){ memory.startClear(); }
  bool serviceClear(size_t maxFloats){
    if(!memory.serviceClear(maxFloats))
      return false;
    resetState();
    return true;
  }
  void resetState(){
    PlateauDelay* lines[] = { &ld1, &ld2, &rd1, &rd2, &pre, &la1.delay, &la2.delay, &ra1.delay, &ra2.delay,
                              &inApf[0].delay, &inApf[1].delay, &inApf[2].delay, &inApf[3].delay };
    for(PlateauDelay* d : lines) d->out = 0.0f;
    PlateauAllpass* apfs[] = { &la1, &la2, &ra1, &ra2, &inApf[0], &inApf[1], &inApf[2], &inApf[3] };
    for(PlateauAllpass* a : apfs) a->out = 0.0f;
    PlateauHighpass* hps[] = { &inDcL, &inDcR, &inHp, &lcL, &lcR, &outDcL, &outDcR };
    for(PlateauHighpass* h : hps) h->x1 = h->y1 = 0.0f;
    inLp.z = hcL.z = hcR.z = 0.0f;
    leftSum = rightSum = 0.0f;
  }

  PlateauSample getSampleRate() const { return sampleRate; }
  PlateauSample getTimeScale() const { return timeScale; }

private:
  static constexpr PlateauSample LA1 = 672.0f, LD1 = 4453.0f, LA2 = 1800.0f, LD2 = 3720.0f;
  static constexpr PlateauSample RA1 = 908.0f, RD1 = 4217.0f, RA2 = 2656.0f, RD2 = 3163.0f;
  // left output: ld1, ld1, la2, ld2, rd1, ra2, rd2; right output: the same with sides swapped
  static constexpr int32_t OUTPUT_TAP[7] = { 266, 2974, 1913, 1996, 1990, 187, 1066 };

  static PlateauSample clamp(PlateauSample x, PlateauSample lo, PlateauSample hi){ return x < lo ? lo : (x > hi ? hi : x); }

  int32_t lineLength(PlateauSample time, int32_t tap, bool modulated) const {
    PlateauSample need = time * maxScale + (modulated ? MAX_MOD_DEPTH * LFO_EXCURSION : 0.0f);
    PlateauSample n = srs * need;
    if(n < (PlateauSample)tap) n = (PlateauSample)tap;
    return (int32_t)n + GUARD;
  }

  inline void tank(PlateauSample in, PlateauSample& outL, PlateauSample& outR){
    const PlateauSample s = timeScale * srs;
    la1.delay.setTime(lfo[0].process() * excursion + LA1 * s);
    la2.delay.setTime(lfo[1].process() * excursion + LA2 * s);
    ra1.delay.setTime(lfo[2].process() * excursion + RA1 * s);
    ra2.delay.setTime(lfo[3].process() * excursion + RA2 * s);
    ld1.setTime(LD1 * s);
    ld2.setTime(LD2 * s);
    rd1.setTime(RD1 * s);
    rd2.setTime(RD2 * s);

    const PlateauSample g = frozen ? 1.0f : decay;
    leftSum += in;
    rightSum += in;

    const PlateauSample l1 = ld1.process(la1.process(leftSum));
    const PlateauSample lf = lcL.process(hcL.process(l1));
    const PlateauSample l2 = ld2.process(la2.process((l1 + fade * (lf - l1)) * g));

    const PlateauSample r1 = rd1.process(ra1.process(rightSum));
    const PlateauSample rf = lcR.process(hcR.process(r1));
    const PlateauSample r2 = rd2.process(ra2.process((r1 + fade * (rf - r1)) * g));

    rightSum = l2 * g;
    leftSum = r2 * g;
    PLATEAU_PROBE(6);

    PlateauSample yl = la1.out;
    yl += ld1.tap(taps[0]);
    yl += ld1.tap(taps[1]);
    yl -= la2.delay.tap(taps[2]);
    yl += ld2.tap(taps[3]);
    yl -= rd1.tap(taps[4]);
    yl -= ra2.delay.tap(taps[5]);
    yl -= rd2.tap(taps[6]);

    PlateauSample yr = ra1.out;
    yr += rd1.tap(taps[0]);
    yr += rd1.tap(taps[1]);
    yr -= ra2.delay.tap(taps[2]);
    yr += rd2.tap(taps[3]);
    yr -= ld1.tap(taps[4]);
    yr -= la2.delay.tap(taps[5]);
    yr -= ld2.tap(taps[6]);

    outL = outDcL.process(yl) * 0.5f;
    outR = outDcR.process(yr) * 0.5f;

    fade += fadeStep * fadeDir;
    fade = fade < 0.0f ? 0.0f : (fade > 1.0f ? 1.0f : fade);
  }

  PlateauSample sampleRate = 48000.0f, tankRate = 48000.0f, srs = 1.0f, maxScale = 1.0f, maxPre = 0.0f;
  int32_t taps[7] = {};

  PlateauHighpass inDcL, inDcR, inHp;
  PlateauLowpass inLp;
  PlateauDelay pre;
  PlateauAllpass inApf[4];

  PlateauAllpass la1, la2, ra1, ra2;
  PlateauDelay ld1, ld2, rd1, rd2;
  PlateauLowpass hcL, hcR;
  PlateauHighpass lcL, lcR;
  PlateauHighpass outDcL, outDcR;
  PlateauLfo lfo[4];

  PlateauSample timeScale = 1.0f, preTime = 0.0f, diffuse = 1.0f, decay = 0.5f;
  PlateauSample tsStep = 0.0f, preStep = 0.0f, diffuseStep = 0.0f, decayStep = 0.0f;
  PlateauSample excursion = 0.0f;
  PlateauSample leftSum = 0.0f, rightSum = 0.0f;
  bool frozen = false, primed = false;
  PlateauSample fade = 1.0f, fadeStep = 0.0f, fadeDir = 1.0f;
};

#endif
