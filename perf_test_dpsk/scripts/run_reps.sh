#!/bin/bash
# run_reps.sh <OPUS_DIR> <LIBNAME> <REPS>  — 多点重复测量取最优
set -u
LIBDIR="$1"; LIBNAME="$2"; REPS="${3:-5}"
CONFIG=configs/final_bench.json
cd /home/zl_00986855/workspace/libopus/perf_test_dpsk
make clean >/dev/null 2>&1
if ! make OPUS_DIR="$LIBDIR" >/dev/null 2>&1; then echo "BUILD_FAIL"; exit 1; fi
for i in $(seq 1 "$REPS"); do
  OPUS_DIR="$LIBDIR" taskset -c 0 ./opus_perf_bench -c "$CONFIG" --lib-name "$LIBNAME" --cpu 0 -C --no-text >/dev/null 2>&1
done
echo "REPS_DONE:$REPS lib=$LIBNAME"
ls output/csv/*"$LIBNAME"*final-bench* | wc -l
