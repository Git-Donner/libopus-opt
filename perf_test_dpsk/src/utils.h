/*
 * opus_perf_dpsk - Opus 端到端性能测试框架
 *
 * utils.h - 公共宏、常量与跨平台兼容层
 */
#ifndef OPUS_PERF_UTILS_H
#define OPUS_PERF_UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>

/* ─── 版本与错误码 ─────────────────────────────────────────────── */
#define FRAMEWORK_VERSION   "1.0.0"
#define FRAMEWORK_NAME      "opus_perf_dpsk"

#define PERF_OK             0
#define PERF_ERR           -1
#define PERF_WARN           1

/* ─── 公共常量 ─────────────────────────────────────────────────── */
#define SAMPLE_RATE          48000
#define MAX_FRAME_SIZE      (6*960)          /* 120ms @48kHz */
#define MAX_PACKET_SIZE     (3*1276)         /* 最大 opus 包 */
#define CACHE_LINE           64

/* ─── 计时辅助（纳秒级，单调时钟不受系统时间调整影响） ─────────── */
static inline double now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e6 + (double)ts.tv_nsec * 1e-3;
}

/* ─── 安全分配 ─────────────────────────────────────────────────── */
static inline void *xmalloc(size_t n)
{
    void *p = malloc(n);
    if (!p) {
        fprintf(stderr, "[FATAL] out of memory (%zu bytes)\n", n);
        exit(EXIT_FAILURE);
    }
    return p;
}

static inline void *xcalloc(size_t n, size_t sz)
{
    void *p = calloc(n, sz);
    if (!p) {
        fprintf(stderr, "[FATAL] out of memory\n");
        exit(EXIT_FAILURE);
    }
    return p;
}

/* ─── 字符串工具 ───────────────────────────────────────────────── */
static inline int str_ieq(const char *a, const char *b)
{
    while (*a && *b) {
        int ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == *b;
}

#endif /* OPUS_PERF_UTILS_H */
