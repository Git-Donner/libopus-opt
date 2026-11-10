/*
 * perf_test.c - 核心测试引擎实现
 */
#include "perf_test.h"
#include "pcm_gen.h"
#include "utils.h"

#include <opus.h>
#include <math.h>

static const char *app_label(int app)
{
    switch (app) {
        case OPUS_APPLICATION_VOIP: return "VOIP";
        case OPUS_APPLICATION_AUDIO: return "AUDIO";
        case OPUS_APPLICATION_RESTRICTED_LOWDELAY: return "LOWDELAY";
        case OPUS_APPLICATION_RESTRICTED_SILK: return "RESTRICTED_SILK";
        case OPUS_APPLICATION_RESTRICTED_CELT: return "RESTRICTED_CELT";
        default: return "?";
    }
}

static void set_result_error(PerfResult *res, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(res->error_msg, sizeof(res->error_msg), fmt, ap);
    va_end(ap);
    res->error_count++;
    res->passed = 0;
}

void perf_run_case(const TestCase *tc, const RunOptions *opt, PerfResult *res)
{
    memset(res, 0, sizeof(*res));
    snprintf(res->name, sizeof(res->name), "%s", tc->name);
    snprintf(res->description, sizeof(res->description), "%s", tc->description);
    res->declared_module = tc->module;
    res->channels   = tc->channels;
    res->frame_size = tc->frame_size;
    res->bitrate    = tc->bitrate;
    res->application = tc->application;
    res->complexity  = tc->complexity;
    res->loss_perc   = tc->loss_perc;

    int Fs = SAMPLE_RATE;
    int ch = tc->channels;
    int app = tc->application;
    int chunk_mode = tc->chunk_frames > 0;
    int cf = chunk_mode ? tc->chunk_frames : 1;

    if (opt->verbose)
        printf("      [run] %-30s module=%-6s op=%d ch=%d app=%s frame=%d br=%d\n",
               tc->name, module_label(tc->module),
               (int)tc->operation, ch, app_label(app), tc->frame_size, tc->bitrate);

    /* ── DNN 分析激活判定 ─────────────────────────────────────── */
    res->dnn_analysis_active = probe_dnn_analysis_active(app, tc->complexity, Fs);
    res->dnn_dred_attempted  = probe_dnn_dred_configurable(tc->dred_duration);

    /* ── 缓冲 ─────────────────────────────────────────────────── */
    int in_samples = tc->frame_size * ch;
    opus_int16 *pcm_in = (opus_int16 *)env_aligned_alloc(
        sizeof(opus_int16) * (size_t)in_samples, CACHE_LINE);
    opus_int16 *pcm_out = (opus_int16 *)env_aligned_alloc(
        sizeof(opus_int16) * (size_t)MAX_FRAME_SIZE * ch, CACHE_LINE);
    unsigned char *cbits = (unsigned char *)xmalloc(MAX_PACKET_SIZE);
    if (!pcm_in || !pcm_out) { set_result_error(res, "缓冲分配失败"); goto cleanup; }

    PcmGen gen;
    pcm_gen_init(&gen, opt->seed, opt->noise_db);

    /* ── 创建编码器 ───────────────────────────────────────────── */
    int err = 0;
    double t;
    t = now_us();
    OpusEncoder *enc = opus_encoder_create(Fs, ch, app, &err);
    res->create_enc_us = now_us() - t;
    if (err != OPUS_OK || !enc) {
        set_result_error(res, "opus_encoder_create 失败: %s", opus_strerror(err));
        goto cleanup_enc;
    }
#define ENC_CTL_SUCCESS(call) \
    do { int __e = (call); if (__e != OPUS_OK) { set_result_error(res, "CTL失败: " #call ": %s", opus_strerror(__e)); goto cleanup_enc; } } while (0)

    ENC_CTL_SUCCESS(opus_encoder_ctl(enc, OPUS_SET_BITRATE(tc->bitrate)));
    if (tc->bandwidth > 0)
        ENC_CTL_SUCCESS(opus_encoder_ctl(enc, OPUS_SET_BANDWIDTH(tc->bandwidth)));
    if (tc->complexity != -1000)
        ENC_CTL_SUCCESS(opus_encoder_ctl(enc, OPUS_SET_COMPLEXITY(tc->complexity)));
    if (tc->use_fec)
        ENC_CTL_SUCCESS(opus_encoder_ctl(enc, OPUS_SET_INBAND_FEC(tc->use_fec)));
    if (tc->use_dtx)
        ENC_CTL_SUCCESS(opus_encoder_ctl(enc, OPUS_SET_DTX(tc->use_dtx)));
    if (tc->loss_perc > 0)
        ENC_CTL_SUCCESS(opus_encoder_ctl(enc, OPUS_SET_PACKET_LOSS_PERC(tc->loss_perc)));
#ifdef OPUS_SET_DRED_DURATION
    if (tc->dred_duration > 0)
        opus_encoder_ctl(enc, OPUS_SET_DRED_DURATION(tc->dred_duration)); /* 不支持则忽略 */
#endif

    /* ── 创建解码器（decode 链路需要） ────────────────────────── */
    OpusDecoder *dec = NULL;
    t = now_us();
    dec = opus_decoder_create(Fs, ch, &err);
    res->create_dec_us = now_us() - t;
    if (err != OPUS_OK || !dec) {
        set_result_error(res, "opus_decoder_create 失败: %s", opus_strerror(err));
        goto cleanup_enc;
    }

    /* ── 收集器 ───────────────────────────────────────────────── */
    StatsCollector enc_c, dec_c, rt_c;
    stats_collector_init(&enc_c, tc->iterations);
    stats_collector_init(&dec_c, tc->iterations);
    stats_collector_init(&rt_c, tc->iterations);

    long total_payload = 0;
    res->frames_encoded = res->frames_decoded = 0;
    res->chunk_frames = (tc->chunk_frames > 0) ? tc->chunk_frames : 0;

    if (chunk_mode) {
        /* ── 固定长度块模式：一次性处理一整块(含 cf 帧)再计时 ──
           目的：摊销每次 opus_encode/decode 的固定调用开销，让块内
           SIMD/内核级优势在"整块处理耗时"上显现，避免被单帧开销淹没。 */
        int cf = tc->chunk_frames;
        size_t chunk_samples = (size_t)cf * (size_t)in_samples;
        opus_int16  *chunk_buf = (opus_int16 *)env_aligned_alloc(
            sizeof(opus_int16) * chunk_samples, CACHE_LINE);
        unsigned char *pkts  = (unsigned char *)xmalloc((size_t)MAX_PACKET_SIZE * (size_t)cf);
        int          *plens  = (int *)xmalloc(sizeof(int) * (size_t)cf);
        if (!chunk_buf || !pkts || !plens) {
            set_result_error(res, "块缓冲分配失败");
            env_aligned_free(chunk_buf); free(pkts); free(plens);
            goto done_measure;
        }

        /* 预生成整块 PCM（确定性、块内帧连续） */
        for (int f = 0; f < cf; f++)
            pcm_gen_fill(&gen, chunk_buf + (size_t)f * in_samples, tc->frame_size, ch);

        /* 预热若干块（状态沉降，不计入统计） */
        for (int w = 0; w < tc->warmup; w++) {
            for (int f = 0; f < cf; f++) {
                int nb = opus_encode(enc, chunk_buf + (size_t)f * in_samples,
                                     tc->frame_size, pkts + (size_t)f * MAX_PACKET_SIZE, MAX_PACKET_SIZE);
                if (nb < 0) { set_result_error(res, "warmup encode: %s", opus_strerror(nb)); goto cleanup_chunk; }
                if (tc->operation != OP_ENCODE_ONLY) {
                    if (opus_decode(dec, pkts + (size_t)f * MAX_PACKET_SIZE, nb,
                                    pcm_out, MAX_FRAME_SIZE, 0) < 0) {
                        set_result_error(res, "warmup decode 失败");
                        goto cleanup_chunk;
                    }
                }
            }
            res->frames_encoded += cf;
            res->frames_decoded += (tc->operation != OP_ENCODE_ONLY) ? cf : 0;
        }

        /* 正式测量：每个样本 = 整块(encode[+decode])耗时 */
        int need_dec = tc->operation != OP_ENCODE_ONLY;
        for (int k = 0; k < tc->iterations; k++) {
            double te0 = now_us();
            for (int f = 0; f < cf; f++) {
                int nb = opus_encode(enc, chunk_buf + (size_t)f * in_samples,
                                     tc->frame_size, pkts + (size_t)f * MAX_PACKET_SIZE, MAX_PACKET_SIZE);
                if (nb < 0) { set_result_error(res, "encode blk%d f%d: %s", k, f, opus_strerror(nb)); goto cleanup_chunk; }
                plens[f] = nb;
            }
            double te1 = now_us();

            double td0 = 0, td1 = 0;
            if (need_dec) {
                td0 = now_us();
                for (int f = 0; f < cf; f++) {
                    if (opus_decode(dec, pkts + (size_t)f * MAX_PACKET_SIZE, plens[f],
                                    pcm_out, MAX_FRAME_SIZE, 0) < 0) {
                        set_result_error(res, "decode blk%d f%d 失败", k, f);
                        goto cleanup_chunk;
                    }
                }
                td1 = now_us();
            }

            stats_collector_add(&enc_c, te1 - te0);
            if (need_dec) stats_collector_add(&dec_c, td1 - td0);
            stats_collector_add(&rt_c, (need_dec ? td1 : te1) - te0);

            for (int f = 0; f < cf; f++) total_payload += plens[f];
            res->frames_encoded += cf;
            res->frames_decoded += need_dec ? cf : 0;

            /* 模块探测：采样整块第 1 帧的 TOC */
            if ((k & 0x3) == 0)
                res->route_counts[probe_packet_mode(pkts, plens[0])]++;
        }

        /* 每块字节数按实际测量帧数折算：块内含 cf 帧 */
        res->avg_bytes_per_frame = (double)total_payload /
            (double)((long)cf * (tc->iterations > 0 ? tc->iterations : 1));

cleanup_chunk:
        env_aligned_free(chunk_buf);
        free(pkts);
        free(plens);
        if (res->error_count) goto done_measure;
    } else {
        /* ── 旧单帧模式（逐帧调用一次 API，逐帧计时） ── */
        int total = tc->warmup + tc->iterations;
        for (int k = 0; k < total; k++) {
            pcm_gen_fill(&gen, pcm_in, tc->frame_size, ch);

            double te0 = now_us();
            int nb = opus_encode(enc, pcm_in, tc->frame_size, cbits, MAX_PACKET_SIZE);
            double te1 = now_us();

            if (nb < 0) {
                set_result_error(res, "encode 失败 iter%d: %s", k, opus_strerror(nb));
                goto done_measure;
            }

            double td0 = 0, td1 = 0, tRt0 = te0, tRt1 = te1;
            if (tc->operation != OP_ENCODE_ONLY) {
                td0 = now_us();
                int fs = opus_decode(dec, cbits, nb, pcm_out, MAX_FRAME_SIZE, 0);
                td1 = now_us();
                tRt1 = td1;
                if (fs < 0) {
                    set_result_error(res, "decode 失败 iter%d: %s", k, opus_strerror(fs));
                    goto done_measure;
                }
                res->frames_decoded++;
            }

            if (k >= tc->warmup) {
                stats_collector_add(&enc_c, te1 - te0);
                if (tc->operation != OP_ENCODE_ONLY)
                    stats_collector_add(&dec_c, td1 - td0);
                stats_collector_add(&rt_c, tRt1 - tRt0);
                total_payload += nb;
            }
            res->frames_encoded++;

            if ((k & 0xF) == 0)
                res->route_counts[probe_packet_mode(cbits, nb)]++;
        }
    }

done_measure:
    /*** teardown ***/
    t = now_us();
    opus_encoder_destroy(enc);
    res->destroy_enc_us = now_us() - t;
    if (dec) {
        t = now_us();
        opus_decoder_destroy(dec);
        res->destroy_dec_us = now_us() - t;
    }

    /* ── 汇总统计 ─────────────────────────────────────────────── */
    stats_finalize(&enc_c, &res->enc_us);
    if (tc->operation != OP_ENCODE_ONLY)
        stats_finalize(&dec_c, &res->dec_us);
    stats_finalize(&rt_c, &res->rt_us);

    double frame_ms = (double)tc->frame_size / (double)Fs * 1000.0;
    /* 块模式：每个样本 = 一整块(含 cf 帧)耗时；吞吐按 cf 帧折算 */
    double spf = (double)cf;
    if (res->enc_us.avg > 0)
        res->enc_throughput_x = frame_ms * spf * 1000.0 / res->enc_us.avg;
    if (res->dec_us.avg > 0)
        res->dec_throughput_x = frame_ms * spf * 1000.0 / res->dec_us.avg;
    if (res->rt_us.avg > 0)
        res->rt_throughput_x = frame_ms * spf * 1000.0 / res->rt_us.avg;

    if (!chunk_mode) {
        int measured = tc->iterations > 0 ? tc->iterations : 1;
        res->avg_bytes_per_frame = (double)total_payload / (double)measured;
    }
    res->actual_bitrate_kbps = res->avg_bytes_per_frame * 8.0 / frame_ms;

    /* ── 模块验证 ─────────────────────────────────────────────── */
    int total_routed = res->route_counts[0] + res->route_counts[1] +
                       res->route_counts[2] + res->route_counts[3];
    RouteModule major = ROUTE_UNKNOWN;
    int major_cnt = -1;
    for (int m = 0; m < 4; m++) {
        if (res->route_counts[m] > major_cnt) { major_cnt = res->route_counts[m]; major = (RouteModule)m; }
    }
    res->actual_route = major;
    /* 次少数派：找出第二个非零 */
    int seen_major = 0;
    for (int m = 0; m < 4; m++) {
        if ((RouteModule)m == major) { seen_major = 1; continue; }
        if (res->route_counts[m] > 0) { res->actual_route_minor = (RouteModule)m; }
    }
    (void)seen_major; (void)total_routed;

    /* 判定：声明的模块 vs 实际路由 */
    res->passed = (res->error_count == 0);
    if (res->declared_module == MOD_EXPECT_CELT && res->actual_route != ROUTE_CELT)
        { res->passed = 0; set_result_error(res, "期望 CELT 但实际路由到 %s", route_label(major)); }
    if (res->declared_module == MOD_EXPECT_SILK && res->actual_route != ROUTE_SILK)
        { res->passed = 0; set_result_error(res, "期望 SILK 但实际路由到 %s", route_label(major)); }
    if (res->declared_module == MOD_EXPECT_HYBRID && res->actual_route != ROUTE_HYBRID)
        { res->passed = 0; set_result_error(res, "期望 HYBRID 但实际路由到 %s", route_label(major)); }
    /* DNN 用例：只需 DNN 分析路径激活即可视为命中（模式可为 CELT/SILK） */
    if (res->declared_module == MOD_EXPECT_DNN && !res->dnn_analysis_active)
        { res->passed = 0; set_result_error(res, "DNN 分析路径未激活（需 complexity>=7 & Fs 16k-48k & app!=RESTRICTED_SILK）"); }

    stats_collector_free(&enc_c);
    stats_collector_free(&dec_c);
    stats_collector_free(&rt_c);
    free(cbits);
    env_aligned_free(pcm_in);
    env_aligned_free(pcm_out);
    return;

cleanup_enc:
    if (dec) opus_decoder_destroy(dec);
cleanup:
    free(cbits);
    env_aligned_free(pcm_in);
    env_aligned_free(pcm_out);
}
