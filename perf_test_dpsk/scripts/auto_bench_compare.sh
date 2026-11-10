#!/bin/bash
# =============================================================================
# auto_bench_compare.sh — 一键测试 + 对比
#
# 流程：
#   1) 构建 Source(定点原始版) 二进制
#   2) 构建 zhujing(定点优化版) 二进制
#   3) 各自【独立】测 N 次（不交错）
#   4) 自动化对比：逐测试用例、逐指标(enc/dec 的 min/p50/p95)，
#      计算 N 次独立测试的提升比例的平均值
#
# 用法：
#   bash scripts/auto_bench_compare.sh [N] [config.json]
#     N      = 每个库独立测试次数（默认 1）
#     config = 测试配置 JSON（默认 configs/compile_eval.json）
#
# 可选环境变量（覆盖库路径/输出标记）：
#   SRC_DIR=<Source 库目录>   ZHU_DIR=<zhujing 库目录>   TAG=<输出子目录名>
# =============================================================================
set -u

FRAMEWORK="/home/zl_00986855/workspace/libopus/perf_test_dpsk"
SRC_DIR="${SRC_DIR:-/home/zl_00986855/workspace/libopus/src_fixed_stock}"
ZHU_DIR="${ZHU_DIR:-/home/zl_00986855/workspace/libopus/zj_v1}"

N="${1:-1}"
CONFIG="${2:-configs/compile_eval.json}"
TAG="${TAG:-auto}"
OUTROOT="$FRAMEWORK/output/compare/$TAG"

# 参数校验
if ! [ "$N" -ge 1 ] 2>/dev/null; then echo "错误: N 必须是 >=1 的整数"; exit 1; fi
if [ ! -f "$FRAMEWORK/$CONFIG" ]; then echo "错误: 配置不存在 $CONFIG"; exit 1; fi

cd "$FRAMEWORK" || exit 1

echo "============================================================="
echo " 一键测试+对比  |  N=$N  |  配置=$CONFIG"
echo "  Source : $SRC_DIR"
echo "   zhujing: $ZHU_DIR"
echo "============================================================="

# ---------- 1) 构建 Source 二进制 ----------
echo "[-1/4-] 构建 Source 二进制 ..."
make clean >/dev/null 2>&1
make OPUS_DIR="$SRC_DIR" >/dev/null 2>&1 || { echo "!! Source 构建失败"; exit 1; }
cp opus_perf_bench /tmp/bench_src

# ---------- 2) 构建 zhujing 二进制 ----------
echo "[-2/4-] 构建 zhujing 二进制 ..."
make clean >/dev/null 2>&1
make OPUS_DIR="$ZHU_DIR" >/dev/null 2>&1 || { echo "!! zhujing 构建失败"; exit 1; }
cp opus_perf_bench /tmp/bench_zhu

# ---------- 3) 各自独立测 N 次 ----------
echo "[-3/4-] Source 独立测试 $N 次 ..."
rm -rf "$OUTROOT"
for i in $(seq 1 "$N"); do
  mkdir -p "$OUTROOT/src/r$i" "$OUTROOT/zhu/r$i"
  OPUS_DIR="$SRC_DIR" taskset -c 0 /tmp/bench_src \
    -c "$CONFIG" --lib-name SRC --cpu 0 -C --no-text -o "$OUTROOT/src/r$i" >/dev/null 2>&1
  echo "        Source  #$i done"
done

echo "        zhujing 独立测试 $N 次 ..."
for i in $(seq 1 "$N"); do
  OPUS_DIR="$ZHU_DIR" taskset -c 0 /tmp/bench_zhu \
    -c "$CONFIG" --lib-name ZHU --cpu 0 -C --no-text -o "$OUTROOT/zhu/r$i" >/dev/null 2>&1
  echo "        zhujing #$i done"
done

# ---------- 4) 对比计算 ----------
echo "[-4/4-] 对比计算 (N=$N 独立测试，逐用例逐指标取平均值) ..."
python3 "$FRAMEWORK/scripts/compare_aggreg.py" "$OUTROOT" "$N"

echo "============================================================="
echo " 完成。原始数据: $OUTROOT  |  汇总: $OUTROOT/summary.csv"
echo "============================================================="
