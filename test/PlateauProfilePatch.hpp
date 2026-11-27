#ifndef __PLATEAU_PROFILE_PATCH_HPP__
#define __PLATEAU_PROFILE_PATCH_HPP__

/* Measurement patch, not a music patch: Plateau (patches/plateau/PlateauPatch.hpp) unchanged, plus
 * cycle probes per section and a latency test of the memory segments. Sound and controls are
 * those of Plateau. Why: Plateau reports 51 % CPU, about 5000 cycles per sample, but runs only
 * about 1000 instructions per sample (measurement log D82).
 *
 * The probes read the DWT cycle counter, which the firmware resets at the start of each block.
 * The PLATEAU_PROBE(n) calls in the Plateau sources are empty in the normal build.
 * Sections, in cycles per sample, mean over one second:
 *   c  control rate at the start of the block (knobs, macros, setParams), per sample
 *   i  inputs: block-rate cancellers, normalling of in 2, clear envelope
 *   n  core input: DC blockers, input filters, pre-delay, four input allpasses
 *   t  tank loop: four modulated allpasses, four delays, damping filters
 *   o  output taps of the tank (14 reads)
 *   s  mix, saturation, energy follower
 *   p  end of the block: clear service, NaN check, LEDs, per sample
 *
 * Latency test, once when the patch starts: strided reads (one new 32-byte cache line each
 * time, two passes, the second one counts) over the largest Plateau buffer in each segment,
 * and strided writes of zeros into the SDRAM buffer (the pre-delay, still silent then).
 * Cycles per access, 0 = no buffer in that segment.
 *
 * Two messages alternate every second (only the last one is kept, read it with owl_read.py):
 *   "PA c i n t o s p"
 *   "PB all max probe rI rD rA rS wS cc"
 *   all = sum of the sections per sample, max = the slowest block of that second per sample,
 *   probe = cost of one probe (already part of the sections, about four per sample),
 *   cc = cache bits of SCB->CCR (bit 0: D-cache on, bit 1: I-cache on). hardware.h of the ACDC
 *   turns both caches on only with NDEBUG, which the Release build does not define.
 */

#include <stdint.h>

#ifdef ARM_CORTEX
static inline uint32_t plateauCycles(){ return *(volatile uint32_t*)0xE0001004u; } // DWT_CYCCNT
static inline uint32_t plateauCacheBits(){ return (*(volatile uint32_t*)0xE000ED14u >> 16) & 3u; } // SCB->CCR DC, IC
#else
static uint32_t plateauHostClock = 0; // host test: 10 cycles per call
static inline uint32_t plateauCycles(){ return plateauHostClock += 10; }
static inline uint32_t plateauCacheBits(){ return 3u; }
#endif

static uint32_t plateauProbeSum[8];
static uint32_t plateauProbeLast = 0;
static int plateauProbeSection = 7;

/* section n starts now; the time since the last probe goes to the previous section */
static inline void plateauProbe(int n){
  const uint32_t t = plateauCycles();
  plateauProbeSum[plateauProbeSection] += t - plateauProbeLast;
  plateauProbeLast = t;
  plateauProbeSection = n;
}

#define PLATEAU_PROBE(n) plateauProbe(n)
#include "../patches/plateau/PlateauPatch.hpp"

class PlateauProfilePatch : public PlateauPatch {
public:
  enum { CONTROL = 0, INPUT = 1, CORE_IN = 2, TANK = 3, OUTPUT = 4, POST = 5, TAPS = 6, IDLE = 7 };

  PlateauProfilePatch(){
    rate = getSampleRate();
    // cost of one probe
    plateauProbeLast = plateauCycles();
    plateauProbeSection = IDLE;
    const uint32_t t0 = plateauCycles();
    for(int k = 0; k < 256; ++k)
      plateauProbe(IDLE);
    probeCost = (plateauCycles() - t0) / 256;
    for(int k = 0; k < 8; ++k) plateauProbeSum[k] = 0;
    latencyTest();
  }

  void processAudio(AudioBuffer& buffer){
    const size_t size = buffer.getSize();
    for(int k = 0; k < 8; ++k) plateauProbeSum[k] = 0;
    const uint32_t t0 = plateauCycles();
    plateauProbeLast = t0;
    plateauProbeSection = CONTROL;
    PlateauPatch::processAudio(buffer);
    plateauProbe(IDLE);
    const uint32_t block = plateauProbeLast - t0;
    if(size == 0) return;
    for(int k = 0; k < 7; ++k) window[k] += plateauProbeSum[k];
    windowAll += block;
    const uint32_t perSample = block / (uint32_t)size;
    if(perSample > windowMax) windowMax = perSample;
    windowSamples += size;
    if(windowSamples >= (uint64_t)rate){
      report();
      for(int k = 0; k < 7; ++k) window[k] = 0;
      windowAll = 0;
      windowMax = 0;
      windowSamples = 0;
    }
  }

  // for the host test
  const char* lastText() const { return text; }
  uint32_t latency(int region) const { return readCycles[region]; }

private:
  float rate = 48000.0f;
  uint64_t window[7] = {}, windowAll = 0, windowSamples = 0;
  uint32_t windowMax = 0, probeCost = 0;
  uint32_t readCycles[PlateauMemory::REGIONS] = {}, writeCyclesSdram = 0;
  volatile float sink = 0.0f;
  bool second = false;
  char text[64] = {};

  void latencyTest(){
    const PlateauMemory& m = reverb().memory;
    for(int r = 0; r < PlateauMemory::REGIONS; ++r){
      size_t best = m.blockCount();
      for(size_t i = 0; i < m.blockCount(); ++i)
        if(PlateauMemory::region(m.blockData(i)) == r &&
           (best == m.blockCount() || m.blockLength(i) > m.blockLength(best)))
          best = i;
      if(best == m.blockCount()) continue;
      volatile PlateauSample* b = m.blockData(best);
      const size_t n = m.blockLength(best) / 8;
      if(n == 0) continue;
      float acc = 0.0f;
      uint32_t t = 0;
      for(int pass = 0; pass < 2; ++pass){
        const uint32_t t0 = plateauCycles();
        for(size_t i = 0; i < n; ++i)
          acc += b[i * 8];
        t = plateauCycles() - t0;
      }
      sink = acc;
      readCycles[r] = t / (uint32_t)n;
      if(r == PlateauMemory::SDRAM){
        const uint32_t t0 = plateauCycles();
        for(size_t i = 0; i < n; ++i)
          b[i * 8] = 0.0f; // still silent: the patch has not run yet
        writeCyclesSdram = (plateauCycles() - t0) / (uint32_t)n;
      }
    }
  }

  static char* put(char* s, const char* t){ while(*t) *s++ = *t++; return s; }
  static char* put(char* s, uint64_t v){
    char d[24];
    int n = 0;
    do { d[n++] = (char)('0' + v % 10); v /= 10; } while(v > 0 && n < 20);
    *s++ = ' ';
    while(n > 0) *s++ = d[--n];
    return s;
  }

  void report(){
    const uint64_t n = windowSamples;
    char* s = text;
    if(!second){
      static const int order[7] = { CONTROL, INPUT, CORE_IN, TANK, TAPS, OUTPUT, POST };
      s = put(s, "PA");
      for(int k = 0; k < 7; ++k)
        s = put(s, (window[order[k]] + n / 2) / n);
    }else{
      s = put(s, "PB");
      s = put(s, (windowAll + n / 2) / n);
      s = put(s, (uint64_t)windowMax);
      s = put(s, (uint64_t)probeCost);
      s = put(s, (uint64_t)readCycles[PlateauMemory::ITCM]);
      s = put(s, (uint64_t)readCycles[PlateauMemory::DTCM]);
      s = put(s, (uint64_t)readCycles[PlateauMemory::AXI]);
      s = put(s, (uint64_t)readCycles[PlateauMemory::SDRAM]);
      s = put(s, (uint64_t)writeCyclesSdram);
      s = put(s, (uint64_t)plateauCacheBits());
    }
    *s = '\0';
    second = !second;
    debugMessage(text);
  }
};

#endif
