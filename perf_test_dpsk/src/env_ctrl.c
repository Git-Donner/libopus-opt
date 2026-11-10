/*
 * env_ctrl.c - 环境控制实现
 */
#include "env_ctrl.h"
#include "utils.h"

#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <errno.h>

void *env_aligned_alloc(size_t size, size_t alignment)
{
    void *p = NULL;
    if (posix_memalign(&p, alignment ? alignment : CACHE_LINE, size) != 0)
        return NULL;
    return p;
}

void env_aligned_free(void *p)
{
    free(p);
}

int env_init(EnvState *env, int cpu_core, int numa_node)
{
    memset(env, 0, sizeof(*env));
    env->cpu_core = cpu_core;
    env->numa_node = numa_node;
    env->bound_ok = 0;

    env->nprocs = (int)sysconf(_SC_NPROCESSORS_ONLN);
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "model name", 10) == 0 || strncmp(line, "Processor", 9) == 0) {
                char *colon = strchr(line, ':');
                if (colon) {
                    char *s = colon + 1;
                    while (*s == ' ') s++;
                    s[strcspn(s, "\r\n")] = '\0';
                    snprintf(env->cpu_model, sizeof(env->cpu_model), "%s", s);
                    break;
                }
            }
        }
        fclose(f);
    }

    if (cpu_core >= 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET((size_t)cpu_core, &cpuset);
        if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) == 0) {
            env->bound_ok = 1;
        } else {
            fprintf(stderr, "[env] 警告: 绑定 CPU %d 失败 (%s)\n", cpu_core, strerror(errno));
        }
    }

    (void)numa_node; /* NUMA 绑定通过 taskset/numactl 脚本层处理，这里仅记录 */
    return PERF_OK;
}

void env_print(EnvState *env)
{
    printf("  CPU Model : %s\n", env->cpu_model[0] ? env->cpu_model : "n/a");
    printf("  CPU Cores : %d\n", env->nprocs);
    if (env->bound_ok)
        printf("  CPU Bound : core %d (OK)\n", env->cpu_core);
    else if (env->cpu_core >= 0)
        printf("  CPU Bound : core %d (FAILED)\n", env->cpu_core);
    else
        printf("  CPU Bound : none (use taskset)\n");
}
