/* Copyright (c) 2007-2008 CSIRO
   Copyright (c) 2007-2009 Xiph.Org Foundation
   Copyright (c) 2008-2009 Gregory Maxwell
   Written by Jean-Marc Valin and Gregory Maxwell */
/*
   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   - Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

   - Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
   OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/* This full function is copied from the frozen pre-optimization bands.c.
   Only its symbol is renamed. Keep it independent of production scan helpers
   and the common decision tail so tests can detect regressions in either. */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include "test_spreading_reference.h"
#include "mathops.h"

int spreading_decision_original(const CELTMode *m, const celt_norm *X, int *average,
      int last_decision, int *hf_average, int *tapset_decision, int update_hf,
      int end, int C, int M, const int *spread_weight)
{
   int i, c, N0;
   int sum = 0, nbBands=0;
   const opus_int16 * OPUS_RESTRICT eBands = m->eBands;
   int decision;
   int hf_sum=0;

   celt_assert(end>0);

   N0 = M*m->shortMdctSize;

   if (M*(eBands[end]-eBands[end-1]) <= 8)
      return SPREAD_NONE;
   c=0; do {
      for (i=0;i<end;i++)
      {
         int j, N, tmp=0;
         int tcount[3] = {0,0,0};
         const celt_norm * OPUS_RESTRICT x = X+M*eBands[i]+c*N0;
         N = M*(eBands[i+1]-eBands[i]);
         if (N<=8)
            continue;
         /* Compute rough CDF of |x[j]| */
         for (j=0;j<N;j++)
         {
            opus_val32 x2N; /* Q13 */

            x2N = MULT16_16(MULT16_16_Q15(SHR32(x[j], NORM_SHIFT-14), SHR32(x[j], NORM_SHIFT-14)), N);
            if (x2N < QCONST16(0.25f,13))
               tcount[0]++;
            if (x2N < QCONST16(0.0625f,13))
               tcount[1]++;
            if (x2N < QCONST16(0.015625f,13))
               tcount[2]++;
         }

         /* Only include four last bands (8 kHz and up) */
         if (i>m->nbEBands-4)
            hf_sum += celt_udiv(32*(tcount[1]+tcount[0]), N);
         tmp = (2*tcount[2] >= N) + (2*tcount[1] >= N) + (2*tcount[0] >= N);
         sum += tmp*spread_weight[i];
         nbBands+=spread_weight[i];
      }
   } while (++c<C);

   if (update_hf)
   {
      if (hf_sum)
         hf_sum = celt_udiv(hf_sum, C*(4-m->nbEBands+end));
      *hf_average = (*hf_average+hf_sum)>>1;
      hf_sum = *hf_average;
      if (*tapset_decision==2)
         hf_sum += 4;
      else if (*tapset_decision==0)
         hf_sum -= 4;
      if (hf_sum > 22)
         *tapset_decision=2;
      else if (hf_sum > 18)
         *tapset_decision=1;
      else
         *tapset_decision=0;
   }
   /*printf("%d %d %d\n", hf_sum, *hf_average, *tapset_decision);*/
   celt_assert(nbBands>0); /* end has to be non-zero */
   celt_assert(sum>=0);
   sum = celt_udiv((opus_int32)sum<<8, nbBands);
   /* Recursive averaging */
   sum = (sum+*average)>>1;
   *average = sum;
   /* Hysteresis */
   sum = (3*sum + (((3-last_decision)<<7) + 64) + 2)>>2;
   if (sum < 80)
   {
      decision = SPREAD_AGGRESSIVE;
   } else if (sum < 256)
   {
      decision = SPREAD_NORMAL;
   } else if (sum < 384)
   {
      decision = SPREAD_LIGHT;
   } else {
      decision = SPREAD_NONE;
   }
#ifdef FUZZING
   decision = rand()&0x3;
   *tapset_decision=rand()%3;
#endif
   return decision;
}

/* Separate, plain floating-point description of the original scan. This
   deliberately does not use the production arithmetic or scan helpers. */
void spreading_statistics_original(const CELTMode *m, const celt_norm *X,
      int update_hf, int end, int C, int M, const int *spread_weight,
      int *sum, int *nb_bands, int *hf_sum)
{
   int band, channel;
   *sum = *nb_bands = *hf_sum = 0;
   for (channel = 0; channel < C; channel++)
   {
      for (band = 0; band < end; band++)
      {
         int j;
         int count[3] = {0, 0, 0};
         int N = M*(m->eBands[band+1] - m->eBands[band]);
         const celt_norm *x = X + channel*M*m->shortMdctSize + M*m->eBands[band];
         if (N <= 8) continue;
         for (j = 0; j < N; j++)
         {
#ifdef FIXED_POINT
            /* This helper is linked in fixed builds only to make test targets
               portable; floating-point NEON tests skip those builds. */
            opus_val32 x2N = MULT16_16(MULT16_16_Q15(
                  SHR32(x[j], NORM_SHIFT-14), SHR32(x[j], NORM_SHIFT-14)), N);
            count[0] += x2N < QCONST16(.25f, 13);
            count[1] += x2N < QCONST16(.0625f, 13);
            count[2] += x2N < QCONST16(.015625f, 13);
#else
            float squared = x[j]*x[j];
            float scaled = squared*N;
            count[0] += scaled < .25f;
            count[1] += scaled < .0625f;
            count[2] += scaled < .015625f;
#endif
         }
         *sum += ((2*count[0] >= N) + (2*count[1] >= N)
               + (2*count[2] >= N))*spread_weight[band];
         *nb_bands += spread_weight[band];
         if (update_hf && band > m->nbEBands-4)
            *hf_sum += 32*(count[0] + count[1])/N;
      }
   }
}
