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

/* This translation unit uses the baseline ISA. Never put it in the SVE
   source group: HWCAP must be checked before executing even CNTW. */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#if !defined(FIXED_POINT) && defined(OPUS_ARM_MAY_HAVE_SVE256_INTR)
#include <sys/auxv.h>
#include <asm/hwcap.h>
#include "vq.h"

static int pvq_have_sve(void)
{
   /* 0=unknown, 1=unavailable, 2=available. Linux HWCAP is process-wide;
      vector length is thread state and is checked in the SVE entry itself.
      Relaxed atomics suffice: this integer publishes no other state. */
   static int cached;
   int state = __atomic_load_n(&cached, __ATOMIC_RELAXED);
   if (!state)
   {
      state = (getauxval(AT_HWCAP) & HWCAP_SVE) ? 2 : 1;
      __atomic_store_n(&cached, state, __ATOMIC_RELAXED);
   }
   return state == 2;
}

opus_val16 op_pvq_search_arm_sve256(celt_norm *X, int *iy, int K, int N, int arch)
{
   if (N >= 8 && pvq_have_sve())
      return op_pvq_search_sve256(X, iy, K, N, arch);
   return op_pvq_search_neon(X, iy, K, N, arch);
}
#endif
