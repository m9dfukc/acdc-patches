// Measurement patch test/PlateauProfilePatch.hpp on the host: the messages alternate, have the
// expected fields, and the sections add up to the block total. On the host the "cycle counter"
// advances by 10 per read, so the numbers only check the bookkeeping, not the device.
#include "PlateauProfilePatch.hpp"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

static std::vector<std::string> messages;
void debugMessage(const char* msg){ messages.push_back(msg); }

static int failures = 0;
static void check(bool ok, const char* name){
  std::printf("%s  %s\n", ok ? "ok  " : "FAIL", name);
  if(!ok) failures++;
}

static std::vector<long> numbers(const std::string& s){
  std::vector<long> v;
  const char* p = s.c_str() + 2;
  while(*p){
    char* e;
    long x = std::strtol(p, &e, 10);
    if(e == p) break;
    v.push_back(x);
    p = e;
  }
  return v;
}

int main(){
  PlateauProfilePatch patch;
  AudioBuffer buf;
  buf.ch.assign(4, std::vector<float>(8, 0.0f));
  for(int k = 0; k < 4; ++k) patch.params[k] = 0.5f;
  for(int b = 0; b < 6000 * 3 + 10; ++b){
    for(int c = 0; c < 4; ++c)
      for(size_t i = 0; i < 8; ++i) buf.ch[c][i] = c < 2 ? 0.1f * (float)((b * 8 + i) % 7) - 0.3f : 0.0f;
    patch.processAudio(buf);
  }
  check(messages.size() == 4, "memory report plus three measurement messages in 3 s");
  if(messages.size() == 4){
    check(messages[0].rfind("Plateau KiB", 0) == 0, "first the memory report of Plateau");
    check(messages[1].rfind("PA ", 0) == 0 && messages[2].rfind("PB ", 0) == 0 && messages[3].rfind("PA ", 0) == 0,
          "PA and PB alternate");
    const std::vector<long> a = numbers(messages[3]), b = numbers(messages[2]);
    check(a.size() == 7, "PA has seven sections");
    check(b.size() == 9, "PB has nine fields");
    for(const std::string& m : messages) check(m.size() <= 62, "message fits into 62 characters");
    if(a.size() == 7 && b.size() == 9){
      long sum = 0;
      for(long x : a) sum += x;
      std::printf("      %s | %s\n", messages[3].c_str(), messages[2].c_str());
      check(std::labs(sum - b[0]) <= 4, "sections add up to the block total (rounding)");
      check(a[1] == 10 && a[2] == 10 && a[3] == 10 && a[4] == 10 && a[5] == 10, "one counter step per sample section");
      check(b[1] + 1 >= b[0], "slowest block not below the mean (integer division)");
      check(b[2] == 10, "probe cost = one counter read");
    }
  }
  std::printf(failures ? "%d FAILED\n" : "all cases ok\n", failures);
  return failures ? 1 : 0;
}
