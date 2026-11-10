/*
 * reporter.h - 报告输出：终端文本 + JSON + CSV
 */
#ifndef OPUS_PERF_REPORTER_H
#define OPUS_PERF_REPORTER_H

#include "perf_test.h"
#include "config.h"
#include "env_ctrl.h"

typedef struct {
    TestConfig   *cfg;
    PerfResult   *results;
    int           result_count;
    EnvState     *env;
    const char   *lib_name;
    const char   *lib_path;
    const char   *config_file;
    const char   *output_dir;
    unsigned int  seed;
    double        noise_db;
    int           cpu_core;
    int           emit_text;
    int           emit_json;
    int           emit_csv;
} ReportContext;

/* 依赖 pscp/本机 JSON 序列化输出成三路 */
void reporter_run(ReportContext *ctx);

#endif /* OPUS_PERF_REPORTER_H */
