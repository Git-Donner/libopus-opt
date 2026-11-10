#!/usr/bin/env python3
"""Run broad CELT decode comparison between Source and Inline versions."""
import os, json, subprocess

def main():
    os.chdir("/home/zl_00986855/workspace/libopus/perf_test_dpsk")
    
    # Verify config
    with open("celt_decode_broad.json") as f:
        cfg = json.load(f)
    print(f"Config OK: {len(cfg['test_cases'])} test cases")
    
    # Clean output
    os.system("rm -rf output/broad")
    
    # Source run
    os.system("make clean >/dev/null 2>&1")
    ret = os.system("make OPUS_DIR=../opus-1.6.1-Source >/dev/null 2>&1")
    if ret != 0:
        print("Source build FAILED")
        return 1
    print("Source build OK")
    ret = os.system("./opus_perf_bench -c celt_decode_broad.json --lib-name SRC --cpu 0 -C --no-text -o output/broad/src")
    print(f"Source run: ret={ret}")
    
    # Inline run
    os.system("make clean >/dev/null 2>&1")
    ret = os.system("make OPUS_DIR=../opus-1.6.1-opt-celt-decoder >/dev/null 2>&1")
    if ret != 0:
        print("Inline build FAILED")
        return 1
    print("Inline build OK")
    ret = os.system("./opus_perf_bench -c celt_decode_broad.json --lib-name INLINE --cpu 0 -C --no-text -o output/broad/opt")
    print(f"Inline run: ret={ret}")
    
    # Collect results
    src_csv = None
    opt_csv = None
    for f in os.listdir("output/broad/src/csv"):
        if f.endswith(".csv"):
            src_csv = f"output/broad/src/csv/{f}"
            break
    for f in os.listdir("output/broad/opt/csv"):
        if f.endswith(".csv"):
            opt_csv = f"output/broad/opt/csv/{f}"
            break
    
    if not src_csv or not opt_csv:
        print("Missing CSV files")
        return 1
    
    # Parse and compare
    with open(src_csv) as f:
        src_lines = f.readlines()
    with open(opt_csv) as f:
        opt_lines = f.readlines()
    
    header = src_lines[0].strip().split(",")
    dec_p50_idx = header.index("dec_p50_us")
    name_idx = header.index("name")
    
    print("\n" + "="*70)
    print(f"{'Test Case':<30} {'SRC(us)':>10} {'INLINE(us)':>10} {'Delta%':>8} {'Benefit':>10}")
    print("-"*70)
    
    for i in range(1, len(src_lines)):
        src_fields = src_lines[i].strip().split(",")
        opt_fields = opt_lines[i].strip().split(",")
        name = src_fields[name_idx]
        src_p50 = float(src_fields[dec_p50_idx])
        opt_p50 = float(opt_fields[dec_p50_idx])
        delta = (opt_p50 / src_p50 - 1) * 100
        benefit = "***" if delta < -2 else ("**" if delta < -1 else ("*" if delta < -0.5 else ""))
        print(f"{name:<30} {src_p50:>10.2f} {opt_p50:>10.2f} {delta:>7.2f}% {benefit:>10}")
    
    return 0

if __name__ == "__main__":
    exit(main())
