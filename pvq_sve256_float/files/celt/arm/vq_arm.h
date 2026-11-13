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

#ifndef VQ_ARM_H
#define VQ_ARM_H

#include "armcpu.h"

#if !defined(FIXED_POINT) && \
    (defined(OPUS_ARM_MAY_HAVE_NEON_INTR) || defined(OPUS_ARM_PRESUME_NEON_INTR))

opus_val16 op_pvq_search_neon(celt_norm *X, int *iy, int K, int N, int arch);

#if defined(OPUS_ARM_MAY_HAVE_SVE256_INTR)
opus_val16 op_pvq_search_sve256(celt_norm *X, int *iy, int K, int N, int arch);
opus_val16 op_pvq_search_arm_sve256(celt_norm *X, int *iy, int K, int N, int arch);
#define OPUS_PVQ_SEARCH_NEON_IMPL op_pvq_search_arm_sve256
#else
#define OPUS_PVQ_SEARCH_NEON_IMPL op_pvq_search_neon
#endif

#if defined(OPUS_ARM_PRESUME_NEON_INTR)

#define OVERRIDE_OP_PVQ_SEARCH (1)
#define op_pvq_search(X, iy, K, N, arch) \
    (OPUS_PVQ_SEARCH_NEON_IMPL(X, iy, K, N, arch))

#elif defined(OPUS_HAVE_RTCD)

#define OVERRIDE_OP_PVQ_SEARCH (1)
extern opus_val16 (*const OP_PVQ_SEARCH_IMPL[OPUS_ARCHMASK + 1])(
      celt_norm *X, int *iy, int K, int N, int arch);

#define op_pvq_search(X, iy, K, N, arch) \
    ((*OP_PVQ_SEARCH_IMPL[(arch) & OPUS_ARCHMASK])(X, iy, K, N, arch))

#endif
#endif

#endif /* VQ_ARM_H */
