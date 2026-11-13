/* Tests for the floating-point 256-bit SVE PVQ search.
   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are met:
   - Redistributions of source code must retain this notice,
     this list of conditions and the following disclaimer.
   - Redistributions in binary form must reproduce this notice,
     this list of conditions and the following disclaimer in the documentation
     and/or other materials provided with the distribution.
   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
   AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
   ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
   LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
   CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
   SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
   CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
   ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
   POSSIBILITY OF SUCH DAMAGE. */

/* Correctness tests, not a performance benchmark. The SVE256 implementation
   must preserve the current NEON pulse vector and returned energy exactly.
   Build this test without SVE target flags, so no-SVE fallback is testable. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vq.h"
#include "stack_alloc.h"
#include "cpu_support.h"

#if !defined(FIXED_POINT) && defined(OPUS_ARM_MAY_HAVE_SVE256_INTR)

#if defined(__linux__)
#include <sys/mman.h>
#include <unistd.h>
#include <sys/auxv.h>
#include <sys/prctl.h>
#include <asm/hwcap.h>
#include <linux/prctl.h>
#include <pthread.h>
#endif

#define MAX_TEST_N 256
#define GUARD_VALUE 0x5a319c27

typedef struct {
   unsigned char *allocation;
   size_t allocation_size;
   int *guard;
   void *data;
   int mapped;
} GuardedBuffer;

static unsigned int random_state = 0x9e3779b9U;
static int tests_run;
static int have_sve;
static int direct_tests;
static int requested_vl;

extern opus_val16 op_pvq_search_sve256(celt_norm *, int *, int, int, int);

static unsigned int next_random(void)
{
   random_state ^= random_state << 13;
   random_state ^= random_state >> 17;
   random_state ^= random_state << 5;
   return random_state;
}

static void die(const char *message, int N, int K, int pattern)
{
   fprintf(stderr, "FAIL: %s (N=%d K=%d pattern=%d)\n", message, N, K, pattern);
   exit(EXIT_FAILURE);
}

/* On Linux, the last requested element is immediately followed by a PROT_NONE
   page: even a single-lane tail overread/write causes a failure. Elsewhere a
   canary protects each end. malloc/mmap preserve adequate base alignment. */
static GuardedBuffer guarded_alloc(int N)
{
   GuardedBuffer b;
   memset(&b, 0, sizeof(b));
#if defined(__linux__)
   {
      long page = sysconf(_SC_PAGESIZE);
      if (page > 0 && (size_t)(N + 1)*sizeof(int) <= (size_t)page) {
         b.allocation_size = (size_t)page*2;
         b.allocation = mmap(NULL, b.allocation_size, PROT_READ|PROT_WRITE,
                            MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
         if (b.allocation != MAP_FAILED &&
             mprotect(b.allocation + page, (size_t)page, PROT_NONE) == 0) {
            b.mapped = 1;
            b.data = b.allocation + page - (size_t)N*sizeof(int);
            b.guard = (int *)b.data - 1;
            *b.guard = GUARD_VALUE;
            return b;
         }
         if (b.allocation != MAP_FAILED)
            munmap(b.allocation, b.allocation_size);
      }
   }
#endif
   b.allocation_size = (size_t)(N + 2)*sizeof(int);
   b.allocation = malloc(b.allocation_size);
   if (!b.allocation) die("allocation failed", N, 0, 0);
   b.data = b.allocation + sizeof(int);
   b.guard = (int *)b.allocation;
   *b.guard = ((int *)b.data)[N] = GUARD_VALUE;
   return b;
}

static void guarded_free(GuardedBuffer *b, int N)
{
   if (*b->guard != GUARD_VALUE ||
       (!b->mapped && ((int *)b->data)[N] != GUARD_VALUE))
      die("array boundary canary changed", N, 0, 0);
#if defined(__linux__)
   if (b->mapped) munmap(b->allocation, b->allocation_size);
   else
#endif
      free(b->allocation);
}

static void fill_input(float *x, int N, int pattern)
{
   double energy = 0;
   int j;
   for (j = 0; j < N; j++) {
      float value;
      switch (pattern) {
         case 0: value = 0; break;
         case 1: value = j == N - 1 ? -1.f : 0.f; break;
         case 2: value = j & 1 ? -1.f : 1.f; break;
         case 3: value = (float)(j % 4 + 1); break;
         case 4: value = j & 1 ? -1e-30f : 1e-30f; break;
         case 5: value = j & 1 ? -0.f : 0.f; break;
         case 6: value = j & 1 ? -0x1p-149f : 0x1p-149f; break;
         case 7: value = j % 8 < 4 ? 0.5f : -0.5f; break;
         default:
            value = (float)((int)(next_random() & 0xffffU) - 32768)/32768.f;
            break;
      }
      x[j] = value;
      energy += (double)value*value;
   }
   if (energy > 0 && pattern != 4 && pattern != 6)
      for (j = 0; j < N; j++) x[j] = (float)(x[j]/sqrt(energy));
}

static void check_vector(const float *original, const int *iy,
                         float yy, int N, int K, int pattern)
{
   int j;
   int pulses = 0;
   double energy = 0;
   for (j = 0; j < N; j++) {
      if (iy[j] < -K || iy[j] > K) die("pulse outside bounds", N, K, pattern);
      pulses += abs(iy[j]);
      energy += (double)iy[j]*iy[j];
      if ((original[j] < 0 && iy[j] > 0) || (original[j] >= 0 && iy[j] < 0))
         die("wrong pulse sign", N, K, pattern);
   }
   if (pulses != K) die("L1 pulse sum differs from K", N, K, pattern);
   if (!isfinite(yy) || (double)yy != energy)
      die("returned energy differs from pulse energy", N, K, pattern);
}

static void test_case(int N, int K, int pattern)
{
   GuardedBuffer gx = guarded_alloc(N);
   GuardedBuffer gy = guarded_alloc(N);
   float *x = (float *)gx.data;
   int *iy = (int *)gy.data;
   float original[MAX_TEST_N], reference_x[MAX_TEST_N];
   int reference_iy[MAX_TEST_N];
   float yy, reference_yy;
   int arch = opus_select_arch();
   fill_input(x, N, pattern);
   memcpy(original, x, (size_t)N*sizeof(float));
   memcpy(reference_x, x, (size_t)N*sizeof(float));
   reference_yy = op_pvq_search_neon(reference_x, reference_iy, K, N, arch);
   check_vector(original, reference_iy, reference_yy, N, K, pattern);
   if (have_sve) {
      memset(iy, 0x39, (size_t)N*sizeof(int));
      yy = op_pvq_search_sve256(x, iy, K, N, arch);
      if (memcmp(original, x, (size_t)N*sizeof(float)))
         die("SVE modified caller input", N, K, pattern);
      check_vector(original, iy, yy, N, K, pattern);
      if (memcmp(&yy, &reference_yy, sizeof(float)) ||
          memcmp(iy, reference_iy, (size_t)N*sizeof(int)))
         die("SVE differs from unchanged NEON reference", N, K, pattern);
      direct_tests++;
   }
   memset(iy, 0x48, (size_t)N*sizeof(int));
   yy = op_pvq_search(x, iy, K, N, arch);
   if (memcmp(original, x, (size_t)N*sizeof(float)))
      die("dispatcher modified caller input", N, K, pattern);
   check_vector(original, iy, yy, N, K, pattern);
   if (memcmp(&yy, &reference_yy, sizeof(float)) ||
       memcmp(iy, reference_iy, (size_t)N*sizeof(int)))
      die("dispatcher differs from NEON reference", N, K, pattern);
   guarded_free(&gx, N);
   guarded_free(&gy, N);
   tests_run++;
}

static void test_tie_policy(void)
{
   float x[8];
   int iy[8];
   int j, N;
   for (N = 4; N <= 8; N += 4) {
      for (j = 0; j < N; j++) x[j] = 1.f;
      op_pvq_search(x, iy, 1, N, opus_select_arch());
      /* First winner within each lane, largest index across tied lanes. */
      if (iy[3] != 1) die("SSE-compatible equal-score tie policy", N, 1, 2);
   }
   for (j = 0; j < 8; j++) x[j] = 0.f;
   op_pvq_search(x, iy, 1, 8, opus_select_arch());
   if (iy[0] != 1) die("all-zero low-K winner", 8, 1, 0);
}

static void test_projection_defence(void)
{
   int pattern, j;
   for (pattern = 0; pattern < 4; pattern++) {
      float x[8] = {0};
      float original[8];
      int iy[8];
      float yy;
      if (pattern == 0) x[3] = NAN;
      if (pattern == 1) x[0] = INFINITY;
      if (pattern == 2) x[0] = -INFINITY;
      if (pattern == 3) for (j = 0; j < 8; j++) x[j] = 100.f;
      memcpy(original, x, sizeof(x));
      yy = op_pvq_search(x, iy, 128, 8, opus_select_arch());
      if (memcmp(original, x, sizeof(x)))
         die("projection defence modified input", 8, 128, pattern);
      check_vector(original, iy, yy, 8, 128, pattern);
      tests_run++;
   }
}

static int set_vl(int bytes)
{
#if defined(__linux__) && defined(PR_SVE_SET_VL) && defined(PR_SVE_VL_LEN_MASK)
   int actual = prctl(PR_SVE_SET_VL, bytes, 0, 0, 0);
   if (actual < 0) return 0;
   return (actual & PR_SVE_VL_LEN_MASK) == bytes;
#else
   (void)bytes;
   return 0;
#endif
}

/* VL is thread state. The same encoder arch state must not assume another
   thread's VL, and changing VL after the first call must remain safe. */
static void test_vl_changes(void)
{
   static const int sizes[] = {16, 32, 64};
   int v, n, p;
   if (!have_sve) return;
   for (v = 0; v < 3; v++) {
      if (!set_vl(sizes[v])) {
         printf("SKIP: requested SVE VL=%d bytes unavailable\n", sizes[v]);
         continue;
      }
      for (n = 7; n <= 35; n++)
         for (p = 0; p < 8; p++) test_case(n, n/2 + p, p);
      printf("PASS: same-thread VL change to %d bytes\n", sizes[v]);
   }
   if (requested_vl && !set_vl(requested_vl))
      die("failed to restore requested VL", 0, 0, 0);
}

#if defined(__linux__)
static void *thread_test(void *arg)
{
   int bytes = *(int *)arg;
   int n, p;
   if (!set_vl(bytes)) return (void *)1;
   for (n = 7; n <= 19; n++)
      for (p = 0; p < 8; p++) test_case(n, n + p, p);
   return NULL;
}

static void test_thread_vl(void)
{
   int bytes;
   if (!have_sve) return;
   /* Sequential workers keep the deterministic PRNG/counters race-free while
      checking that a new thread can select a VL unlike the parent thread. */
   for (bytes = 16; bytes <= 64; bytes *= 2) {
      pthread_t t;
      void *result;
      if (pthread_create(&t, NULL, thread_test, &bytes) != 0)
         die("pthread_create failed", 0, 0, 0);
      if (pthread_join(t, &result) != 0) die("pthread_join failed", 0, 0, 0);
      if (result) printf("SKIP: child SVE VL=%d bytes unavailable\n", bytes);
      else printf("PASS: child thread SVE VL=%d bytes\n", bytes);
   }
}
#endif

int main(int argc, char **argv)
{
   static const int lengths[] = {2, 3, 4, 5, 7, 8, 9, 10, 11, 12, 15, 16, 17,
                                23, 24, 31, 32, 33, 40, 48, 64, 96, 128,
                                176, 208, 255, 256};
   int l, k, p;
   ALLOC_STACK;
#if defined(__linux__) && defined(HWCAP_SVE)
   have_sve = !!(getauxval(AT_HWCAP) & HWCAP_SVE);
#endif
   if (argc > 1) requested_vl = atoi(argv[1]);
   if (have_sve && requested_vl && !set_vl(requested_vl)) {
      fprintf(stderr, "Requested VL=%d bytes unavailable\n", requested_vl);
      return 77;
   }
   printf("HWCAP_SVE=%d; requested VL=%d bytes\n", have_sve, requested_vl);
   test_tie_policy();
   test_projection_defence();
   for (l = 0; l < (int)(sizeof(lengths)/sizeof(lengths[0])); l++) {
      int N = lengths[l];
      int pulses[] = {0, 1, 2, 3, N/2, N/2 + 1, N - 1, N, N + 3,
                      64, 128, 255, 512, 1024};
      for (k = 0; k < (int)(sizeof(pulses)/sizeof(pulses[0])); k++)
         for (p = 0; p < 30; p++) test_case(N, pulses[k], p);
   }
   test_vl_changes();
#if defined(__linux__)
   test_thread_vl();
#endif
   RESTORE_STACK;
   printf("PASS: %d PVQ cases; exact NEON pulse/energy matches; "
          "direct SVE calls=%d; exact-N guard pages enabled on Linux\n",
          tests_run, direct_tests);
   return EXIT_SUCCESS;
}

#else
int main(void)
{
   puts("SKIP: floating-point ARM NEON + SVE256 build required");
   return 77;
}
#endif
