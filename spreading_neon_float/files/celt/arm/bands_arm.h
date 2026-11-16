/* Copyright (c) 2016 Jean-Marc Valin */
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

#ifndef BANDS_ARM_H
#define BANDS_ARM_H

#include "cpu_support.h"

void spreading_statistics_neon(const CELTMode *m, const celt_norm *X,
      int update_hf, int end, int C, int M, const int *spread_weight,
      int *sum, int *nb_bands, int *hf_sum);

#if defined(OPUS_ARM_PRESUME_NEON_INTR)
#define OVERRIDE_SPREADING_STATISTICS (1)
#define spreading_statistics(m, X, update_hf, end, C, M, weight, sum, nb, hf, arch) \
   ((void)(arch), spreading_statistics_neon(m, X, update_hf, end, C, M, weight, sum, nb, hf))
#elif defined(OPUS_HAVE_RTCD)
#define OVERRIDE_SPREADING_STATISTICS (1)
extern void (*const SPREADING_STATISTICS_IMPL[OPUS_ARCHMASK + 1])(
      const CELTMode *, const celt_norm *, int, int, int, int,
      const int *, int *, int *, int *);
#define spreading_statistics(m, X, update_hf, end, C, M, weight, sum, nb, hf, arch) \
   ((*SPREADING_STATISTICS_IMPL[(arch) & OPUS_ARCHMASK])(m, X, update_hf, end, C, M, weight, sum, nb, hf))
#endif

#endif /* BANDS_ARM_H */
