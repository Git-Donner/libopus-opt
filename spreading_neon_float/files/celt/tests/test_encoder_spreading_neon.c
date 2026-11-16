/* Public API regression test for the floating-point NEON spreading scan.
   Link with --wrap=spreading_statistics_neon and test_spreading_reference.c.
   The paired encoders differ only in the implementation of that scan. */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "opus.h"
#include "bands.h"
#include "cpu_support.h"
#include "test_spreading_reference.h"

#if !defined(FIXED_POINT) && defined(OPUS_ARM_MAY_HAVE_NEON_INTR) && !defined(FUZZING)

static int reference_mode;
static unsigned long neon_calls, original_calls;
static unsigned long neon_coefficients, original_coefficients;

void __real_spreading_statistics_neon(const CELTMode *m, const celt_norm *X,
      int update_hf, int end, int C, int M, const int *spread_weight,
      int *sum, int *nb_bands, int *hf_sum);

void __wrap_spreading_statistics_neon(const CELTMode *m, const celt_norm *X,
      int update_hf, int end, int C, int M, const int *spread_weight,
      int *sum, int *nb_bands, int *hf_sum)
{
   unsigned long coefficients = 0;
   int i;
   for (i = 0; i < end; i++) {
      int N = M*(m->eBands[i+1] - m->eBands[i]);
      if (N > 8) coefficients += (unsigned long)N*C;
   }
   if (reference_mode) {
      original_calls++;
      original_coefficients += coefficients;
      spreading_statistics_original(m, X, update_hf, end, C, M, spread_weight,
                                    sum, nb_bands, hf_sum);
   } else {
      neon_calls++;
      neon_coefficients += coefficients;
      __real_spreading_statistics_neon(m, X, update_hf, end, C, M,
                                      spread_weight, sum, nb_bands, hf_sum);
   }
}

static void fail(const char *message, int channels, int frame_size, int frame)
{
   fprintf(stderr, "FAIL: %s (channels=%d frame_size=%d frame=%d)\n",
           message, channels, frame_size, frame);
   exit(EXIT_FAILURE);
}

int main(void)
{
   static const int frame_sizes[] = {120, 480, 960};
   opus_uint32 seed = 42;
   int channels, scenario;
#if defined(OPUS_HAVE_RTCD)
   if (opus_select_arch() < OPUS_ARCH_ARM_NEON) {
      puts("SKIP: CPU does not support NEON");
      return 77;
   }
#endif
   for (channels = 1; channels <= 2; channels++) {
      for (scenario = 0; scenario < 3; scenario++) {
         OpusEncoder *enc[2];
         OpusDecoder *dec[2];
         opus_int16 pcm[1920], decoded[2][1920];
         unsigned char packet[2][1500];
         unsigned long before_neon = neon_calls;
         unsigned long before_original = original_calls;
         int frame_size = frame_sizes[scenario];
         int i, frame;
         for (i = 0; i < 2; i++) {
            int error;
            enc[i] = opus_encoder_create(48000, channels,
                                        OPUS_APPLICATION_RESTRICTED_CELT, &error);
            if (!enc[i] || error != OPUS_OK)
               fail("encoder creation", channels, frame_size, -1);
            dec[i] = opus_decoder_create(48000, channels, &error);
            if (!dec[i] || error != OPUS_OK)
               fail("decoder creation", channels, frame_size, -1);
            if (opus_encoder_ctl(enc[i], OPUS_SET_BITRATE(64000*channels)) != OPUS_OK ||
                opus_encoder_ctl(enc[i], OPUS_SET_COMPLEXITY(5)) != OPUS_OK)
               fail("encoder configuration", channels, frame_size, -1);
         }
         for (frame = 0; frame < 50; frame++) {
            opus_uint32 encoder_range[2], decoder_range[2];
            int lengths[2], j;
            for (j = 0; j < frame_size*channels; j++) {
               seed = 1664525U*seed + 1013904223U;
               pcm[j] = (opus_int16)((int)(seed >> 16)/4 - 8192);
            }
            for (i = 0; i < 2; i++) {
               reference_mode = i;
               lengths[i] = opus_encode(enc[i], pcm, frame_size, packet[i],
                                        (opus_int32)sizeof(packet[i]));
               if (lengths[i] <= 0 || !(packet[i][0] & 0x80))
                  fail("encode did not produce a CELT packet", channels, frame_size, frame);
               if (opus_decode(dec[i], packet[i], lengths[i], decoded[i],
                               frame_size, 0) != frame_size)
                  fail("decode", channels, frame_size, frame);
               if (opus_encoder_ctl(enc[i], OPUS_GET_FINAL_RANGE(&encoder_range[i])) != OPUS_OK ||
                   opus_decoder_ctl(dec[i], OPUS_GET_FINAL_RANGE(&decoder_range[i])) != OPUS_OK ||
                   encoder_range[i] != decoder_range[i])
                  fail("encoder/decoder final range mismatch", channels, frame_size, frame);
            }
            if (lengths[0] != lengths[1] ||
                memcmp(packet[0], packet[1], (size_t)lengths[0]) != 0 ||
                memcmp(decoded[0], decoded[1], (size_t)frame_size*channels*sizeof(opus_int16)) != 0 ||
                encoder_range[0] != encoder_range[1])
               fail("NEON/original packet, PCM or final range mismatch", channels, frame_size, frame);
         }
         if (neon_calls == before_neon || original_calls == before_original)
            fail("spreading scan was not reached by both encoders", channels, frame_size, 50);
         for (i = 0; i < 2; i++) {
            opus_encoder_destroy(enc[i]);
            opus_decoder_destroy(dec[i]);
         }
      }
   }
   if (!neon_coefficients || !original_coefficients || neon_calls != original_calls ||
       neon_coefficients != original_coefficients)
      fail("scan call/work counters differ or contain no work", 0, 0, 0);
   printf("PASS: 300 paired CELT frames, mono/stereo, 2.5/10/20 ms; "
          "packets, PCM and final ranges exactly match original scan; "
          "NEON calls=%lu coefficients=%lu original calls=%lu coefficients=%lu\n",
          neon_calls, neon_coefficients, original_calls, original_coefficients);
   return EXIT_SUCCESS;
}

#else

int main(void)
{
   puts("SKIP: requires floating-point NEON without FUZZING");
   return 77;
}

#endif
