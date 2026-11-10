/*
 * reporter.c - 报告输出实现（终端文本 + JSON + CSV）
 */
#include "reporter.h"
#include "utils.h"

#include <time.h>
#include <sys/stat.h>
#include <errno.h>

static void timestamp_now(char *buf, size_t sz, int compact)
{
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    if (compact)
        snprintf(buf, sz, "%04d%02d%02d_%02d%02d%02d",
                 tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                 tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    else
        snprintf(buf, sz, "%04d-%02d-%02d %02d:%02d:%02d",
                 tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                 tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
}

static void mkdir_p(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) return;

    /* 逐级创建中间目录 */
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    if (len > 0 && tmp[len - 1] == '/') tmp[len - 1] = '\0';
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (stat(tmp, &st) != 0)
                mkdir(tmp, 0755);
            *p = '/';
        }
    }
    if (stat(tmp, &st) != 0)
        mkdir(tmp, 0755);
}

/* ─── 终端报告 ─────────────────────────────────────────────────── */
static void print_stats_line(const char *tag, const StatsData *s)
{
    printf("  %-18s min=%-9.2f avg=%-9.2f max=%-9.2f p50=%-9.2f p95=%-9.2f p99=%-9.2f  (us)\n",
           tag, s->min, s->avg, s->max, s->p50, s->p95, s->p99);
}

static void report_text(ReportContext *ctx)
{
    char ts[64];
    timestamp_now(ts, sizeof(ts), 0);
    printf("\n");
    printf("============================================================\n");
    printf("  %s  v%s\n", FRAMEWORK_NAME, FRAMEWORK_VERSION);
    printf("  Lib    : %s\n", ctx->lib_name);
    printf("  Config : %s\n", ctx->config_file);
    printf("  Time   : %s\n", ts);
    printf("============================================================\n");
    env_print(ctx->env);

    int pass = 0;
    for (int i = 0; i < ctx->result_count; i++) {
        const PerfResult *r = &ctx->results[i];
        if (r->passed) pass++;
        printf("\n------------------------------------------------------------\n");
        printf(" [%s] %s  (module=%s -> route=%s%s%s)\n",
               r->passed ? "PASS" : "FAIL", r->name,
               module_label(r->declared_module), route_label(r->actual_route),
               r->actual_route_minor != ROUTE_UNKNOWN ? "/" : "",
               r->actual_route_minor != ROUTE_UNKNOWN ? route_label(r->actual_route_minor) : "");
        printf("   desc : %s\n", r->description);
        if (r->chunk_frames > 0)
            printf("   mode : 固定长度块(每块 %d 帧)，min/p50/p95 为每块一次处理耗时\n", r->chunk_frames);
        if (r->dnn_analysis_active)
            printf("   dnn  : analysis(mlp.c) ACTIVE\n");
        if (r->dnn_dred_attempted)
            printf("   dnn  : DRED requested (needs ENABLE_DRED build)\n");

        printf("   create_enc=%.2fus create_dec=%.2fus destroy_enc=%.2fus destroy_dec=%.2fus\n",
               r->create_enc_us, r->create_dec_us, r->destroy_enc_us, r->destroy_dec_us);

        printf("   -- opus_encode (%d frames, samples=%d) --\n", r->frames_encoded, (int)r->enc_us.count);
        print_stats_line("latency", &r->enc_us);
        printf("   throughput : %.1f x realtime\n", r->enc_throughput_x);

        if (r->frames_decoded > 0) {
            printf("   -- opus_decode (%d frames) --\n", r->frames_decoded);
            print_stats_line("latency", &r->dec_us);
            printf("   throughput : %.1f x realtime\n", r->dec_throughput_x);
        }
        printf("   -- roundtrip --\n");
        print_stats_line("latency", &r->rt_us);
        printf("   throughput : %.1f x realtime\n", r->rt_throughput_x);
        printf("   bitstream  : avg %.1f bytes/frame, %.1f kbps actual\n",
               r->avg_bytes_per_frame, r->actual_bitrate_kbps);
        if (r->error_msg[0])
            printf("   ERROR      : %s\n", r->error_msg);
    }
    printf("\n============================================================\n");
    printf("  SUMMARY : %d/%d test cases passed\n", pass, ctx->result_count);
    printf("============================================================\n");
}

/* ─── JSON 报告 ────────────────────────────────────────────────── */
static void report_json(ReportContext *ctx)
{
    char ts[64];
    timestamp_now(ts, sizeof(ts), 1);

    char dir[512];
    snprintf(dir, sizeof(dir), "%s/json", ctx->output_dir);
    mkdir_p(dir);
    char fname[1024];
    snprintf(fname, sizeof(fname), "%s/%s_%s_%s.json", dir, ctx->lib_name,
             ctx->cfg->config_name, ts);
    FILE *f = fopen(fname, "w");
    if (!f) { fprintf(stderr, "[report] 无法写 JSON: %s\n", fname); return; }

    fprintf(f, "{\n");
    fprintf(f, "  \"meta\": {\n");
    fprintf(f, "    \"framework\": \"%s v%s\",\n", FRAMEWORK_NAME, FRAMEWORK_VERSION);
    fprintf(f, "    \"timestamp\": \"%s\",\n", ts);
    fprintf(f, "    \"lib_name\": \"%s\",\n", ctx->lib_name);
    fprintf(f, "    \"lib_path\": \"%s\",\n", ctx->lib_path);
    fprintf(f, "    \"cpu_model\": \"%s\",\n", ctx->env->cpu_model);
    fprintf(f, "    \"cpu_cores\": %d,\n", ctx->env->nprocs);
    fprintf(f, "    \"cpu_core\": %d,\n", ctx->cpu_core);
    fprintf(f, "    \"seed\": %u,\n", ctx->seed);
    fprintf(f, "    \"noise_db\": %.2f\n", ctx->noise_db);
    fprintf(f, "  },\n");
    fprintf(f, "  \"results\": [\n");
    for (int i = 0; i < ctx->result_count; i++) {
        const PerfResult *r = &ctx->results[i];
        fprintf(f, "    {\n");
        fprintf(f, "      \"name\": \"%s\",\n", r->name);
        fprintf(f, "      \"module_declared\": \"%s\",\n", module_machine_name(r->declared_module));
        fprintf(f, "      \"module_actual_route\": \"%s\",\n", route_label(r->actual_route));
        fprintf(f, "      \"dnn_analysis_active\": %d,\n", r->dnn_analysis_active);
        fprintf(f, "      \"dnn_dred_attempted\": %d,\n", r->dnn_dred_attempted);
        fprintf(f, "      \"passed\": %d,\n", r->passed);
        if (r->error_msg[0])
            fprintf(f, "      \"error\": \"%s\",\n", r->error_msg);
        fprintf(f, "      \"overhead_us\": { \"create_enc\": %.2f, \"create_dec\": %.2f, \"destroy_enc\": %.2f, \"destroy_dec\": %.2f },\n",
                r->create_enc_us, r->create_dec_us, r->destroy_enc_us, r->destroy_dec_us);
        fprintf(f, "      \"encode_us\": { \"min\": %.2f, \"avg\": %.2f, \"max\": %.2f, \"p50\": %.2f, \"p95\": %.2f, \"p99\": %.2f, \"stddev\": %.2f, \"n\": %d },\n",
                r->enc_us.min, r->enc_us.avg, r->enc_us.max, r->enc_us.p50, r->enc_us.p95, r->enc_us.p99, r->enc_us.stddev, r->enc_us.count);
        fprintf(f, "      \"encode_throughput_x\": %.2f,\n", r->enc_throughput_x);
        fprintf(f, "      \"decode_us\": { \"min\": %.2f, \"avg\": %.2f, \"max\": %.2f, \"p50\": %.2f, \"p95\": %.2f, \"p99\": %.2f, \"stddev\": %.2f, \"n\": %d },\n",
                r->dec_us.min, r->dec_us.avg, r->dec_us.max, r->dec_us.p50, r->dec_us.p95, r->dec_us.p99, r->dec_us.stddev, r->dec_us.count);
        fprintf(f, "      \"decode_throughput_x\": %.2f,\n", r->dec_throughput_x);
        fprintf(f, "      \"roundtrip_us\": { \"min\": %.2f, \"avg\": %.2f, \"max\": %.2f, \"p50\": %.2f, \"p95\": %.2f, \"p99\": %.2f },\n",
                r->rt_us.min, r->rt_us.avg, r->rt_us.max, r->rt_us.p50, r->rt_us.p95, r->rt_us.p99);
        fprintf(f, "      \"roundtrip_throughput_x\": %.2f,\n", r->rt_throughput_x);
        fprintf(f, "      \"bitstream\": { \"avg_bytes_per_frame\": %.2f, \"actual_bitrate_kbps\": %.2f }\n",
                r->avg_bytes_per_frame, r->actual_bitrate_kbps);
        fprintf(f, "    }%s\n", (i < ctx->result_count - 1) ? "," : "");
    }
    fprintf(f, "  ]\n}\n");
    fclose(f);
    printf("[report] JSON 已写入: %s\n", fname);
}

/* ─── CSV 报告 ─────────────────────────────────────────────────── */
static void report_csv(ReportContext *ctx)
{
    char ts[64];
    timestamp_now(ts, sizeof(ts), 1);
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/csv", ctx->output_dir);
    mkdir_p(dir);
    char fname[1024];
    snprintf(fname, sizeof(fname), "%s/%s_%s_%s.csv", dir, ctx->lib_name,
             ctx->cfg->config_name, ts);
    FILE *f = fopen(fname, "w");
    if (!f) { fprintf(stderr, "[report] 无法写 CSV: %s\n", fname); return; }

    fprintf(f, "name,module,module_actual,passed,channels,frame_size,bitrate,chunk_frames,"
               "enc_min_us,enc_p50_us,enc_p95_us,enc_p99_us,enc_avg_us,enc_x,"
               "dec_min_us,dec_p50_us,dec_p95_us,dec_p99_us,dec_avg_us,dec_x,rt_min_us,rt_p50_us,rt_p95_us,rt_avg_us,rt_x,"
               "bytes_per_frame,actual_kbps\n");
    for (int i = 0; i < ctx->result_count; i++) {
        const PerfResult *r = &ctx->results[i];
        fprintf(f, "%s,%s,%s,%d,%d,%d,%d,%d,"
                   "%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,"
                   "%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n",
                r->name, module_machine_name(r->declared_module), route_label(r->actual_route), r->passed,
                r->channels, r->frame_size, r->bitrate, r->chunk_frames,
                r->enc_us.min, r->enc_us.p50, r->enc_us.p95, r->enc_us.p99, r->enc_us.avg, r->enc_throughput_x,
                r->dec_us.min, r->dec_us.p50, r->dec_us.p95, r->dec_us.p99, r->dec_us.avg, r->dec_throughput_x,
                r->rt_us.min, r->rt_us.p50, r->rt_us.p95, r->rt_us.avg, r->rt_throughput_x,
                r->avg_bytes_per_frame, r->actual_bitrate_kbps);
    }
    fclose(f);
    printf("[report] CSV 已写入: %s\n", fname);
}

void reporter_run(ReportContext *ctx)
{
    if (ctx->emit_text) report_text(ctx);
    if (ctx->emit_json) report_json(ctx);
    if (ctx->emit_csv)  report_csv(ctx);
}
