/*
 * stats.c - 统计计算实现
 */
#include "stats.h"
#include "utils.h"

#include <math.h>

static int cmp_double(const void *a, const void *b)
{
    double da = *(const double *)a, db = *(const double *)b;
    return (da > db) - (da < db);
}

void stats_collector_init(StatsCollector *c, int capacity)
{
    c->data     = (double *)xmalloc(sizeof(double) * (size_t)capacity);
    c->count    = 0;
    c->capacity = capacity;
    c->sum      = 0.0;
    c->min      = 1e300;
    c->max      = -1e300;
}

void stats_collector_add(StatsCollector *c, double val)
{
    if (c->count >= c->capacity)
        return;
    c->data[c->count++] = val;
    c->sum += val;
    if (val < c->min) c->min = val;
    if (val > c->max) c->max = val;
}

static double percentile(double *sorted, int n, double p)
{
    if (n <= 0) return 0.0;
    double idx = p * (double)(n - 1);
    int lo = (int)idx;
    int hi = lo + 1 < n ? lo + 1 : lo;
    double frac = idx - (double)lo;
    return sorted[lo] * (1.0 - frac) + sorted[hi] * frac;
}

void stats_finalize(StatsCollector *c, StatsData *out)
{
    int n = c->count;
    out->count = n;
    if (n == 0) {
        out->min = out->max = out->avg = out->p50 = out->p95 = out->p99 = out->stddev = 0.0;
        return;
    }
    out->min = c->min;
    out->max = c->max;
    out->avg = c->sum / (double)n;

    qsort(c->data, (size_t)n, sizeof(double), cmp_double);
    out->p50 = percentile(c->data, n, 0.50);
    out->p95 = percentile(c->data, n, 0.95);
    out->p99 = percentile(c->data, n, 0.99);

    double var = 0.0;
    for (int i = 0; i < n; i++) {
        double d = c->data[i] - out->avg;
        var += d * d;
    }
    out->stddev = sqrt(var / (double)n);
}

void stats_collector_free(StatsCollector *c)
{
    if (c->data) free(c->data);
    c->data = NULL;
    c->count = 0;
}
