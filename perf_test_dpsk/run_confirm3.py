#!/usr/bin/env python3
"""Multi-run CELT decode comparison with 3 independent measurements per version."""
import os, json, subprocess, statistics

N_RUNS = 3
CONFIG = "celt_decode_confirm.json"
OUTDIR = "output/confirm3"
SRC_DIR = "../opus-1.6.1-Source"
OPT_DIR = "../opus-1.6.1-opt-celt-decoder"

def build_and_run(label, opus_dir):
    os.system("make clean >/dev/null 2>&1")
    ret = os.system(f"make OPUS_DIR={opus_dir} >/dev/null 2>&1")
    if ret != 0:
        print(f"  {label} BUILD FAILED")
        return []
    results = []
    for r in range(1, N_RUNS+1):
        out = f"{OUTDIR}/{label}/r{r}"
        ret = os.system(f"./opus_perf_bench -c {CONFIG} --lib-name {label} --cpu 0 -C --no-text -o {out} >/dev/null 2>&1")
        if ret == 0:
            # Find CSV
            for f in os.listdir(f"{out}/csv"):
                if f.endswith(".csv"):
                    results.append(f"{out}/csv/{f}")
                    break
    return results

def parse_csv(path):
    with open(path) as f:
        lines = f.readlines()
    header = lines[0].strip().split(",")
    dec_p50_idx = header.index("dec_p50_us")
    name_idx = header.index("name")
    data = {}
    for line in lines[1:]:
        fields = line.strip().split(",")
        data[fields[name_idx]] = float(fields[dec_p50_idx])
    return data

def main():
    os.chdir("/home/zl_00986855/workspace/libopus/perf_test_dpsk")
    os.system(f"rm -rf {OUTDIR}")
    
    print("=" * 60)
    print(f"Multi-run comparison: {N_RUNS} independent measurements each")
    print("=" * 60)
    
    # Source runs
    print("\n--- Source ---")
    src_csvs = build_and_run("SRC", SRC_DIR)
    print(f"  {len(src_csvs)} runs completed")
    
    # Inline runs
    print("\n--- Inline ---")
    opt_csvs = build_and_run("OPT", OPT_DIR)
    print(f"  {len(opt_csvs)} runs completed")
    
    if len(src_csvs) < N_RUNS or len(opt_csvs) < N_RUNS:
        print("ERROR: not enough runs completed")
        return 1
    
    # Parse all runs
    src_data = [parse_csv(p) for p in src_csvs]
    opt_data = [parse_csv(p) for p in opt_csvs]
    
    # Get all test case names
    test_names = list(src_data[0].keys())
    
    print("\n" + "=" * 80)
    print(f"{'Test Case':<28} {'SRC avg':>10} {'SRC range':>12} {'OPT avg':>10} {'OPT range':>12} {'Delta%':>8} {'Verdict':>12}")
    print("=" * 80)
    
    good = 0
    marginal = 0
    noise = 0
    
    for name in test_names:
        src_vals = [d[name] for d in src_data]
        opt_vals = [d[name] for d in opt_data]
        
        src_avg = statistics.mean(src_vals)
        opt_avg = statistics.mean(opt_vals)
        src_range = max(src_vals) - min(src_vals)
        opt_range = max(opt_vals) - min(opt_vals)
        delta = (opt_avg / src_avg - 1) * 100
        
        # Determine if statistically meaningful
        # Simple heuristic: delta must exceed max range to be real
        max_noise = max(src_range, opt_range)
        delta_abs = abs(delta) * src_avg / 100  # convert to absolute us
        
        if delta < -1.0 and delta_abs > max_noise:
            verdict = "CONFIRMED"
            good += 1
        elif delta < -0.5:
            verdict = "MARGINAL"
            marginal += 1
        else:
            verdict = "NOISE"
            noise += 1
        
        print(f"{name:<28} {src_avg:>10.2f} {src_range:>10.2f} {opt_avg:>10.2f} {opt_range:>10.2f} {delta:>7.2f}% {verdict:>12}")
    
    print("=" * 80)
    print(f"Summary: {good} confirmed, {marginal} marginal, {noise} within noise")
    
    return 0

if __name__ == "__main__":
    exit(main())
