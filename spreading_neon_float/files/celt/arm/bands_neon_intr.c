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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef FIXED_POINT
#include <arm_neon.h>
#include "bands.h"

static OPUS_INLINE int spreading_sum_u32(uint32x4_t count)
{
#if defined(__aarch64__)
   return (int)vaddvq_u32(count);
#else
   uint32x2_t sum = vpadd_u32(vget_low_u32(count), vget_high_u32(count));
   sum = vpadd_u32(sum, sum);
   return (int)vget_lane_u32(sum, 0);
#endif
}

void spreading_statistics_neon(const CELTMode *m, const celt_norm *X,
      int update_hf, int end, int C, int M, const int *spread_weight,
      int *sum, int *nb_bands, int *hf_sum)
{
   int c, i;
   int sum_acc = 0, nb_bands_acc = 0, hf_sum_acc = 0;
   const int N0 = M*m->shortMdctSize;
   const opus_int16 * OPUS_RESTRICT eBands = m->eBands;
   const float32x4_t threshold0 = vdupq_n_f32(0.25f);
   const float32x4_t threshold1 = vdupq_n_f32(0.0625f);
   const float32x4_t threshold2 = vdupq_n_f32(0.015625f);

   c=0; do {
      for (i=0;i<end;i++)
      {
         int j, tmp;
         int tcount[3];
         const int N = M*(eBands[i+1]-eBands[i]);
         const celt_norm * OPUS_RESTRICT x = X+M*eBands[i]+c*N0;
         float32x4_t n4;
         uint32x4_t count0, count1, count2;
         if (N<=8)
            continue;
         n4 = vdupq_n_f32((float)N);
         count0 = count1 = count2 = vdupq_n_u32(0);
         for (j=0;j+3<N;j+=4)
         {
            float32x4_t x4 = vld1q_f32(x+j);
            float32x4_t x2N = vmulq_f32(vmulq_f32(x4, x4), n4);
            /* A true comparison is UINT32_MAX, so subtraction adds one. */
            count0 = vsubq_u32(count0, vcltq_f32(x2N, threshold0));
            count1 = vsubq_u32(count1, vcltq_f32(x2N, threshold1));
            count2 = vsubq_u32(count2, vcltq_f32(x2N, threshold2));
         }
         tcount[0] = spreading_sum_u32(count0);
         tcount[1] = spreading_sum_u32(count1);
         tcount[2] = spreading_sum_u32(count2);
         for (;j<N;j++)
         {
            float x2N = (x[j]*x[j])*(float)N;
            if (x2N < 0.25f)
               tcount[0]++;
            if (x2N < 0.0625f)
               tcount[1]++;
            if (x2N < 0.015625f)
               tcount[2]++;
         }
         if (update_hf && i>m->nbEBands-4)
            hf_sum_acc += celt_udiv(32*(tcount[1]+tcount[0]), N);
         tmp = (2*tcount[2] >= N) + (2*tcount[1] >= N) + (2*tcount[0] >= N);
         sum_acc += tmp*spread_weight[i];
         nb_bands_acc += spread_weight[i];
      }
   } while (++c<C);
   *sum = sum_acc;
   *nb_bands = nb_bands_acc;
   *hf_sum = hf_sum_acc;
}

#endif /* !FIXED_POINT */
