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

/* Floating-point PVQ search using SVE throughout the vector operations.
   The current VL must be 256 bits. Eight-element I/O and projection preserve
   the original four logical accumulation streams and greedy tie policy.
   Build with scalable SVE vector length so the runtime VL guard remains. */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef FIXED_POINT
#include <arm_sve.h>
#include <limits.h>
#include "stack_alloc.h"
#include "mathops.h"
#include "vq.h"

/* Keep the NEON/SSE (lane0+lane2)+(lane1+lane3) reduction order.
   Only the first four logical accumulators participate. A generic ADDV
   would not preserve this floating-point evaluation order. */
static OPUS_INLINE float pvq_sum4_f32(svfloat32_t v)
{
   svbool_t four = svwhilelt_b32_s32(0, 4);
   svuint32_t lanes = svindex_u32(0, 1);
   v = svadd_f32_x(four, v, svtbl_f32(v, sveor_n_u32_x(svptrue_b32(), lanes, 2)));
   v = svadd_f32_x(four, v, svtbl_f32(v, sveor_n_u32_x(svptrue_b32(), lanes, 1)));
   return svlastb_f32(svwhilelt_b32_s32(0, 1), v);
}

static OPUS_INLINE svfloat32_t pvq_rcp_sve(svfloat32_t x)
{
   svfloat32_t r = svrecpe_f32(x);
   return svmul_f32_x(svptrue_b32(), r, svrecps_f32(x, r));
}

/* GCC's normal -O3 contracts NEON vadd(vmul) into FMLA, whereas separate
   SVE ACLE intrinsics do not contract. Use explicit MLA to retain that
   normal-build behavior. For a non-contracted NEON reference (for example,
   -ffp-contract=off or an unoptimized debug build), define
   OPUS_PVQ_SVE_NO_FMA to request the same two rounding steps.
   This switch affects only projection accumulation, not ISA dispatch. */
static OPUS_INLINE svfloat32_t pvq_accumulate4(svfloat32_t acc,
                                             svfloat32_t x, svfloat32_t y)
{
   svbool_t four = svwhilelt_b32_s32(0, 4);
#ifdef OPUS_PVQ_SVE_NO_FMA
   return svadd_f32_m(four, acc, svmul_f32_x(four, x, y));
#else
   return svmla_f32_m(four, acc, x, y);
#endif
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
   svfloat32_t sums;
   svbool_t all, four;
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
   all = svptrue_b32();
   four = svwhilelt_b32_s32(0, 4);
   ALLOC(y, N, celt_norm);
   ALLOC(X, N, celt_norm);
   ALLOC(signy, N, opus_uint32);

   sums = svdup_n_f32(0);
   for (j=0;j<N;j+=8)
   {
      svbool_t active = svwhilelt_b32_s32(j, N);
      svfloat32_t x8 = svld1_f32(active, _X+j);
      svbool_t negative = svcmplt_n_f32(active, x8, 0);
      /* x<0, not the sign bit: negative zero is nonnegative. */
      svst1_u32(active, signy+j, svsel_u32(negative,
                svdup_n_u32(0xffffffffU), svdup_n_u32(0)));
      x8 = svabs_f32_z(active, x8);
      /* Two ORDERED updates reproduce the original four-lane streams.
         Inactive input lanes are explicitly zero, including a short tail. */
      sums = svadd_f32_m(four, sums, x8);
      sums = svadd_f32_m(four, sums, svext_f32(x8, x8, 4));
      svst1_f32(active, X+j, x8);
      svst1_f32(active, y+j, svdup_n_f32(0));
      svst1_s32(active, iy+j, svdup_n_s32(0));
   }
   xy = yy = 0;
   pulsesLeft = K;

   if (K > (N>>1))
   {
      float sum = pvq_sum4_f32(sums);
      svfloat32_t rcp8, xy4, yy4;
      svint32_t pulses_sum = svdup_n_s32(0);
      if (!(sum > EPSILON && sum < 64))
      {
         for (j=0;j<N;j+=8)
         {
            svbool_t active = svwhilelt_b32_s32(j, N);
            svfloat32_t unit = svsel_f32(
                  svcmpeq_n_s32(active, svindex_s32(j, 1), 0),
                  svdup_n_f32(1), svdup_n_f32(0));
            svst1_f32(active, X+j, unit);
         }
         sum = 1.f;
      }
      rcp8 = svmul_f32_x(all, svdup_n_f32((float)(K+.8)),
                        pvq_rcp_sve(svdup_n_f32(sum)));
      xy4 = yy4 = svdup_n_f32(0);
      for (j=0;j<N;j+=8)
      {
         svbool_t active = svwhilelt_b32_s32(j, N);
         svfloat32_t x8 = svld1_f32(active, X+j);
         /* FCVTZS truncates; positive values therefore give floor.
            Zero inactive conversion lanes before the split accumulation. */
         svint32_t iy8 = svcvt_s32_f32_z(active, svmul_f32_x(active, x8, rcp8));
         svfloat32_t y8 = svcvt_f32_s32_z(active, iy8);
         pulses_sum = svadd_s32_x(all, pulses_sum, iy8);
         svst1_s32(active, iy+j, iy8);
         xy4 = pvq_accumulate4(xy4, x8, y8);
         xy4 = pvq_accumulate4(xy4, svext_f32(x8, x8, 4), svext_f32(y8, y8, 4));
         yy4 = pvq_accumulate4(yy4, y8, y8);
         yy4 = pvq_accumulate4(yy4, svext_f32(y8, y8, 4), svext_f32(y8, y8, 4));
         svst1_f32(active, y+j, svadd_f32_x(active, y8, y8));
      }
      pulsesLeft -= (opus_int32)svaddv_s32(all, pulses_sum);
      xy = pvq_sum4_f32(xy4);
      yy = pvq_sum4_f32(yy4);
      /* Defend the pulse budget even for unusually large K or rounding at
         a projection boundary. Normal CELT inputs should not enter here. */
      if (pulsesLeft < 0)
      {
         for (j=0;j<N;j+=8)
         {
            svbool_t active = svwhilelt_b32_s32(j, N);
            svst1_s32(active, iy+j, svdup_n_s32(0));
            svst1_f32(active, y+j, svdup_n_f32(0));
         }
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

   for (j=0;j<N;j+=8)
   {
      svbool_t active = svwhilelt_b32_s32(j, N);
      svint32_t p8 = svld1_s32(active, iy+j);
      svbool_t negative = svcmpne_n_u32(active, svld1_u32(active, signy+j), 0);
      svst1_s32(active, iy+j, svsel_s32(negative, svneg_s32_x(active, p8), p8));
   }
   RESTORE_STACK;
   return yy;
}
#endif /* !FIXED_POINT */
