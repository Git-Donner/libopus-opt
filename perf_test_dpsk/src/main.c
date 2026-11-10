/*
 * main.c - 入口：解析 CLI、加载配置、调度测试、输出报告
 *
 * 用法:
 *   ./opus_perf_bench -c <config.json> [options]
 * 需要头文件与库通过 Makefile 的 OPUS_DIR 注入（run-time 通过 -l/--lib-name）。
 */
#include "config.h"
#include "perf_test.h"
#include "reporter.h"
#include "env_ctrl.h"
#include "module_probe.h"
#include "utils.h"

#include <getopt.h>
#include <string.h>

static void usage(const char *prog)
{
    printf(
        "Usage: %s -c <config.json> [options]\n"
        "\n"
        "Required:\n"
        "  -c, --config <FILE>     测试配置 JSON 文件\n"
        "\n"
        "Optional:\n"
        "  -l, --lib-name <NAME>   被测库名称(用于报告命名, 默认取自 OPUS_DIR)\n"
        "  -o, --output-dir <DIR>  输出目录 (默认 ./output)\n"
        "      --cpu <N>           绑定 CPU 核心 (默认 -1=不绑定建议 taskset)\n"
        "  -s, --seed <N>          PCM 随机种子 (默认 42)\n"
        "  -n, --noise <dB>        白噪声电平 (默认 0)\n"
        "  -j, --json              JSON 输出\n"
        "  -C, --csv               CSV 输出\n"
        "  -t, --text              (默认) 终端文本输出\n"
        "  -q, --quiet             安静模式(仅 no banner)\n"
        "  -v, --verbose           详细输出\n"
        "      --no-text           关闭终端输出\n"
        "  -h, --help              帮助\n",
        prog);
}

int main(int argc, char **argv)
{
    const char *config_file = NULL;
    const char *lib_name = NULL;
    const char *output_dir = "output";
    int cpu_core = -1;
    int seed = 42;
    double noise_db = 0.0;
    int emit_json = 0, emit_csv = 0, emit_text = 1, verbose = 0;

    static struct option longopts[] = {
        {"config",     required_argument, 0, 'c'},
        {"lib-name",   required_argument, 0, 'l'},
        {"output-dir", required_argument, 0, 'o'},
        {"cpu",        required_argument, 0, 1000},
        {"seed",       required_argument, 0, 's'},
        {"noise",      required_argument, 0, 'n'},
        {"json",       no_argument,       0, 'j'},
        {"csv",        no_argument,       0, 'C'},
        {"text",       no_argument,       0, 't'},
        {"quiet",      no_argument,       0, 'q'},
        {"verbose",    no_argument,       0, 'v'},
        {"no-text",    no_argument,       0, 1001},
        {"help",       no_argument,       0, 'h'},
        {0,0,0,0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "c:l:o:s:n:jCtqvh", longopts, NULL)) != -1) {
        switch (opt) {
            case 'c': config_file = optarg; break;
            case 'l': lib_name = optarg; break;
            case 'o': output_dir = optarg; break;
            case 1000: cpu_core = atoi(optarg); break;
            case 's': seed = atoi(optarg); break;
            case 'n': noise_db = atof(optarg); break;
            case 'j': emit_json = 1; break;
            case 'C': emit_csv = 1; break;
            case 't': emit_text = 1; break;
            case 'q': break;
            case 'v': verbose = 1; break;
            case 1001: emit_text = 0; break;
            case 'h': usage(argv[0]); return EXIT_SUCCESS;
            default: usage(argv[0]); return EXIT_FAILURE;
        }
    }

    if (!config_file) {
        fprintf(stderr, "[main] 缺少 -c 配置参数\n\n");
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    /* ── 加载配置 ─────────────────────────────────────────────── */
    TestConfig cfg;
    if (config_load(config_file, &cfg) != PERF_OK) {
        fprintf(stderr, "[main] 配置加载失败\n");
        return EXIT_FAILURE;
    }
    if (cfg.count == 0) {
        fprintf(stderr, "[main] 配置中无测试用例\n");
        config_free(&cfg);
        return EXIT_FAILURE;
    }

    /* ── 库名 ─────────────────────────────────────────────────── */
    const char *the_lib = lib_name;
    char auto_lib[128];
    if (!the_lib) {
        const char *env = getenv("OPUS_DIR");
        if (env) {
            const char *slash = strrchr(env, '/');
            snprintf(auto_lib, sizeof(auto_lib), "%s", slash ? slash + 1 : env);
        } else {
            snprintf(auto_lib, sizeof(auto_lib), "unknown");
        }
        the_lib = auto_lib;
    }

    /* ── 环境初始化 ───────────────────────────────────────────── */
    EnvState env;
    env_init(&env, cpu_core, -1);

    /* ── 运行所有用例 ─────────────────────────────────────────── */
    RunOptions ropt;
    ropt.env = &env;
    ropt.seed = (unsigned int)seed;
    ropt.noise_db = noise_db;
    ropt.verbose = verbose;

    PerfResult *results = (PerfResult *)xcalloc((size_t)cfg.count, sizeof(PerfResult));
    for (int i = 0; i < cfg.count; i++) {
        if (verbose) printf("\n--- case %d/%d ---\n", i + 1, cfg.count);
        perf_run_case(&cfg.cases[i], &ropt, &results[i]);
    }

    /* ── 报告 ─────────────────────────────────────────────────── */
    if (!emit_json && !emit_csv) emit_text = 1; /* 默认文本 */

    ReportContext rctx;
    memset(&rctx, 0, sizeof(rctx));
    rctx.cfg = &cfg;
    rctx.results = results;
    rctx.result_count = cfg.count;
    rctx.env = &env;
    rctx.lib_name = the_lib;
    rctx.lib_path = getenv("OPUS_DIR") ? getenv("OPUS_DIR") : the_lib;
    rctx.config_file = config_file;
    rctx.output_dir = output_dir;
    rctx.seed = (unsigned int)seed;
    rctx.noise_db = noise_db;
    rctx.cpu_core = cpu_core;
    rctx.emit_text = emit_text;
    rctx.emit_json = emit_json || 1; /* 始终产 JSON 供 KPBot/对比消费 */
    rctx.emit_csv = emit_csv;

    reporter_run(&rctx);

    free(results);
    config_free(&cfg);
    return EXIT_SUCCESS;
}
