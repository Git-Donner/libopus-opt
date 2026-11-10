/*
 * pcm_gen.h - 确定性合成 PCM 生成器
 *
 * 生成的信号多频正弦叠加 + 可选白噪声，seed 固定保证字节级可复现。
 */
#ifndef OPUS_PERF_PCM_GEN_H
#define OPUS_PERF_PCM_GEN_H

#include "opus_types.h"

typedef struct {
    unsigned int seed;      /* 随机种子 */
    double       noise_db;  /* 白噪声电平 (dB)，0=无噪声 */
    long long    sample_offset; /* 全局样点偏移，保证帧间连续 */
} PcmGen;

/* 初始化（若 noise_db<=0 则无噪声） */
void pcm_gen_init(PcmGen *g, unsigned int seed, double noise_db);
/*
 * 生成一帧。channels 交错的 int16 PCM。
 * frame_size 为每声道样点数。
 */
void pcm_gen_fill(PcmGen *g, opus_int16 *pcm, int frame_size, int channels);

#endif /* OPUS_PERF_PCM_GEN_H */
