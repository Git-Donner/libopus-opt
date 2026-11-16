#include "bands.h"
#include <stdio.h>
#include <string.h>

static int calls_c, calls_neon;

void __real_spreading_statistics_c(const CELTMode *, const celt_norm *,
      int, int, int, int, const int *, int *, int *, int *);
void __real_spreading_statistics_neon(const CELTMode *, const celt_norm *,
      int, int, int, int, const int *, int *, int *, int *);

void __wrap_spreading_statistics_c(const CELTMode *m, const celt_norm *x,
      int u, int e, int c, int M, const int *w, int *s, int *n, int *h)
{
   calls_c++;
   __real_spreading_statistics_c(m, x, u, e, c, M, w, s, n, h);
}

void __wrap_spreading_statistics_neon(const CELTMode *m, const celt_norm *x,
      int u, int e, int c, int M, const int *w, int *s, int *n, int *h)
{
   calls_neon++;
   __real_spreading_statistics_neon(m, x, u, e, c, M, w, s, n, h);
}

int main(void)
{
   CELTMode mode;
   const opus_int16 ebands[] = {0, 5, 14, 25, 38};
   const int weights[] = {1, 2, 4, 8};
   float x[76];
   int arch, update, i;
   memset(&mode, 0, sizeof(mode));
   mode.eBands = ebands;
   mode.nbEBands = 4;
   mode.shortMdctSize = 38;
   for (i=0; i<76; i++)
      x[i] = (float)((i*29)%71-35)/127.f;
   for (arch=0; arch<3; arch++)
      if (SPREADING_STATISTICS_IMPL[arch] != spreading_statistics_c)
         return 1;
   for (arch=3; arch<5; arch++)
      if (SPREADING_STATISTICS_IMPL[arch] != spreading_statistics_neon)
         return 2;
   for (update=0; update<2; update++) {
      for (arch=0; arch<5; arch++) {
         int expected_avg=240, expected_hf=19, expected_tap=1;
         int actual_avg=240, actual_hf=19, actual_tap=1;
         int expected_decision, actual_decision;
         expected_decision = spreading_decision(&mode, x, &expected_avg,
               SPREAD_NORMAL, &expected_hf, &expected_tap, update,
               4, 2, 1, weights, 0);
         calls_c = calls_neon = 0;
         actual_decision = spreading_decision(&mode, x, &actual_avg,
               SPREAD_NORMAL, &actual_hf, &actual_tap, update,
               4, 2, 1, weights, arch+8);
         if (calls_c != (arch<3) || calls_neon != (arch>=3))
            return 3;
         if (actual_decision!=expected_decision || actual_avg!=expected_avg ||
               actual_hf!=expected_hf || actual_tap!=expected_tap)
            return 4;
      }
   }
   {
      int average=240, hf=19, tap=1;
      calls_c = calls_neon = 0;
      if (spreading_decision(&mode, x, &average, SPREAD_NORMAL, &hf, &tap,
               1, 1, 2, 1, weights, 3) != SPREAD_NONE)
         return 5;
      if (calls_c || calls_neon || average!=240 || hf!=19 || tap!=1)
         return 6;
   }
   puts("PASS: ARM RTCD slots 0..2=C, 3..4=NEON; masked arch dispatches exactly once; decision and history agree; early return skips scan");
   return 0;
}
