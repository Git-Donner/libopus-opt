/*
 * env_ctrl.h - 环境控制：CPU 绑核、NUMA 绑定、内存对齐
 */
#ifndef OPUS_PERF_ENV_CTRL_H
#define OPUS_PERF_ENV_CTRL_H

#include <stddef.h>

typedef struct {
    int  cpu_core;      /* 绑定到的 CPU 核心，-1=不绑定 */
    int  numa_node;     /* -1=不指定 */
    int  bound_ok;      /* 绑核是否成功 */
    char cpu_model[128];
    int  nprocs;
} EnvState;

/* 预检 + 初始化环境（绑核等）。返回 0 成功。 */
int env_init(EnvState *env, int cpu_core, int numa_node);

/* 对齐分配（缓存行对齐） */
void *env_aligned_alloc(size_t size, size_t alignment);
void  env_aligned_free(void *p);

/* 打印环境信息 */
void env_print(EnvState *env);

#endif /* OPUS_PERF_ENV_CTRL_H */
