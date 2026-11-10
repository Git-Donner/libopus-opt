/*
 * decode_prof.c - 解码热点分析专用（真实解码路径采样）
 *
 * 用法: ./decode_prof <mode> [bitrate] [iters]
 *   mode: celt | silk | voip   (voip=默认混合路由)
 *
 * 设计目标:
 *   1. 编码一小批(批次含 NBUF 个不同帧)真实码流包，非伪造数据
 *   2. 测量区只做 opus_decode()（纯解码路径），让采样集中在解码热点
 *   3. 循环解码同一批包，保证计时/采样充分、确定性强
 * 全部在用户态真实调用 opus API，覆盖端到端 decoder 全链路。
 */
#include <opus.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#define FS       48000
#define MAXPKT   4000
#define MAXFR    2880
#define NBUF     400   /* 编码出的不同帧包数量（批内不同，确定性循环） */

static double now_us(void){
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec*1e6 + (double)ts.tv_nsec/1e3;
}

int main(int argc, char**argv){
    const char* mode = argc>1?argv[1]:"celt";
    int br = argc>2?atoi(argv[2]):(strcmp(mode,"celt")==0 ? 64000 : (strcmp(mode,"silk")==0?16000:24000));
    int iters = argc>3?atoi(argv[3]):200000;
    int frame = 960;        /* 20ms @48k */
    int cplx = 10;
    int ch = 1;
    int app;
    if(strcmp(mode,"celt")==0)      app = OPUS_APPLICATION_RESTRICTED_CELT;
    else if(strcmp(mode,"silk")==0) app = OPUS_APPLICATION_RESTRICTED_SILK;
    else                            app = OPUS_APPLICATION_VOIP;

    int err;
    OpusEncoder *enc = opus_encoder_create(FS, ch, app, &err);
    if(err!=OPUS_OK || !enc){ fprintf(stderr,"enc create fail %d\n",err); return 1; }
    opus_encoder_ctl(enc, OPUS_SET_BITRATE(br));
    opus_encoder_ctl(enc, OPUS_SET_COMPLEXITY(cplx));
    opus_encoder_ctl(enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));

    OpusDecoder *dec = opus_decoder_create(FS, ch, &err);
    if(err!=OPUS_OK || !dec){ fprintf(stderr,"dec create fail %d\n",err); return 1; }

    opus_int16 *pcm  = malloc(sizeof(opus_int16)*frame*ch);
    opus_int16 *out  = malloc(sizeof(opus_int16)*MAXFR*ch);
    unsigned char *pkts = malloc((size_t)MAXPKT*NBUF);
    int *plens = malloc(sizeof(int)*NBUF);

    /* 生成 NBUF 个不同的确定性 PCM 帧并编码为真实码流 */
    uint32_t s = 42;
    for(int b=0;b<NBUF;b++){
        for(int i=0;i<frame*ch;i++){ s=s*1664525+1013904223u; pcm[i]=(opus_int16)((s>>16)&0xFFFFu); }
        int nb = opus_encode(enc, pcm, frame, pkts+(size_t)b*MAXPKT, MAXPKT);
        if(nb<=0){ fprintf(stderr,"encode fail %d\n",nb); return 1; }
        plens[b] = nb;
    }

    /* 校验路由：统计 TOC 首字节 mode（沿用框架 module_probe 已校验启发式）
       data[0]&0x80 -> CELT; (data[0]&0x60)==0x60 -> HYBRID; else SILK */
    int celt_cnt=0, silk_cnt=0, hybrid_cnt=0;
    for(int b=0;b<NBUF;b++){
        unsigned char t = pkts[(size_t)b*MAXPKT];
        if(t & 0x80) celt_cnt++;
        else if((t & 0x60)==0x60) hybrid_cnt++;
        else silk_cnt++;
    }
    printf("routing: celt=%d silk=%d hybrid=%d (of %d)\n", celt_cnt, silk_cnt, hybrid_cnt, NBUF);

    /* 预热 */
    for(int i=0;i<2000;i++){ int b=i%NBUF; opus_decode(dec, pkts+(size_t)b*MAXPKT, plens[b], out, MAXFR, 0); }

    /* 测量区：纯解码（循环同一批包） */
    long long total=0;
    double t0=now_us();
    for(int i=0;i<iters;i++){
        int b=i%NBUF;
        int fs = opus_decode(dec, pkts+(size_t)b*MAXPKT, plens[b], out, MAXFR, 0);
        total += fs;
    }
    double t1=now_us();
    printf("mode=%s br=%d frames=%d time=%.3f ms  %.3f us/frame  sumdec=%lld\n",
        mode, br, iters, (t1-t0)/1e3, (t1-t0)/(double)iters, total);

    return total<=0 ? 1 : 0;
}
