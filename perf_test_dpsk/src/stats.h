/*
 * stats.h - 统计计算接口：min/max/avg/p50/p95/p99/stddev
 */
#ifndef OPUS_PERF_STATS_H
#define OPUS_PERF_STATS_H

typedef struct {
    double  min;
    double  max;
    double  avg;
    double  p50;
    double  p95;
    double  p99;
    double  stddev;
    int     count;
} StatsData;

/* 收集器：增量收集样本 */
typedef struct {
    double *data;
    int     count;
    int     capacity;
    double  sum;
    double  min;
    double  max;
} StatsCollector;

void stats_collector_init(StatsCollector *c, int capacity);
void stats_collector_add(StatsCollector *c, double val);
/* 计算最终统计（内部会排序样本以取分位数） */
void stats_finalize(StatsCollector *c, StatsData *out);
void stats_collector_free(StatsCollector *c);

#endif /* OPUS_PERF_STATS_H */
