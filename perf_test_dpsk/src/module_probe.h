/*
 * module_probe.h - 运行时模块探测
 *
 * 通过解析编码产生的 opus 包 TOC 字节，确定本帧实际使用的编码模块
 * （SILK / HYBRID / CELT），从而验证测试用例声明的 module 是否命中。
 * 同时根据编码器参数判断 DNN（分析神经网络 mlp.c）是否被激活。
 */
#ifndef OPUS_PERF_MODULE_PROBE_H
#define OPUS_PERF_MODULE_PROBE_H

#include "config.h"

/* 实际路由到的模块 */
typedef enum {
    ROUTE_CELT   = 0,
    ROUTE_HYBRID = 1,
    ROUTE_SILK   = 2,
    ROUTE_UNKNOWN = 3
} RouteModule;

/*
 * 解析单个 opus 包的 TOC，返回实际编码模块。
 * 复刻 opus_decoder.c 的 opus_packet_get_mode 逻辑：
 *   data[0]&0x80        → CELT
 *   (data[0]&0x60)==0x60 → HYBRID
 *   否则                 → SILK
 */
RouteModule probe_packet_mode(const unsigned char *data, int len);

/*
 * 判断编码器配置是否会激活 DNN 分析神经网络（mlp.c）：
 *   运行 run_analysis 的条件：
 *     complexity >= 7 (float) 或 >=10 (fixed)
 *     && Fs in [16000, 48000]
 *     && application != OPUS_APPLICATION_RESTRICTED_SILK
 */
int probe_dnn_analysis_active(int application, int complexity, int sample_rate);

/*
 * 判断 DNN 冗余编码 (DRED) 是否可被激活（需要库启用 ENABLE_DRED 且设置 duration）。
 */
int probe_dnn_dred_configurable(int dred_duration);

const char *route_label(RouteModule r);

#endif /* OPUS_PERF_MODULE_PROBE_H */
