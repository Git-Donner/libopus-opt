#!/bin/bash
# run_compare.sh - 多版本对比：对每个 opus-* 版本编译并跑同一配置。
# 用法: ./scripts/run_compare.sh <config.json> [cpu_core]
set -u

FRAMEWORK_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT_ROOT="$(dirname "$FRAMEWORK_DIR")"
CONFIG="${1:-configs/module_coverage.json}"
CPU="${2:--1}"

# 要对比的版本目录（可自行增删）
VERSIONS=(
  "opus-1.6.1-Source"
  "opus-1.6.1-opt-celt"
  "opus-1.6.1-opt-silk"
)

cd "$FRAMEWORK_DIR"
mkdir -p output/compare

for ver in "${VERSIONS[@]}"; do
    echo ""
    echo "############ Testing: $ver ############"
    make clean >/dev/null 2>&1
    if ! make OPUS_DIR="../$ver" >/dev/null 2>&1; then
        echo "  [SKIP] $ver 编译失败（可能未构建静态库）"
        continue
    fi
    OPUS_DIR="../$ver" ./opus_perf_bench -c "$CONFIG" \
        --lib-name "$ver" --cpu "$CPU" -o "output/compare/$ver" -C -q >/dev/null 2>&1 \
        || echo "  [WARN] $ver 运行失败"
    echo "  [DONE] $ver -> output/compare/$ver"
done

echo ""
echo "==== 汇总 CSV ===="
for f in output/compare/*/csv/*.csv; do
    [ -f "$f" ] && { echo "--- $f ---"; head -1 "$f"; tail -n +2 "$f"; }
done
