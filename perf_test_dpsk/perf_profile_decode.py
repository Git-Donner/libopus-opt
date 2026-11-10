#!/usr/bin/env python3
"""Profile CELT decode path with perf to measure sub-function time distribution."""
import os, subprocess, json

PERF_DATA = "/tmp/celt_decode_perf.data"
PERF_REPORT = "/tmp/celt_decode_perf_report.txt"
CONFIG = "celt_decode_prof.json"
OUT = "output/perf_analysis"

def run_cmd(cmd, check=True):
    print(f"  $ {cmd}")
    return subprocess.run(cmd, shell=True, capture_output=True, text=True)

def main():
    os.chdir("/home/zl_00986855/workspace/libopus/perf_test_dpsk")
    os.system(f"rm -rf {OUT}")
    
    # Build Source version
    print("=== Build Source ===")
    r = run_cmd("make clean >/dev/null 2>&1; make OPUS_DIR=../opus-1.6.1-Source >/dev/null 2>&1", check=False)
    
    # Run perf record on decode-only benchmark
    print("\n=== Perf Record (decode_only, CELT 64k + 128k) ===")
    cmd = f"perf record -g -o {PERF_DATA} -- ./opus_perf_bench -c {CONFIG} --lib-name PROF --cpu 0 -C --no-text -o {OUT}"
    r = run_cmd(cmd, check=False)
    print(f"  stdout: {r.stdout.strip()}")
    if r.returncode != 0:
        print(f"  stderr: {r.stderr.strip()}")
    
    # perf report - function level
    print("\n=== Perf Report (functions sorted by overhead) ===")
    cmd = f"perf report -i {PERF_DATA} --stdio --sort=comm,dso,symbol -n --percent-limit 0.1 2>&1 | head -80"
    r = run_cmd(cmd, check=False)
    print(r.stdout)
    
    # perf report - callgraph focused on celt decode
    print("\n=== Perf Callgraph (decode_pulses, normalise_residual, exp_rotation) ===")
    for func in ["decode_pulses", "normalise_residual", "exp_rotation", "exp_rotation1", 
                 "extract_collapse_mask", "alg_unquant", "quant_partition",
                 "opus_decode", "celt_decode_with_ec", "celt_decoder_main"]:
        cmd = f"perf report -i {PERF_DATA} --stdio -g --symbol-filter={func} 2>&1 | head -30"
        r = run_cmd(cmd, check=False)
        if r.stdout.strip():
            print(f"\n--- {func} ---")
            print(r.stdout[:1500])
    
    return 0

if __name__ == "__main__":
    exit(main())
