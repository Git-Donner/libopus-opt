/* Independent host IEEE binary32 oracle; not an Opus test or build input. */
#pragma STDC FENV_ACCESS ON
#include <fenv.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const int ns[] = {12,16,18,22,24,32,36,44,48,64,72,88,96,144,176};
static const uint32_t cutoffs[] = {
  0x3e13cd3b,0x3e000000,0x3df15bef,0x3dda514a,0x3dd105ec,
  0x3db504f4,0x3daaaaab,0x3d9a5fb2,0x3d93cd3b,0x3d800000,
  0x3d715bef,0x3d5a514a,0x3d5105ec,0x3d2aaaab,0x3d1a5fb2
};

static float from_bits(uint32_t u) { float x; memcpy(&x,&u,sizeof(x)); return x; }

static __attribute__((noinline)) int original(float x, int n, float t) {
  volatile float square = x*x;
  volatile float scaled = square*(float)n;
  return scaled < t;
}

static __attribute__((noinline)) int magnitude(float x, float cutoff) {
  return fabsf(x) < cutoff;
}

int main(void) {
  static const float ts[] = {.25f,.0625f,.015625f};
  static const int rms[] = {FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
  static const char *names[] = {"nearest","downward","upward","towardzero"};
  uint32_t random_state = 0x61759b23;
  unsigned long checks = 0;
  int mode, ni, ti, off, sign, sample;
  for (mode=0;mode<4;mode++) {
    unsigned long mismatches = 0;
    if (fesetround(rms[mode])) return 2;
    for (ni=0;ni<15;ni++) for (ti=0;ti<3;ti++) {
      uint32_t cutoff = cutoffs[ni]-ti*0x00800000;
      for (off=-1024;off<=1024;off++) for (sign=0;sign<2;sign++) {
        uint32_t xb = (cutoff+off) | (sign ? 0x80000000 : 0);
        float x = from_bits(xb);
        int old = original(x,ns[ni],ts[ti]);
        int newer = magnitude(x,from_bits(cutoff));
        if (old != newer) {
          if (!mismatches) printf("%s first difference N=%d T=%g xbits=0x%08x old=%d cutoff=%d\n",
            names[mode],ns[ni],(double)ts[ti],xb,old,newer);
          mismatches++;
        }
        checks++;
      }
    }
    printf("%s boundary mismatches: %lu\n",names[mode],mismatches);
    if (!mode && mismatches) return 1;
  }
  if (fesetround(FE_TONEAREST)) return 2;
  for (ni=0;ni<15;ni++) for (sample=0;sample<262144;sample++) {
    float x;
    random_state ^= random_state<<13;
    random_state ^= random_state>>17;
    random_state ^= random_state<<5;
    x = from_bits(random_state);
    for (ti=0;ti<3;ti++) {
      int old = original(x,ns[ni],ts[ti]);
      int newer = magnitude(x,from_bits(cutoffs[ni]-ti*0x00800000));
      if (old != newer) return 1;
      checks++;
    }
  }
  printf("PASS: %lu host boundary/random comparisons (including both signs and all-bit-pattern random inputs)\n",checks);
  {
    static const uint32_t samples[] = {0x3dcccccd,0x7f7fffff,1};
    static const char *sample_names[] = {"0.1f","FLT_MAX","smallest subnormal"};
    for (sample=0;sample<3;sample++) {
      int old, newer, oldflags, newflags;
      float x = from_bits(samples[sample]);
      feclearexcept(FE_ALL_EXCEPT);
      old=original(x,12,.25f);
      oldflags=fetestexcept(FE_ALL_EXCEPT);
      feclearexcept(FE_ALL_EXCEPT);
      newer=magnitude(x,from_bits(cutoffs[0]));
      newflags=fetestexcept(FE_ALL_EXCEPT);
      printf("flags %-18s old=0x%x new=0x%x classification=%d/%d\n",
        sample_names[sample],oldflags,newflags,old,newer);
    }
  }
  return 0;
}
