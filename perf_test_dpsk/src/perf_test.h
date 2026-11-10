/*
 * perf_test.h - 核心测试引擎
 *
 * 对每个 TestCase 执行：
 *   创建编码器/解码器 → 设置参数 → 预热 → 测量编码/解码/往返延迟
 *   → 解析包 TOC 验证实际编码模块（module probe）→ 统计输出
 */
#ifndef OPUS_PERF_TEST_H
#define OPUS_PERF_TEST_H

#include "config.h"
#include "stats.h"
#include "module_probe.h"
#include "env_ctrl.h"

/* 每用例收集的度量 */
typedef struct {
    char  name[128];
    char  description[256];

    /* 配置参数（用于 CSV/对比） */
    int    channels;
    int    frame_size;
    int    bitrate;
    int    application;
    int    complexity;
    int    loss_perc;

    /* 声明 vs 验证的模块 */
    ExpectedModule declared_module;   /* 用例声明 */
    RouteModule    actual_route;      /* 采样得到的实际路由（多数派） */
    RouteModule    actual_route_minor;/* 次要路由（模式切换时存在） */
    int            dnn_analysis_active;
    int            dnn_dred_attempted;
    int            route_counts[4];   /* CELT/HYBRID/SILK/UNKNOWN 帧计数 */

    /* 单次开销 */
    double create_enc_us, create_dec_us;
    double destroy_enc_us, destroy_dec_us;

    /* 延迟统计 */
    StatsData enc_us, dec_us, rt_us;

    /* 吞吐 */
    double enc_throughput_x, dec_throughput_x, rt_throughput_x;

    /* 码流 */
    double avg_bytes_per_frame;
    double actual_bitrate_kbps;
    int    frames_encoded, frames_decoded;

    /* 固定长度块模式：>0 表示 enc_us/dec_us/rt_us 为"每块(含 chunk_frames 帧)"耗时 */
    int chunk_frames;

    /* 结果 */
    int    passed;      /* 模块命中且无错误 */
    int    error_count;
    char   error_msg[256];
} PerfResult;

typedef struct {
    EnvState *env;
    unsigned int seed;
    double noise_db;
    int verbose;
} RunOptions;

/*
 * 运行单个测试用例。
 * @return 0 成功；非 0 用例级失败（结果中记录）。
 */
void perf_run_case(const TestCase *tc, const RunOptions *opt, PerfResult *res);

#endif /* OPUS_PERF_TEST_H */
