#!/bin/bash
# run_pair2.sh <SRC_OPUS> <VAR_OPUS> <VTAG> <N> <CONFIG>
# 用同一框架分别构建两个指定库，交stagger N 对测量（二者数值基/编译级别由调用方自定）。
set -u
SRC_OPUS="$1"; VAR_OPUS="$2"; VTAG="$3"; N="${4:-10}"; CONFIG="${5:-configs/compile_eval.json}"
RUNDIR=/tmp/pe/$VTAG
cd /home/zl_00986855/workspace/libopus/perf_test_dpsk

make clean >/dev/null 2>&1
make OPUS_DIR="$SRC_OPUS" >/dev/null 2>&1 || { echo BUILD_SRC_FAIL:$SRC_OPUS; exit 1; }
cp opus_perf_bench /tmp/bench_src
make clean >/dev/null 2>&1
make OPUS_DIR="$VAR_OPUS" >/dev/null 2>&1 || { echo BUILD_VAR_FAIL:$VAR_OPUS; exit 1; }
cp opus_perf_bench /tmp/bench_var

rm -rf "$RUNDIR"
for i in $(seq 1 "$N"); do
  mkdir -p "$RUNDIR/src_r$i" "$RUNDIR/var_r$i"
  OPUS_DIR="$SRC_OPUS" taskset -c 0 /tmp/bench_src -c "$CONFIG" --lib-name SRC --cpu 0 -C --no-text -o "$RUNDIR/src_r$i" >/dev/null 2>&1
  OPUS_DIR="$VAR_OPUS" taskset -c 0 /tmp/bench_var -c "$CONFIG" --lib-name VAR --cpu 0 -C --no-text -o "$RUNDIR/var_r$i" >/dev/null 2>&1
done
echo "PAIR2_DONE VTAG=$VTAG N=$N"
