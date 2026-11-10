#!/bin/bash
# run_pair_eval.sh <VOPUS> <VTAG> <N> <CONFIG>
# 用更新后的框架分别构建 Source 与变体二进制，交stagger N 队测量。
set -u
VOPUS="$1"; VTAG="$2"; N="${3:-10}"; CONFIG="${4:-configs/compile_eval.json}"
RUNDIR=/tmp/pe/$VTAG
cd /home/zl_00986855/workspace/libopus/perf_test_dpsk

make clean >/dev/null 2>&1
make OPUS_DIR=../opus-1.6.1-Source >/dev/null 2>&1 || { echo BUILD_SRC_FAIL; exit 1; }
cp opus_perf_bench /tmp/bench_src
make clean >/dev/null 2>&1
make OPUS_DIR="$VOPUS" >/dev/null 2>&1 || { echo BUILD_VAR_FAIL:$VOPUS; exit 1; }
cp opus_perf_bench /tmp/bench_var

rm -rf "$RUNDIR"
for i in $(seq 1 "$N"); do
  mkdir -p "$RUNDIR/src_r$i" "$RUNDIR/var_r$i"
  OPUS_DIR=/home/zl_00986855/workspace/libopus/opus-1.6.1-Source \
    taskset -c 0 /tmp/bench_src -c "$CONFIG" --lib-name SRC --cpu 0 -C --no-text -o "$RUNDIR/src_r$i" >/dev/null 2>&1
  OPUS_DIR="$VOPUS" \
    taskset -c 0 /tmp/bench_var -c "$CONFIG" --lib-name VAR --cpu 0 -C --no-text -o "$RUNDIR/var_r$i" >/dev/null 2>&1
done
echo "PAIR_DONE VTAG=$VTAG N=$N"
