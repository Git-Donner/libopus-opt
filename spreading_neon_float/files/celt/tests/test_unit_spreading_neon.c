/* Exact equivalence and boundary tests for the float NEON spreading scan.
   This test is intentionally separate from the frozen reference implementation.
   It is a correctness test, not a performance benchmark. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bands.h"
#include "cpu_support.h"
#include "test_spreading_reference.h"

#if !defined(FIXED_POINT) && defined(OPUS_ARM_MAY_HAVE_NEON_INTR) && !defined(FUZZING)

#if defined(__linux__)
#include <sys/mman.h>
#include <unistd.h>
#endif

#define PATTERNS 32
#define CANARY 0x5a319c27U

typedef struct {
   unsigned char *allocation;
   size_t bytes;
   float *x;
   uint32_t *before;
   int mapped;
   int length;
} GuardedInput;

typedef struct {
   int sum, nb_bands, hf_sum;
} Statistics;

typedef struct {
   int average, hf_average, tapset, last;
} History;

static uint32_t random_state = 0x9e3779b9U;
static unsigned long cases_run;
static unsigned long guarded_sequences;
static int runtime_arch;
static int context_N, context_C, context_M, context_end, context_pattern;

static void fail(const char *what)
{
   fprintf(stderr, "FAIL: %s (N=%d C=%d M=%d end=%d pattern=%d)\n",
         what, context_N, context_C, context_M, context_end, context_pattern);
   exit(EXIT_FAILURE);
}

static uint32_t next_random(void)
{
   random_state ^= random_state << 13;
   random_state ^= random_state >> 17;
   random_state ^= random_state << 5;
   return random_state;
}

/* The very last float is adjacent to a protected page. There is no readable
   SIMD padding. The single-band modes make this the end of the scanned band,
   including the second channel in stereo. Varying N covers every alignment. */
static GuardedInput guarded_alloc(int length)
{
   GuardedInput b;
   size_t data_bytes = (size_t)length*sizeof(float);
   memset(&b, 0, sizeof(b));
   b.length = length;
#if defined(__linux__)
   {
      long page = sysconf(_SC_PAGESIZE);
      if (page > 0)
      {
         size_t accessible = (data_bytes + sizeof(uint32_t) + page-1)/page*page;
         b.bytes = accessible + 2*page;
         b.allocation = mmap(NULL, b.bytes, PROT_READ|PROT_WRITE,
               MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
         if (b.allocation != MAP_FAILED)
         {
            if (mprotect(b.allocation, (size_t)page, PROT_NONE) == 0 &&
                  mprotect(b.allocation + page + accessible, (size_t)page,
                     PROT_NONE) == 0)
            {
               b.mapped = 1;
               b.x = (float *)(b.allocation + page + accessible - data_bytes);
               b.before = (uint32_t *)b.x - 1;
               *b.before = CANARY;
               guarded_sequences++;
               return b;
            }
            munmap(b.allocation, b.bytes);
         }
      }
   }
#endif
   b.bytes = data_bytes + 2*sizeof(uint32_t);
   b.allocation = (unsigned char *)malloc(b.bytes);
   if (!b.allocation) fail("input allocation");
   b.x = (float *)(b.allocation + sizeof(uint32_t));
   b.before = (uint32_t *)b.allocation;
   *b.before = CANARY;
   memcpy(b.x + length, b.before, sizeof(uint32_t));
   return b;
}

static void guarded_free(GuardedInput *b)
{
   uint32_t after = CANARY;
   if (!b->mapped) memcpy(&after, b->x + b->length, sizeof(after));
   if (*b->before != CANARY || after != CANARY) fail("input canary changed");
#if defined(__linux__)
   if (b->mapped) munmap(b->allocation, b->bytes);
   else
#endif
      free(b->allocation);
}

static void fill_band(float *x, int N, int pattern)
{
   static const float thresholds[3] = {.25f, .0625f, .015625f};
   double energy = 0;
   int j;
   for (j = 0; j < N; j++)
   {
      float v;
      switch (pattern)
      {
         case 0: v = 0.f; break;
         case 1: v = j&1 ? -0.f : 0.f; break;
         case 2: v = 1.f/sqrtf((float)N); break;
         case 3: v = j == N-1 ? -1.f : 0.f; break;
         case 4: v = (j&1 ? -1.f : 1.f)/sqrtf((float)N); break;
         case 5: v = j&1 ? -FLT_MIN : FLT_MIN; break;
         case 6: v = j&1 ? -ldexpf(1.f, -149) : ldexpf(1.f, -149); break;
         case 7: v = j%3 == 0 ? NAN : (j%3 == 1 ? INFINITY : -INFINITY); break;
         case 8: v = j == N-1 ? NAN : 0.f; break;
         case 9: v = INFINITY; break;
         case 10: v = -INFINITY; break;
         case 11:
         case 12:
            v = ((int)(next_random() & 0xffffU) - 32768)/32768.f;
            break;
         case 13: v = j&1 ? FLT_MAX : -FLT_MAX; break;
         default:
         {
            int threshold = (pattern-14)/4;
            int variant = (pattern-14)%4;
            float center;
            /* Last six patterns straddle the half-band count that controls
               the integer majority comparison, including odd-length tails. */
            if (threshold >= 3)
            {
               int boundary = pattern-26;
               int low_count = N/2 + (boundary&1);
               center = sqrtf(thresholds[boundary/2]/N);
               v = nextafterf(center, j < low_count ? 0.f : INFINITY);
            } else {
               center = sqrtf(thresholds[threshold]/N);
               if (variant == 0) v = nextafterf(center, 0.f);
               else if (variant == 1) v = center;
               else if (variant == 2) v = nextafterf(center, INFINITY);
               else v = j%3 == 0 ? nextafterf(center, 0.f) :
                     (j%3 == 1 ? center : nextafterf(center, INFINITY));
            }
            if (j&1) v = -v;
            break;
         }
      }
      x[j] = v;
      if (pattern == 11) energy += (double)v*v;
   }
   if (pattern == 11 && energy > 0)
      for (j = 0; j < N; j++) x[j] = (float)(x[j]/sqrt(energy));
}

static void fill_input(float *x, const CELTMode *m, int C, int M,
      int end, int pattern)
{
   int i, c;
   /* Prefixes, gaps and the unused top of standard modes must stay ignored. */
   for (i = 0; i < C*M*m->shortMdctSize; i++) x[i] = FLT_MAX;
   for (c = 0; c < C; c++)
      for (i = 0; i < end; i++)
         fill_band(x + c*M*m->shortMdctSize + M*m->eBands[i],
               M*(m->eBands[i+1]-m->eBands[i]), pattern);
}

static void check_statistics(const CELTMode *m, const float *x, int C, int M,
      int end, int update_hf, const int *weights)
{
   Statistics scalar = {-991, -992, -993};
   Statistics neon = {-994, -995, -996};
   Statistics original = {-997, -998, -999};
   spreading_statistics_c(m, x, update_hf, end, C, M, weights,
         &scalar.sum, &scalar.nb_bands, &scalar.hf_sum);
   spreading_statistics_neon(m, x, update_hf, end, C, M, weights,
         &neon.sum, &neon.nb_bands, &neon.hf_sum);
   spreading_statistics_original(m, x, update_hf, end, C, M, weights,
         &original.sum, &original.nb_bands, &original.hf_sum);
   if (scalar.sum != neon.sum || scalar.nb_bands != neon.nb_bands ||
         scalar.hf_sum != neon.hf_sum || scalar.sum != original.sum ||
         scalar.nb_bands != original.nb_bands || scalar.hf_sum != original.hf_sum)
   {
      fprintf(stderr, "aggregates C=%d/%d/%d NEON=%d/%d/%d original=%d/%d/%d update_hf=%d\n",
            scalar.sum, scalar.nb_bands, scalar.hf_sum,
            neon.sum, neon.nb_bands, neon.hf_sum,
            original.sum, original.nb_bands, original.hf_sum, update_hf);
      fail("integer scan aggregates differ");
   }
}

static void check_decision(const CELTMode *m, const float *x, int C, int M,
      int end, int update_hf, const int *weights, History history[3])
{
   int path;
   for (path = 0; path < 3; path++)
   {
      History *h = &history[path];
      if (path == 2)
         h->last = spreading_decision_original(m, x, &h->average, h->last,
               &h->hf_average, &h->tapset, update_hf, end, C, M, weights);
      else
         /* arch=0 exercises scalar dispatch only in a non-presumed build.
            Presumed-NEON builds intentionally select NEON for both values. */
         h->last = spreading_decision(m, x, &h->average, h->last,
               &h->hf_average, &h->tapset, update_hf, end, C, M, weights,
               path == 0 ? 0 : runtime_arch);
   }
   for (path = 0; path < 2; path++)
   {
      const History *h = &history[path];
      const History *r = &history[2];
      if (h->last != r->last || h->average != r->average ||
            h->hf_average != r->hf_average || h->tapset != r->tapset)
      {
         fprintf(stderr, "decision/state path=%d: %d/%d/%d/%d original=%d/%d/%d/%d update_hf=%d\n",
               path, h->last, h->average, h->hf_average, h->tapset,
               r->last, r->average, r->hf_average, r->tapset, update_hf);
         fail("full decision or history differs from frozen original");
      }
   }
}

static void run_sequence(const CELTMode *m, int C, int M, int end,
      int weight_scheme, int seed)
{
   static const int averages[8] = {0, 79, 80, 255, 256, 383, 384, 768};
   static const int hf_averages[6] = {0, 18, 19, 22, 23, 64};
   GuardedInput input = guarded_alloc(C*M*m->shortMdctSize);
   float *snapshot = (float *)malloc((size_t)input.length*sizeof(float));
   History histories[2][3];
   int weights[21];
   int i, path, update_hf, pattern;
   context_N = M*(m->eBands[end]-m->eBands[end-1]);
   context_C = C;
   context_M = M;
   context_end = end;
   if (!snapshot) fail("snapshot allocation");
   for (i = 0; i < end; i++)
   {
      if (weight_scheme == 0) weights[i] = 1;
      else if (weight_scheme == 1) weights[i] = 32;
      else if (weight_scheme == 2) weights[i] = 1 + (i*7+seed)%32;
      else weights[i] = i == end-1 ? 1+seed%32 : 0;
   }
   for (update_hf = 0; update_hf <= 1; update_hf++)
      for (path = 0; path < 3; path++)
      {
         History *h = &histories[update_hf][path];
         h->average = averages[seed%8];
         h->hf_average = hf_averages[seed%6];
         h->tapset = seed%3;
         h->last = seed%4;
      }
   for (pattern = 0; pattern < PATTERNS; pattern++)
   {
      context_pattern = pattern;
      fill_input(input.x, m, C, M, end, pattern);
      memcpy(snapshot, input.x, (size_t)input.length*sizeof(float));
      for (update_hf = 0; update_hf <= 1; update_hf++)
      {
         check_statistics(m, input.x, C, M, end, update_hf, weights);
         check_decision(m, input.x, C, M, end, update_hf, weights,
               histories[update_hf]);
         cases_run++;
      }
      if (memcmp(snapshot, input.x, (size_t)input.length*sizeof(float)))
         fail("scan or decision modified const input");
   }
   free(snapshot);
   guarded_free(&input);
}

static void single_band_modes(void)
{
   int N, C, prefix;
   for (N = 1; N <= 209; N++)
      for (C = 1; C <= 2; C++)
         for (prefix = 0; prefix < 4; prefix++)
         {
            CELTMode mode;
            opus_int16 bands[2];
            memset(&mode, 0, sizeof(mode));
            bands[0] = (opus_int16)prefix;
            bands[1] = (opus_int16)(prefix+N);
            mode.nbEBands = mode.effEBands = 1;
            mode.shortMdctSize = prefix+N;
            mode.eBands = bands;
            run_sequence(&mode, C, 1, 1, 2, N+prefix+C);
         }
}

static void standard_modes(void)
{
   static const opus_int16 bands[22] = {
      0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 14, 16, 20, 24, 28,
      34, 40, 48, 60, 78, 100
   };
   CELTMode mode;
   int C, M, end, scheme;
   memset(&mode, 0, sizeof(mode));
   mode.eBands = bands;
   mode.nbEBands = mode.effEBands = 21;
   mode.shortMdctSize = 120;
   for (M = 1; M <= 8; M *= 2)
      for (C = 1; C <= 2; C++)
         for (end = 1; end <= 21; end++)
            for (scheme = 0; scheme < 4; scheme++)
               run_sequence(&mode, C, M, end, scheme, end+M+C+scheme);
}

int main(void)
{
   runtime_arch = opus_select_arch();
#if defined(OPUS_HAVE_RTCD) && !defined(OPUS_ARM_PRESUME_NEON_INTR)
   if (runtime_arch < 3)
   {
      puts("SKIP: CPU does not support ARM NEON");
      return 77;
   }
#endif
   single_band_modes();
   standard_modes();
#if defined(__linux__)
   if (!guarded_sequences) fail("no guard-page allocation succeeded");
#endif
   printf("PASS: %lu spreading cases; C/NEON/original aggregates and dispatched/original decisions/histories exact; %lu guard-page sequences\n",
         cases_run, guarded_sequences);
   return EXIT_SUCCESS;
}

#else
int main(void)
{
   puts("SKIP: floating-point ARM NEON build without FUZZING required");
   return 77;
}
#endif
