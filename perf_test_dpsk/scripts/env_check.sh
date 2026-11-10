#!/bin/bash
# env_check.sh - 测试前环境预检（鲲鹏 aarch64）
# 检查 CPU 型号、governor、负载、是否有异常进程，保证控制变量一致。
set -u

echo "================ opus_perf_dpsk 环境预检 ================"

echo -n "CPU Model  : "
lscpu 2>/dev/null | grep -E 'Model name|BIOS Model name' | head -1 | awk -F: '{print $2}' | xargs

echo -n "Cores      : "
nproc

echo -n "Governor   : "
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo "n/a"

echo -n "Loadavg(1/5/15): "
cat /proc/loadavg 2>/dev/null | awk '{print $1"/"$2"/"$3}' || uptime

NCORES=$(nproc)
LOAD1=$(cat /proc/loadavg | awk '{print $1}')
LOAD_INT=${LOAD1%.*}
echo "负载检查   : load1=$LOAD1 (阈值 = 核数$NCORES * 0.5 = $(echo "$NCORES*0.5" | bc))"
LIMIT=$(echo "$NCORES*0.5" | bc)
if [ "$LOAD1" = "$LOAD1" ] 2>/dev/null; then :; fi
if awk "BEGIN{exit !($LOAD1 < $LIMIT)}"; then
    echo "  OK: 系统负载低，可开始测试。"
else
    echo "  WARN: 负载偏高，建议等待或检查后台任务。"
fi

echo "---------------- CPU 占用 Top ---"
ps -eo pcpu,comm --sort=-pcpu 2>/dev/null | head -6

echo "============================================================"
echo "建议: taskset -c 0 ./opus_perf_bench -c configs/xxx.json --cpu 0"
