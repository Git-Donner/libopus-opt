#!/bin/bash
# run_paired.sh — 交错配对测量：Source 与 zhujing 背靠背交替跑 N 对，
# 使负载漂移对两者同等影响，得到可配对比较的数据。
set -u
N="${1:-10}"
CONFIG=configs/final_bench.json
RUNDIR=/tmp/paired
cd /home/zl_00986855/workspace/libopus/perf_test_dpsk

# 预构建两个二进制，避免重复重建
make clean >/dev/null 2>&1
make OPUS_DIR=../opus-1.6.1-Source >/dev/null 2>&1 || { echo BUILD_SRC_FAIL; exit 1; }
cp opus_perf_bench /tmp/bench_src
make clean >/dev/null 2>&1
make OPUS_DIR=/home/zhujing/202608/opus >/dev/null 2>&1 || { echo BUILD_ZHU_FAIL; exit 1; }
cp opus_perf_bench /tmp/bench_zhu

rm -rf "$RUNDIR"
for i in $(seq 1 "$N"); do
  mkdir -p "$RUNDIR/src_r$i" "$RUNDIR/zhu_r$i"
  OPUS_DIR=/home/zl_00986855/workspace/libopus/opus-1.6.1-Source \
    taskset -c 0 /tmp/bench_src -c "$CONFIG" --lib-name SRC --cpu 0 -C --no-text \
    -o "$RUNDIR/src_r$i" >/dev/null 2>&1
  OPUS_DIR=/home/zhujing/202608/opus \
    taskset -c 0 /tmp/bench_zhu -c "$CONFIG" --lib-name ZHU --cpu 0 -C --no-text \
    -o "$RUNDIR/zhu_r$i" >/dev/null 2>&1
done
echo "PAIRED_DONE N=$N"
