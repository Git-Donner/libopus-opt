/* Copyright (c) 2007-2008 CSIRO
   Copyright (c) 2007-2009 Xiph.Org Foundation
   Copyright (c) 2007-2016 Jean-Marc Valin */
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

/* Floating-point PVQ search for AArch64 SVE with a 256-bit current VL.
   Projection and sign handling retain the existing NEON arithmetic. The
   greedy scan evaluates eight candidates per iteration, then reconstructs
   the NEON four-stream tie policy. Build with scalable SVE vector length:
   the entry guard must remain valid when another thread uses another VL. */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef FIXED_POINT
#include <arm_neon.h>
#include <arm_sve.h>
#include <limits.h>
#include "stack_alloc.h"
#include "mathops.h"
#include "vq.h"

/* Keep SSE's (lane0+lane2)+(lane1+lane3) reduction order. */
static OPUS_INLINE float pvq_sum_f32(float32x4_t v)
{
   v = vaddq_f32(v, vextq_f32(v, v, 2));
   v = vaddq_f32(v, vrev64q_f32(v));
   return vgetq_lane_f32(v, 0);
}

static OPUS_INLINE opus_int32 pvq_sum_s32(int32x4_t v)
{
   v = vaddq_s32(v, vextq_s32(v, v, 2));
   v = vaddq_s32(v, vrev64q_s32(v));
   return vgetq_lane_s32(v, 0);
}

/* ARM's raw estimate has less precision than SSE RCPPS/RSQRTPS.
   One Newton step gives sufficient precision for the float search without
   requiring AArch64-only division/sqrt instructions or FMA. */
static OPUS_INLINE float32x4_t pvq_rcp_f32(float32x4_t x)
{
   float32x4_t r = vrecpeq_f32(x);
   return vmulq_f32(r, vrecpsq_f32(x, r));
}

static OPUS_INLINE svfloat32_t pvq_rsqrt_sve(svfloat32_t x)
{
   svfloat32_t r = svrsqrte_f32(x);
   /* Identical per-lane operation order to pvq_rsqrt_f32 in the NEON path. */
   return svmul_f32_x(svptrue_b32(), r,
                      svrsqrts_f32(svmul_f32_x(svptrue_b32(), x, r), r));
}

opus_val16 op_pvq_search_sve256(celt_norm *_X, int *iy, int K, int N, int arch)
{
   int i, j, pulsesLeft;
   float xy, yy;
   float32x4_t sums;
   VARDECL(celt_norm, y);
   VARDECL(celt_norm, X);
   VARDECL(opus_uint32, signy);
   SAVE_STACK;

   /* The baseline dispatcher checks hardware support before this function.
      VL belongs to the calling thread and may change after encoder creation.
      Do not compile this file with -msve-vector-bits=256: that would allow the
      compiler to remove this runtime check. */
   if (svcntw() != 8 || N < 8)
   {
      RESTORE_STACK;
      return op_pvq_search_neon(_X, iy, K, N, arch);
   }
   celt_assert(N > 1);
   celt_assert(K >= 0);
   ALLOC(y, N+3, celt_norm);
   ALLOC(X, N+3, celt_norm);
   ALLOC(signy, N+3, opus_uint32);

   /* Only local arrays are padded: neither _X nor iy needs extra elements. */
   OPUS_COPY(X, _X, N);
   X[N] = X[N+1] = X[N+2] = 0;
   OPUS_CLEAR(iy, N);
   sums = vdupq_n_f32(0);
   for (j=0;j<N;j+=4)
   {
      float32x4_t x4 = vld1q_f32(X+j);
      /* Use x<0, not the sign bit: -0 must be treated as in the SSE code. */
      vst1q_u32(signy+j, vcltq_f32(x4, vdupq_n_f32(0)));
      x4 = vabsq_f32(x4);
      sums = vaddq_f32(sums, x4);
      vst1q_f32(X+j, x4);
      vst1q_f32(y+j, vdupq_n_f32(0));
   }
   xy = yy = 0;
   pulsesLeft = K;

   if (K > (N>>1))
   {
      float sum = pvq_sum_f32(sums);
      float32x4_t rcp4, xy4, yy4;
      int32x4_t pulses_sum = vdupq_n_s32(0);
      if (!(sum > EPSILON && sum < 64))
      {
         X[0] = 1.f;
         for (j=1;j<N;j++) X[j] = 0;
         sum = 1.f;
      }
      rcp4 = vmulq_f32(vdupq_n_f32((float)(K+.8)),
                       pvq_rcp_f32(vdupq_n_f32(sum)));
      xy4 = yy4 = vdupq_n_f32(0);
      for (j=0;j<N;j+=4)
      {
         float32x4_t x4 = vld1q_f32(X+j);
         /* Values are nonnegative, so truncation equals floor. */
         int32x4_t iy4 = vcvtq_s32_f32(vmulq_f32(x4, rcp4));
         float32x4_t y4 = vcvtq_f32_s32(iy4);
         pulses_sum = vaddq_s32(pulses_sum, iy4);
         if (j+3 < N)
            vst1q_s32(iy+j, iy4);
         else
         {
            opus_int32 tail[4];
            int t;
            vst1q_s32(tail, iy4);
            for (t=0;j+t<N;t++) iy[j+t] = tail[t];
         }
         xy4 = vaddq_f32(xy4, vmulq_f32(x4, y4));
         yy4 = vaddq_f32(yy4, vmulq_f32(y4, y4));
         vst1q_f32(y+j, vaddq_f32(y4, y4));
      }
      pulsesLeft -= pvq_sum_s32(pulses_sum);
      xy = pvq_sum_f32(xy4);
      yy = pvq_sum_f32(yy4);
      /* Defend the pulse budget even for unusually large K or rounding at
         a projection boundary. Normal CELT inputs should not enter here. */
      if (pulsesLeft < 0)
      {
         OPUS_CLEAR(iy, N);
         for (j=0;j<N+3;j++) y[j] = 0;
         xy = yy = 0;
         pulsesLeft = K;
      }
   }
   celt_sig_assert(pulsesLeft >= 0);
   if (pulsesLeft > N+3)
   {
      float tmp = (float)pulsesLeft;
      yy += tmp*tmp;
      yy += tmp*y[0];
      iy[0] += pulsesLeft;
      pulsesLeft = 0;
   }

   for (i=0;i<pulsesLeft;i++)
   {
      svbool_t all = svptrue_b32();
      svbool_t first_four = svwhilelt_b32_s32(0, 4);
      svfloat32_t best = svdup_n_f32(0);
      svint32_t pos = svdup_n_s32(0);
      svint32_t count = svindex_s32(0, 1);
      svfloat32_t xy8, yy8;
      svint32_t candidates, paired;
      float maximum;
      yy += 1;
      xy8 = svdup_n_f32(xy);
      yy8 = svdup_n_f32(yy);
      for (j=0;j<N;j+=8)
      {
         svbool_t active = svwhilelt_b32_s32(j, N);
         svfloat32_t num = svadd_f32_x(active, svld1_f32(active, X+j), xy8);
         svfloat32_t den = svadd_f32_x(active, svld1_f32(active, y+j), yy8);
         svfloat32_t score;
         /* Estimate/step instructions are unpredicated. Give inactive lanes
            denominator 1 to avoid spurious 0*infinity exceptions in the tail. */
         den = svsel_f32(active, den, svdup_n_f32(1));
         score = svmul_f32_x(active, num, pvq_rsqrt_sve(den));
         svbool_t better = svcmpgt_f32(active, score, best);
         pos = svsel_s32(better, count, pos);
         best = svsel_f32(better, score, best);
         count = svadd_n_s32_x(all, count, 8);
      }
      /* Each SVE lane retains its FIRST maximum. Merge lanes r and r+4
         by MINIMUM position among global winners, then take MAXIMUM over
         r=0..3. This exactly preserves NEON's modulo-4 tie rule: for equal
         N=8,K=1 inputs the chosen index is 3, not 7. */
      maximum = svmaxv_f32(all, best);
      candidates = svsel_s32(svcmpeq_n_f32(all, best, maximum), pos,
                            svdup_n_s32(INT_MAX));
      paired = svmin_s32_x(all, candidates,
                          svext_s32(candidates, candidates, 4));
      paired = svsel_s32(svcmplt_n_s32(all, paired, N), paired,
                        svdup_n_s32(0));
      j = svmaxv_s32(first_four, paired);
      celt_assert(j < N);
      xy += X[j];
      yy += y[j];
      y[j] += 2;
      iy[j]++;
   }

   for (j=0;j+3<N;j+=4)
   {
      int32x4_t p4 = vld1q_s32(iy+j);
      int32x4_t s4 = vreinterpretq_s32_u32(vld1q_u32(signy+j));
      vst1q_s32(iy+j, veorq_s32(vaddq_s32(p4, s4), s4));
   }
   for (;j<N;j++)
      if (signy[j]) iy[j] = -iy[j];
   RESTORE_STACK;
   return yy;
}
#endif /* !FIXED_POINT */
