/* Copyright (c) 2013 Jean-Marc Valin */
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

/* This is meant to be a simple example of CELT-only audio encoding
   using Opus. Encoded packets are discarded; no decoder is created.
   It should make it easy to understand how the Opus API
   works. For more information, see the full API documentation at:
   https://www.opus-codec.org/docs/ */

#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <opus.h>
#include <stdio.h>

/*The frame size is hardcoded for this sample code but it doesn't have to be*/
#define FRAME_SIZE 960
#define SAMPLE_RATE 48000
#define CHANNELS 1
#define APPLICATION OPUS_APPLICATION_RESTRICTED_CELT
#define BITRATE 64000
#define COMPLEXITY 5
#define BANDWIDTH OPUS_BANDWIDTH_FULLBAND
#define FRAMES_PER_LOOP 1100
#define INPUT_SAMPLES (FRAMES_PER_LOOP*FRAME_SIZE*CHANNELS)

#define MAX_PACKET_SIZE (3*1276)

int main(int argc, char **argv)
{
   FILE *fin = NULL;
   /* Fixed-size static buffers avoid a large stack allocation. */
   static opus_int16 in[INPUT_SAMPLES];
   static unsigned char pcm_bytes[INPUT_SAMPLES*2];
   unsigned char cbits[MAX_PACKET_SIZE];
   int nbBytes;
   OpusEncoder *encoder = NULL;
   unsigned long encoded_frames = 0;
   unsigned long loops = 1;
   int status = EXIT_FAILURE;
   int err;

   if (argc != 2 && argc != 3)
   {
      fprintf(stderr, "usage: %s input.pcm [loops]\n", argv[0]);
      fprintf(stderr, "input: 48000 Hz, mono, signed 16-bit little-endian raw PCM\n");
      return EXIT_FAILURE;
   }
   if (argc == 3)
   {
      char *end;
      errno = 0;
      loops = strtoul(argv[2], &end, 10);
      if (argv[2][0]<'0' || argv[2][0]>'9' || *end!='\0' ||
          errno!=0 || loops==0 || loops>ULONG_MAX/FRAMES_PER_LOOP)
      {
         fprintf(stderr, "loops must be an integer from 1 to %lu\n",
                 ULONG_MAX/FRAMES_PER_LOOP);
         return EXIT_FAILURE;
      }
   }

   fin = fopen(argv[1], "rb");
   if (fin==NULL)
   {
      fprintf(stderr, "failed to open input file: %s\n", strerror(errno));
      goto cleanup;
   }

   /* Read and convert the entire case before entering the encoding loop. */
   if (fread(pcm_bytes, 1, sizeof(pcm_bytes), fin) != sizeof(pcm_bytes))
   {
      fprintf(stderr, "input is too short or unreadable; need %d bytes (%d frames)\n",
              INPUT_SAMPLES*2, FRAMES_PER_LOOP);
      goto cleanup;
   }
   for (int i=0;i<INPUT_SAMPLES;i++)
   {
      int sample = pcm_bytes[2*i+1]<<8 | pcm_bytes[2*i];
      in[i] = (opus_int16)((sample ^ 0x8000) - 0x8000);
   }

   /*Create a new encoder state */
   encoder = opus_encoder_create(SAMPLE_RATE, CHANNELS, APPLICATION, &err);
   if (encoder==NULL || err<0)
   {
      fprintf(stderr, "failed to create an encoder: %s\n", opus_strerror(err));
      goto cleanup;
   }
   /* Match the celt-encode case. These checks run once, before encoding. */
   if ((err = opus_encoder_ctl(encoder, OPUS_SET_BITRATE(BITRATE))) != OPUS_OK ||
       (err = opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(COMPLEXITY))) != OPUS_OK ||
       (err = opus_encoder_ctl(encoder, OPUS_SET_FORCE_CHANNELS(CHANNELS))) != OPUS_OK ||
       (err = opus_encoder_ctl(encoder, OPUS_SET_BANDWIDTH(BANDWIDTH))) != OPUS_OK ||
       (err = opus_encoder_ctl(encoder, OPUS_SET_MAX_BANDWIDTH(BANDWIDTH))) != OPUS_OK ||
       (err = opus_encoder_ctl(encoder, OPUS_SET_VBR(0))) != OPUS_OK ||
       (err = opus_encoder_ctl(encoder, OPUS_SET_DTX(0))) != OPUS_OK ||
       (err = opus_encoder_ctl(encoder, OPUS_SET_INBAND_FEC(0))) != OPUS_OK)
   {
      fprintf(stderr, "failed to configure encoder: %s\n", opus_strerror(err));
      goto cleanup;
   }

   /* Repeat the same PCM with one continuous encoder state; no warmup.
      The hot loop does no I/O, conversion, timing, hashing or decoding. */
   for (unsigned long loop=0;loop<loops;loop++)
   {
      for (int frame=0;frame<FRAMES_PER_LOOP;frame++)
      {
         nbBytes = opus_encode(encoder, in + frame*FRAME_SIZE*CHANNELS,
                              FRAME_SIZE, cbits, MAX_PACKET_SIZE);
         if (nbBytes<0)
         {
            fprintf(stderr, "encode failed: %s\n", opus_strerror(nbBytes));
            goto cleanup;
         }
      }
   }
   encoded_frames = loops*FRAMES_PER_LOOP;
   fprintf(stderr, "Encoded %lu CELT frames (%lu loops x %d frames; no warmup).\n",
           encoded_frames, loops, FRAMES_PER_LOOP);
   status = EXIT_SUCCESS;

cleanup:
   if (encoder!=NULL)
      opus_encoder_destroy(encoder);
   if (fin!=NULL && fclose(fin)!=0)
   {
      fprintf(stderr, "failed to close input file: %s\n", strerror(errno));
      status = EXIT_FAILURE;
   }
   return status;
}
