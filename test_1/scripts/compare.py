#!/usr/bin/env python3
"""Balanced paired comparison of two copyable opus_perf_bench executables."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import random
import statistics
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import bench


def paired_summary(pairs):
    """Geometric speedup and percentile bootstrap CI over independent pairs."""
    if not pairs or any(a <= 0 or b <= 0 or not math.isfinite(a + b) for a, b in pairs):
        raise ValueError("nonpositive or nonfinite paired times")
    logs = [math.log(a / b) for a, b in pairs]
    ratio = math.exp(statistics.mean(logs))
    ci = None
    if len(logs) >= 5:
        rng = random.Random(1729)
        draws = sorted(math.exp(statistics.mean(rng.choices(logs, k=len(logs))))
                       for _ in range(10000))
        ci = [draws[249], draws[9749]]
    verdict = "insufficient" if ci is None else (
        "faster" if ci[0] > 1 else "slower" if ci[1] < 1 else "inconclusive")
    return {"pairs": len(pairs), "baseline_mean_us": statistics.mean(a for a, _ in pairs),
            "candidate_mean_us": statistics.mean(b for _, b in pairs),
            "speedup_ratio": ratio, "paired_bootstrap_ci95": ci,
            "time_reduction_percent": 100 * (1 - 1 / ratio), "verdict": verdict}


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def run_one(executable, config, cpu, fixtures, case, out, label):
    command = [str(executable), "-c", str(config), "--cpu", str(cpu),
               "--fixtures-dir", str(fixtures), "--case", case, "--repeats", "1",
               "--lib-name", label, "-o", str(out)]
    proc = subprocess.run(command, capture_output=True, text=True)
    out.mkdir(parents=True, exist_ok=True)
    (out / "console.log").write_text(proc.stdout + "\n" + proc.stderr, encoding="utf-8")
    if proc.returncode:
        raise RuntimeError(f"{label}/{case} failed (exit {proc.returncode}); see {out / 'console.log'}")
    markers = [line.split("=", 1)[1] for line in proc.stdout.splitlines()
               if line.startswith("RESULT_JSON=")]
    if len(markers) != 1:
        raise RuntimeError(f"{label}/{case}: missing unique result artifact")
    report = json.loads(Path(markers[0]).read_text(encoding="utf-8"))
    if report.get("status") != "passed" or len(report.get("results", [])) != 1:
        raise RuntimeError(f"{label}/{case}: invalid/failed report")
    row = report["results"][0]
    if row.get("name") != case or row.get("status") != "passed" or len(row.get("runs", [])) != 1:
        raise RuntimeError(f"{label}/{case}: incomplete case or wrong sample count")
    run = row["runs"][0]
    frames = run["frames"]
    if frames <= 0 or len(run["samples_ns"]) != frames:
        raise RuntimeError("invalid native sample count")
    return report, row, run["total_ns"] / frames / 1000


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", required=True, type=Path)
    parser.add_argument("--candidate", required=True, type=Path)
    parser.add_argument("-c", "--config", required=True, type=Path)
    parser.add_argument("--cpu", required=True, type=int)
    parser.add_argument("--fixtures-dir", required=True, type=Path)
    parser.add_argument("--pairs", type=int, default=10)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--allow-output-differences", action="store_true",
                        help="Permit output hash differences after separate correctness/quality validation")
    parser.add_argument("-o", "--output-dir", type=Path, default=Path("output/compare"))
    args = parser.parse_args()
    if not 1 <= args.pairs <= 100:
        parser.error("--pairs must be in 1..100 (at least 5 for an interval)")
    if args.cpu not in os.sched_getaffinity(0):
        parser.error("--cpu is outside this process's allowed affinity")
    config = bench.load_config(args.config)
    for attr in ("baseline", "candidate", "config", "fixtures_dir"):
        setattr(args, attr, getattr(args, attr).resolve(strict=True))
    hashes = {label: sha256(getattr(args, label)) for label in ("baseline", "candidate")}
    config_hash = sha256(args.config)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="paired-", dir=args.output_dir.resolve()))
    schedule, collected = [], {case["name"]: [] for case in config["test_cases"]}
    workloads, differences, artifacts = {}, [], []
    rng = random.Random(args.seed)
    # Randomize which version leads the first pair, then alternate AB/BA.
    lead = rng.randrange(2)
    for i in range(args.pairs):
        cases = config["test_cases"][:]
        rng.shuffle(cases)
        labels = ["baseline", "candidate"]
        if (i + lead) % 2:
            labels.reverse()
        for case in cases:
            if sha256(args.config) != config_hash:
                raise RuntimeError("configuration changed during comparison")
            readings = {}
            safe_case = hashlib.sha256(case["name"].encode()).hexdigest()[:20]
            for label in labels:
                report, row, duration = run_one(getattr(args, label), args.config, args.cpu,
                    args.fixtures_dir, case["name"], out / f"pair-{i+1}" / safe_case / label, label)
                readings[label] = (report, row, duration)
                schedule.append({"pair": i + 1, "case": case["name"], "version": label})
                artifacts.append({"pair": i + 1, "case": case["name"], "version": label,
                                  "result": report})
            ar, a, at = readings["baseline"]
            br, b, bt = readings["candidate"]
            if a["case"] != b["case"] or a["case"] != case:
                raise RuntimeError("mismatched effective case parameters")
            key = "pcm_hash" if case["operation"] == "encode" else "packet_hash"
            if not a["runs"][0].get(key) or a["runs"][0][key] != b["runs"][0].get(key):
                raise RuntimeError(f"{case['name']}: A/B workloads differ ({key})")
            workload = a["runs"][0][key]
            if workloads.setdefault(case["name"], workload) != workload:
                raise RuntimeError(f"{case['name']}: workload changed between pairs")
            if a["runs"][0].get("output_hash") != b["runs"][0].get("output_hash"):
                differences.append({"pair": i + 1, "case": case["name"]})
                if not args.allow_output_differences:
                    raise RuntimeError(f"{case['name']}: decoded output differs; validate correctness/quality before using --allow-output-differences")
            if ar.get("config_sha256") != br.get("config_sha256"):
                raise RuntimeError("configuration fingerprint mismatch")
            collected[case["name"]].append((at, bt))
            print(f"pair {i+1}/{args.pairs} {case['name']}: A={at:.3f} B={bt:.3f} us/frame", flush=True)
    if any(sha256(getattr(args, label)) != value for label, value in hashes.items()):
        raise RuntimeError("a benchmark executable changed during the experiment")
    results = [{"name": name, **paired_summary(values)} for name, values in collected.items()]
    report = {"status": "passed", "cpu": args.cpu, "binary_sha256": hashes,
              "config_sha256": config_hash, "schedule": schedule,
              "pairs": collected, "results": results, "output_differences": differences,
              "run_artifacts": artifacts,
              "interval_note": "Percentile bootstrap over paired run mean ratios in log space; controlled-run estimate, not a guarantee of CPU isolation or fixed frequency."}
    text = json.dumps(report, indent=2, ensure_ascii=False, allow_nan=False)
    temp = out / "comparison.json.tmp"
    temp.write_text(text + "\n", encoding="utf-8")
    temp.replace(out / "comparison.json")
    print("\n| Case | A mean us | B mean us | Speedup A/B | 95% interval | Time reduction | Result |")
    print("|---|---:|---:|---:|---|---:|---|")
    for r in results:
        ci = r["paired_bootstrap_ci95"]
        interval = f"[{ci[0]:.3f}, {ci[1]:.3f}]" if ci else "insufficient pairs"
        print(f"| {r['name']} | {r['baseline_mean_us']:.3f} | {r['candidate_mean_us']:.3f} | {r['speedup_ratio']:.3f} | {interval} | {r['time_reduction_percent']:.2f}% | {r['verdict']} |")
    print(f"RESULT_JSON={out / 'comparison.json'}")
    if differences:
        print("WARNING: output hashes differ; speed comparisons require separate quality validation.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError, KeyError, bench.BenchError) as error:
        print(f"comparison failed: {error}", file=sys.stderr)
        sys.exit(1)
