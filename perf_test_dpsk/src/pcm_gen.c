/*
 * pcm_gen.c - 确定性合成 PCM 生成器实现
 */
#include "pcm_gen.h"
#include "utils.h"
#include <math.h>

#define GEN_SAMPLE_RATE 48000
#define GEN_GAIN        0.5

/*
 * 多频正弦叠加：
 *   freq(kHz): 0.22 0.44 0.88 1.76 3.52   → 覆盖窄带→全频带
 *   amp:       0.15 0.30 0.20 0.10 0.05
 */
static const double GEN_FREQ[5] = { 220.0, 440.0, 880.0, 1760.0, 3520.0 };
static const double GEN_AMP[5]  = { 0.30, 0.45, 0.30, 0.15, 0.08 };

/* 简单 xorshift 伪随机数，用于噪声注入 */
static unsigned int rnd_next(unsigned int *s)
{
    *s ^= *s << 13;
    *s ^= *s >> 17;
    *s ^= *s << 5;
    return *s;
}

void pcm_gen_init(PcmGen *g, unsigned int seed, double noise_db)
{
    g->seed = seed ? seed : 42;
    g->noise_db = noise_db > 0.0 ? noise_db : 0.0;
    g->sample_offset = 0;
}

void pcm_gen_fill(PcmGen *g, opus_int16 *pcm, int frame_size, int channels)
{
    double noise_amp = 0.0;
    if (g->noise_db > 0.0)
        noise_amp = pow(10.0, -g->noise_db / 20.0) * 32767.0 * 0.5;

    unsigned int s = g->seed;
    for (int i = 0; i < frame_size; i++) {
        double t = (double)(g->sample_offset + i) / (double)GEN_SAMPLE_RATE;
        double val = 0.0;
        for (int k = 0; k < 5; k++)
            val += GEN_AMP[k] * sin(2.0 * M_PI * GEN_FREQ[k] * t);
        val *= GEN_GAIN;

        if (noise_amp > 0.0) {
            double n = ((double)(rnd_next(&s) % 1000000) / 500000.0) - 1.0;
            val += n * noise_amp / 32767.0;
        }

        opus_int16 sample = (opus_int16)(val * 32767.0);
        for (int c = 0; c < channels; c++)
            pcm[i * channels + c] = sample;
    }
    g->sample_offset += frame_size;
}
