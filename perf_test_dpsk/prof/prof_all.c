/*
 * prof_all.c - 四场景函数占比采样程序
 *
 * 用法: ./prof_all <mode> <phase> [iters]
 *   mode:  celt | silk
 *   phase: encode | decode
 *   iters: 迭代次数 (默认: encode=500000, decode=800000)
 *
 * 设计要点:
 *   1. 编码阶段：真实 PCM→opus_encode() 循环，避免数据伪造
 *   2. 解码阶段：先编码一批真实包，再 opus_decode() 循环
 *   3. 确定性种子保证每次运行采样一致
 *   4. 单线程纯计算，适合 perf record 采样
 *
 * perf 用法:
 *   perf record -g -e cycles:u -o /tmp/<tag>.data -- ./prof_all <mode> <phase>
 *   perf report -i /tmp/<tag>.data --stdio -g none --sort symbol
 */
#include <opus.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#define FS       48000
#define FRAME    960        /* 20ms @48k */
#define MAXPKT   4000
#define MAXFR    2880
#define NBUF     400         /* 编码出的不同帧包数量 */

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <celt|silk> <encode|decode> [iters]\n", argv[0]);
        return 1;
    }
    const char *mode  = argv[1];
    const char *phase = argv[2];

    int is_celt = (strcmp(mode, "celt") == 0);
    int is_silk = (strcmp(mode, "silk") == 0);
    if (!is_celt && !is_silk) {
        fprintf(stderr, "mode must be 'celt' or 'silk'\n");
        return 1;
    }

    int is_enc = (strcmp(phase, "encode") == 0);
    int is_dec = (strcmp(phase, "decode") == 0);
    if (!is_enc && !is_dec) {
        fprintf(stderr, "phase must be 'encode' or 'decode'\n");
        return 1;
    }

    int iters = argc > 3 ? atoi(argv[3]) : (is_enc ? 500000 : 800000);
    int ch = 1;
    int app = is_celt ? OPUS_APPLICATION_RESTRICTED_CELT : OPUS_APPLICATION_RESTRICTED_SILK;
    int br  = is_celt ? 64000 : 16000;
    int cplx = 10;

    int err;
    OpusEncoder *enc = opus_encoder_create(FS, ch, app, &err);
    if (err != OPUS_OK || !enc) { fprintf(stderr, "enc create fail %d\n", err); return 1; }
    opus_encoder_ctl(enc, OPUS_SET_BITRATE(br));
    opus_encoder_ctl(enc, OPUS_SET_COMPLEXITY(cplx));
    opus_encoder_ctl(enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));

    OpusDecoder *dec = opus_decoder_create(FS, ch, &err);
    if (err != OPUS_OK || !dec) { fprintf(stderr, "dec create fail %d\n", err); return 1; }

    opus_int16 *pcm = malloc(sizeof(opus_int16) * FRAME * ch);
    opus_int16 *out = malloc(sizeof(opus_int16) * MAXFR * ch);
    unsigned char *pkts = malloc((size_t)MAXPKT * NBUF);
    int *plens = malloc(sizeof(int) * NBUF);

    /* 生成 NBUF 个确定性 PCM 帧并编码为真实码流 */
    uint32_t s = 42;
    for (int b = 0; b < NBUF; b++) {
        for (int i = 0; i < FRAME * ch; i++) {
            s = s * 1664525 + 1013904223u;
            pcm[i] = (opus_int16)((s >> 16) & 0xFFFFu);
        }
        int nb = opus_encode(enc, pcm, FRAME, pkts + (size_t)b * MAXPKT, MAXPKT);
        if (nb <= 0) { fprintf(stderr, "pre-encode fail b=%d err=%d\n", b, nb); return 1; }
        plens[b] = nb;
    }

    printf("mode=%s phase=%s ch=%d br=%d iters=%d\n", mode, phase, ch, br, iters);

    if (is_enc) {
        /* 编码测量: 循环 opus_encode() */
        long long total_bytes = 0;
        s = 42; /* 重播种子 */
        for (int i = 0; i < iters; i++) {
            for (int j = 0; j < FRAME * ch; j++) {
                s = s * 1664525 + 1013904223u;
                pcm[j] = (opus_int16)((s >> 16) & 0xFFFFu);
            }
            int nb = opus_encode(enc, pcm, FRAME, pkts, MAXPKT);
            if (nb <= 0) { fprintf(stderr, "encode fail i=%d err=%d\n", i, nb); return 1; }
            total_bytes += nb;
        }
        printf("total_bytes=%lld\n", total_bytes);
    } else {
        /* 解码测量: 循环 opus_decode() 已有的 NBUF 个包 */
        long long total_frames = 0;
        for (int i = 0; i < iters; i++) {
            int b = i % NBUF;
            int fs = opus_decode(dec, pkts + (size_t)b * MAXPKT, plens[b], out, MAXFR, 0);
            if (fs < 0) { fprintf(stderr, "decode fail i=%d err=%d\n", i, fs); return 1; }
            total_frames += fs;
        }
        printf("total_frames=%lld\n", total_frames);
    }

    return 0;
}
