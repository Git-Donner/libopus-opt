#!/usr/bin/env python3
"""Strict configuration and reporting for the native Opus six-scenario worker.

Python never runs inside a timed codec interval.  The copyable zipapp contains
an uninstrumented timing worker and a separate operator-verification worker.
"""
from __future__ import annotations

import argparse
import contextlib
import csv
import datetime as dt
import hashlib
import io
import json
import math
import os
from pathlib import Path
import platform
import random
import statistics
import subprocess
import sys
import tempfile
import uuid
import zipfile


SCHEMA_VERSION = 2
MODES = {"silk", "celt", "hybrid"}
OPERATIONS = {"encode", "decode"}
SIGNALS = {"mixed", "sine", "noise", "impulse"}
BANDWIDTHS = {"WIDEBAND", "SUPERWIDEBAND", "FULLBAND"}
DEFAULTS = {
    "sample_rate": 48000, "channels": 1, "frame_size": 960,
    "complexity": 5, "frames": 1000, "warmup": 100, "signal": "mixed",
}
CASE_KEYS = {"name", "description", "mode", "module", "operation", "sample_rate",
             "channels", "frame_size", "bitrate", "complexity", "bandwidth",
             "frames", "iterations", "warmup", "signal"}
TOP_KEYS = {"schema_version", "name", "description", "repeats", "seed", "test_cases"}
PROBE_KEYS = ("silk_encode", "celt_encode", "silk_decode", "celt_decode")


class BenchError(Exception):
    """Invalid experiment, execution failure, or invalid result."""


def canonical_json(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False,
                      allow_nan=False)


def digest(value):
    return hashlib.sha256(canonical_json(value).encode()).hexdigest()


def file_sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def _pairs(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise BenchError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def _constant(value):
    raise BenchError(f"non-finite JSON constant: {value}")


def read_json(path):
    try:
        return json.loads(Path(path).read_text(encoding="utf-8"),
                          object_pairs_hook=_pairs, parse_constant=_constant)
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise BenchError(f"cannot read JSON {path}: {exc}") from exc


def _integer(value, name, low, high):
    if type(value) is not int or not low <= value <= high:
        raise BenchError(f"{name} must be an integer in [{low}, {high}]")
    return value


def _unknown(obj, allowed, where):
    extra = set(obj) - allowed
    if extra:
        raise BenchError(f"{where}: unsupported fields: {', '.join(sorted(extra))}")


def validate_config(raw):
    if not isinstance(raw, dict):
        raise BenchError("configuration root must be an object")
    _unknown(raw, TOP_KEYS, "configuration")
    if raw.get("schema_version") != SCHEMA_VERSION or type(raw.get("schema_version")) is not int:
        raise BenchError("schema_version must be 2; legacy broad/packet-loss configs are not supported")
    cfg = {"schema_version": SCHEMA_VERSION, "name": raw.get("name", "six-scenarios"),
           "description": raw.get("description", ""),
           "repeats": _integer(raw.get("repeats", 7), "repeats", 1, 1000),
           "seed": _integer(raw.get("seed", 42), "seed", 1, 4294967295)}
    for key in ("name", "description"):
        if not isinstance(cfg[key], str) or len(cfg[key]) > 512:
            raise BenchError(f"{key} must be a string of at most 512 characters")
    cases = raw.get("test_cases")
    if not isinstance(cases, list) or not 1 <= len(cases) <= 256:
        raise BenchError("test_cases must contain between 1 and 256 cases")
    names = set()
    cfg["test_cases"] = []
    for number, item in enumerate(cases, 1):
        if not isinstance(item, dict):
            raise BenchError(f"case {number} must be an object")
        _unknown(item, CASE_KEYS, f"case {number}")
        case = dict(DEFAULTS)
        case.update(item)
        name = case.get("name")
        if not isinstance(name, str) or not name or len(name) > 96 or any(ord(c) < 32 for c in name):
            raise BenchError(f"case {number}: name must contain 1..96 printable characters")
        if name in names:
            raise BenchError(f"duplicate case name: {name}")
        names.add(name)
        description = case.setdefault("description", "")
        if not isinstance(description, str) or len(description) > 1024:
            raise BenchError(f"{name}: description must be a string of at most 1024 characters")
        if "mode" in item and "module" in item:
            raise BenchError(f"{name}: use mode or module, not both")
        case["mode"] = case.pop("module", case.get("mode"))
        if not isinstance(case["mode"], str) or case["mode"] not in MODES:
            raise BenchError(f"{name}: mode must be silk, celt, or hybrid")
        aliases = {"encode_only": "encode", "decode_only": "decode"}
        operation = case.get("operation")
        if not isinstance(operation, str):
            raise BenchError(f"{name}: operation must be encode or decode")
        case["operation"] = aliases.get(operation, operation)
        if case["operation"] not in OPERATIONS:
            raise BenchError(f"{name}: operation must be encode or decode; combined timing is not a targeted scenario")
        if "frames" in item and "iterations" in item:
            raise BenchError(f"{name}: use frames or iterations, not both")
        if "iterations" in item:
            case["frames"] = case.pop("iterations")
        fs = _integer(case["sample_rate"], f"{name}.sample_rate", 8000, 48000)
        if fs not in (8000, 12000, 16000, 24000, 48000):
            raise BenchError(f"{name}: unsupported Opus sample rate")
        _integer(case["channels"], f"{name}.channels", 1, 2)
        frame = _integer(case["frame_size"], f"{name}.frame_size", 1, 2880)
        # Purposefully use ordinary 10/20 ms frames. Wider codec feature testing
        # belongs in the codec's correctness suite, not this targeted benchmark.
        if frame not in (fs // 100, fs // 50):
            raise BenchError(f"{name}: use a 10 ms or 20 ms frame for this targeted benchmark")
        mode = case["mode"]
        default_bitrate = {"silk": 16000, "celt": 64000, "hybrid": 32000}[mode]
        case.setdefault("bitrate", default_bitrate)
        _integer(case["bitrate"], f"{name}.bitrate", 6000, 256000)
        _integer(case["complexity"], f"{name}.complexity", 0, 10)
        _integer(case["frames"], f"{name}.frames", 1, 100000)
        _integer(case["warmup"], f"{name}.warmup", 1, 10000)
        if (case["frames"] + case["warmup"]) * frame * case["channels"] > 64000000:
            raise BenchError(f"{name}: PCM corpus exceeds 64 million samples; reduce frames")
        case.setdefault("bandwidth", "WIDEBAND" if mode == "silk" else "FULLBAND")
        if not isinstance(case["bandwidth"], str) or case["bandwidth"] not in BANDWIDTHS:
            raise BenchError(f"{name}: unsupported bandwidth")
        if mode == "silk" and case["bandwidth"] != "WIDEBAND":
            raise BenchError(f"{name}: SILK cases require WIDEBAND")
        if mode == "hybrid" and (fs < 24000 or case["bandwidth"] == "WIDEBAND"):
            raise BenchError(f"{name}: HYBRID requires 24/48 kHz and SUPERWIDEBAND/FULLBAND")
        if case["bandwidth"] == "FULLBAND" and fs != 48000:
            raise BenchError(f"{name}: FULLBAND requires 48 kHz input")
        if case["bandwidth"] == "SUPERWIDEBAND" and fs < 24000:
            raise BenchError(f"{name}: SUPERWIDEBAND requires at least 24 kHz input")
        if case["bandwidth"] == "WIDEBAND" and fs < 16000:
            raise BenchError(f"{name}: WIDEBAND requires at least 16 kHz input")
        if not isinstance(case["signal"], str) or case["signal"] not in SIGNALS:
            raise BenchError(f"{name}: signal must be mixed, sine, noise, or impulse")
        cfg["test_cases"].append(case)
    return cfg


def load_config(path):
    return validate_config(read_json(path))


def fixture_parameters(case, seed):
    return {"format_version": 2, "seed": seed, **{
        key: case[key] for key in ("mode", "sample_rate", "channels", "frame_size",
                                  "bitrate", "complexity", "bandwidth", "frames", "warmup", "signal")}}


def fixture_key(case, seed):
    return f"{case['mode']}-{digest(fixture_parameters(case, seed))[:20]}"


def percentile(values, p):
    if not values:
        raise BenchError("cannot compute a percentile of zero samples")
    ordered = sorted(values)
    index = (len(ordered) - 1) * p
    lo = int(index)
    hi = min(lo + 1, len(ordered) - 1)
    return ordered[lo] + (ordered[hi] - ordered[lo]) * (index - lo)


def bootstrap_mean_ci(values, seed=42):
    if len(values) < 3:
        return None
    rng = random.Random(seed)
    n = len(values)
    means = [sum(rng.choices(values, k=n)) / n for _ in range(4000)]
    return [percentile(means, 0.025), percentile(means, 0.975)]


def summarize_runs(runs, case):
    if not runs:
        raise BenchError("no valid runs to summarize")
    means = [run["total_ns"] / run["frames"] / 1000.0 for run in runs]
    all_samples = [ns / 1000.0 for run in runs for ns in run["samples_ns"]]
    mean = statistics.mean(means)
    return {"n_runs": len(runs), "mean_us": mean, "median_us": statistics.median(means),
            "mean_us_ci95": bootstrap_mean_ci(means), "min_run_mean_us": min(means),
            "max_run_mean_us": max(means),
            "cv_percent": statistics.stdev(means) / mean * 100 if len(means) > 1 else None,
            "frame_p50_us": percentile(all_samples, .50),
            "frame_p95_us": percentile(all_samples, .95),
            "frame_p99_us": percentile(all_samples, .99), "frame_max_us": max(all_samples),
            "audio_us_per_frame": case["frame_size"] / case["sample_rate"] * 1e6,
            "realtime_x": case["frame_size"] / case["sample_rate"] * 1e6 / mean,
            "deadline_miss_count": sum(us > case["frame_size"] / case["sample_rate"] * 1e6 for us in all_samples),
            "measured_frames": len(all_samples)}


def validate_result(result, case, verification=False, cpu=None):
    if not isinstance(result, dict) or result.get("status") != "passed":
        raise BenchError(f"worker did not pass: {result}")
    for field in ("mode", "operation", "frames", "warmup", "sample_rate", "frame_size", "channels"):
        expected = case[field]
        if result.get(field) != expected:
            raise BenchError(f"worker {field}={result.get(field)!r}, expected {expected!r}")
    if cpu is not None and result.get("cpu") != cpu:
        raise BenchError("worker did not confirm the requested CPU")
    route = result.get("packet_mode_counts")
    if not isinstance(route, dict) or route.get(case["mode"]) != case["frames"] or any(
            count for mode, count in route.items() if mode != case["mode"]):
        raise BenchError("not every measured packet matched the requested mode")
    enc = result.get("measured_encode_calls")
    dec = result.get("measured_decode_calls")
    if case["operation"] == "encode" and (enc != case["frames"] or dec != 0):
        raise BenchError("encode measurement included the wrong API calls")
    if case["operation"] == "decode" and (dec != case["frames"] or enc != 0):
        raise BenchError("decode measurement included the wrong API calls")
    if verification:
        raw = result.get("operator_calls")
        if isinstance(raw, list) and len(raw) == 4:
            raw = dict(zip(PROBE_KEYS, raw))
        if not isinstance(raw, dict) or set(PROBE_KEYS) - set(raw):
            raise BenchError("operator verification did not return actual call counters")
        for key in PROBE_KEYS:
            if type(raw[key]) is not int or raw[key] < 0:
                raise BenchError("invalid operator call counter")
        suffix = case["operation"]
        required = [f"{mode}_{suffix}" for mode in ("silk", "celt")
                    if case["mode"] in (mode, "hybrid")]
        if any(raw[key] < case["frames"] for key in required):
            raise BenchError("actual operator calls were not observed; use a non-LTO verification build with visible symbols")
        wrong = "decode" if suffix == "encode" else "encode"
        if raw[f"silk_{wrong}"] or raw[f"celt_{wrong}"]:
            raise BenchError("verification included operators from the wrong phase")
        if case["mode"] in ("silk", "celt"):
            other = "celt" if case["mode"] == "silk" else "silk"
            if raw[f"{other}_{suffix}"]:
                raise BenchError("exclusive mode called the other codec's operator")
        result["operator_calls"] = raw
    else:
        samples = result.get("samples_ns")
        if not isinstance(samples, list) or len(samples) != case["frames"] or any(
                type(x) not in (int, float) or not math.isfinite(x) or x <= 0 for x in samples):
            raise BenchError("invalid or incomplete timing samples")
        for key in ("total_ns", "thread_cpu_ns"):
            value = result.get(key)
            if type(value) not in (int, float) or not math.isfinite(value) or value <= 0:
                raise BenchError(f"invalid {key}")
        if abs(sum(samples) - result["total_ns"]) > max(1.0, result["total_ns"] * 1e-9):
            raise BenchError("total_ns does not equal the sum of timed API samples")
    for key in ("pcm_hash", "packet_hash", "output_hash"):
        if not isinstance(result.get(key), str) or not result[key]:
            raise BenchError(f"worker did not provide {key}")
    return result


def atomic_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    data = json.dumps(value, indent=2, ensure_ascii=False, allow_nan=False) + "\n"
    fd, tmp = tempfile.mkstemp(prefix="." + path.name, dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, path)
    finally:
        if os.path.exists(tmp):
            os.unlink(tmp)


@contextlib.contextmanager
def workers(directory=None):
    if directory:
        root = Path(directory).resolve()
        metadata = read_json(root / "build_metadata.json")
        yield root, metadata
        return
    archive = Path(sys.argv[0]).resolve()
    if not zipfile.is_zipfile(archive):
        raise BenchError("run ./opus_perf_bench built by make, or specify --worker-dir for development")
    with zipfile.ZipFile(archive) as zf, tempfile.TemporaryDirectory(prefix="opus-bench-") as tmp:
        root = Path(tmp)
        for name in ("worker", "verify_worker", "build_metadata.json"):
            (root / name).write_bytes(zf.read(name))
        for name in ("worker", "verify_worker"):
            (root / name).chmod(0o700)
        yield root, read_json(root / "build_metadata.json")


def worker_command(worker, case, cpu, seed, fixture=None, prepare=False, verify=False):
    cmd = [str(worker)]
    fields = ("mode", "operation", "sample_rate", "channels", "frame_size", "bitrate",
              "complexity", "bandwidth", "frames", "warmup", "signal")
    for field in fields:
        cmd += ["--" + field.replace("_", "-"), str(case[field])]
    cmd += ["--seed", str(seed), "--cpu", str(cpu)]
    if fixture is not None:
        cmd += ["--fixture", str(fixture)]
    if prepare:
        cmd.append("--prepare")
    if verify:
        cmd.append("--verify-only")
    return cmd


def invoke_worker(cmd):
    try:
        run = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                             timeout=1200, check=False)
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise BenchError(f"native worker failed: {exc}") from exc
    if run.returncode:
        raise BenchError(f"native worker exit {run.returncode}: {run.stderr.strip() or run.stdout.strip()}")
    try:
        result = json.loads(run.stdout, object_pairs_hook=_pairs, parse_constant=_constant)
    except (json.JSONDecodeError, BenchError) as exc:
        raise BenchError(f"invalid worker JSON: {run.stdout[:500]!r}") from exc
    if not isinstance(result, dict) or result.get("status") != "passed":
        raise BenchError(f"native worker did not pass: {result}")
    return result


def fixture_paths(directory, case, seed):
    key = fixture_key(case, seed)
    return Path(directory) / (key + ".opusb"), Path(directory) / (key + ".json")


def checked_fixture(directory, case, seed):
    path, manifest_path = fixture_paths(directory, case, seed)
    if not path.is_file() or not manifest_path.is_file():
        raise BenchError(f"missing fixed decoder corpus {path}; prepare it once using the baseline: --prepare-fixtures")
    manifest = read_json(manifest_path)
    if not isinstance(manifest, dict):
        raise BenchError(f"fixture manifest must be an object: {manifest_path}")
    if manifest.get("parameters") != fixture_parameters(case, seed):
        raise BenchError(f"fixture parameters do not match: {manifest_path}")
    if manifest.get("sha256") != file_sha256(path):
        raise BenchError(f"fixture checksum mismatch: {path}")
    return path, manifest


def prepare_fixtures(root, metadata, cases, args, seed):
    directory = Path(args.fixtures_dir).resolve()
    directory.mkdir(parents=True, exist_ok=True)
    prepared = []
    seen = set()
    for case in cases:
        if case["operation"] != "decode":
            continue
        key = fixture_key(case, seed)
        if key in seen:
            continue
        seen.add(key)
        path, manifest_path = fixture_paths(directory, case, seed)
        if path.exists() or manifest_path.exists():
            checked_fixture(directory, case, seed)
            print(f"REUSE {path}")
            prepared.append(str(path))
            continue
        temp = directory / ("." + key + "-" + uuid.uuid4().hex + ".tmp")
        try:
            result = invoke_worker(worker_command(root / "worker", case, args.cpu, seed,
                                                  fixture=temp, prepare=True))
            manifest = {"schema_version": SCHEMA_VERSION,
                        "parameters": fixture_parameters(case, seed),
                        "sha256": file_sha256(temp), "prepared_by": metadata,
                        "worker_result": result}
            # link fails rather than overwriting an existing immutable corpus.
            os.link(temp, path)
            atomic_json(manifest_path, manifest)
            print(f"PREPARED {path}")
            prepared.append(str(path))
        finally:
            temp.unlink(missing_ok=True)
    if not prepared:
        raise BenchError("selected cases contain no decode scenario to prepare")
    return prepared


def write_csv(path, results):
    fields = ["case", "mode", "operation", "status", "runs", "mean_us", "median_us",
              "mean_ci95_low_us", "mean_ci95_high_us", "frame_p95_us", "cv_percent", "realtime_x"]
    buf = io.StringIO(newline="")
    writer = csv.DictWriter(buf, fieldnames=fields)
    writer.writeheader()
    for result in results:
        case = result["case"]
        row = {"case": case["name"], "mode": case["mode"], "operation": case["operation"],
               "status": result["status"]}
        summary = result.get("summary")
        if summary:
            row.update({key: summary[key] for key in ("mean_us", "median_us", "frame_p95_us", "cv_percent", "realtime_x")})
            row["runs"] = summary["n_runs"]
            ci = summary["mean_us_ci95"]
            row["mean_ci95_low_us"] = ci[0] if ci else ""
            row["mean_ci95_high_us"] = ci[1] if ci else ""
        writer.writerow(row)
    path.write_text(buf.getvalue(), encoding="utf-8")


def print_table(results):
    headers = ("Case", "Mode", "Op", "Verified", "Run median us", "Mean 95% CI us", "Frame P95 us", "Run CV%", "Runs")
    rows = []
    for result in results:
        case = result["case"]
        summary = result.get("summary")
        if summary:
            ci = summary["mean_us_ci95"]
            rows.append((case["name"], case["mode"].upper(), case["operation"], "PASS",
                         f"{summary['median_us']:.3f}",
                         f"[{ci[0]:.3f}, {ci[1]:.3f}]" if ci else "insufficient runs",
                         f"{summary['frame_p95_us']:.3f}",
                         f"{summary['cv_percent']:.2f}" if summary["cv_percent"] is not None else "n/a",
                         str(summary["n_runs"])))
        else:
            rows.append((case["name"], case["mode"].upper(), case["operation"], "FAIL", "-", "-", "-", "-", "0"))
    widths = [max(len(str(row[i])) for row in (headers, *rows)) for i in range(len(headers))]
    def line(row):
        return " | ".join(str(value).ljust(widths[i]) for i, value in enumerate(row))
    print(line(headers))
    print("-+-".join("-" * width for width in widths))
    for row in rows:
        print(line(row))
    for result in results:
        if result["status"] != "passed":
            print(f"FAIL {result['case']['name']}: {result['error']}", file=sys.stderr)
        for warning in result.get("warnings", []):
            print(f"WARN {result['case']['name']}: {warning}")
    print("Run median = median of independent-run mean API costs; CI = bootstrap interval for the mean.")
    print("Frame P95 describes observed per-call wall time; no outliers were removed.")


def run_benchmark(root, metadata, cfg, cases, args):
    now = dt.datetime.now(dt.timezone.utc)
    run_id = now.strftime("%Y%m%dT%H%M%S") + "-" + uuid.uuid4().hex[:12]
    directory = Path(args.output_dir).resolve() / ("run-" + run_id)
    directory.mkdir(parents=True, exist_ok=False)
    results = []
    report = {"schema_version": SCHEMA_VERSION, "run_id": run_id, "status": "failed",
              "started_at_utc": now.isoformat(), "lib_name": args.lib_name,
              "build_metadata": metadata, "config": cfg, "config_sha256": digest(cfg),
              "selected_cases": [case["name"] for case in cases],
              "cpu": args.cpu, "allowed_cpu_affinity": sorted(os.sched_getaffinity(0)),
              "host": {"platform": platform.platform(), "machine": platform.machine()},
              "command": sys.argv, "results": results,
              "statistics": {"unit": "microseconds per frame", "ci_method": "4000-resample bootstrap of independent-run means; no CI for fewer than 3 runs",
                             "timing_scope": "codec API only; no input generation, file I/O, correctness checks or probe instrumentation",
                             "diagnostic_scope": "loop_ns and thread_cpu_ns include untimed harness checks/hashes; they are diagnostics, not codec-only CPU time",
                             "limitations": "Affinity is verified, but does not ensure an idle core or fixed CPU frequency. CI describes these runs, not all machines or workloads."}}
    for case in cases:
        result = {"name": case["name"], "case": case, "status": "failed", "runs": []}
        results.append(result)
        try:
            fixture = None
            if case["operation"] == "decode":
                fixture, manifest = checked_fixture(args.fixtures_dir, case, cfg["seed"])
                result["fixture"] = {"path": str(fixture), "manifest": manifest}
            verification = invoke_worker(worker_command(root / "verify_worker", case, args.cpu,
                                                         cfg["seed"], fixture, verify=True))
            result["verification"] = validate_result(verification, case, verification=True, cpu=args.cpu)
            for repeat in range(cfg["repeats"]):
                native = invoke_worker(worker_command(root / "worker", case, args.cpu, cfg["seed"], fixture))
                validate_result(native, case, cpu=args.cpu)
                # Same data and deterministic codec output across independent runs.
                for key in ("pcm_hash", "packet_hash", "output_hash"):
                    if native[key] != verification[key]:
                        raise BenchError(f"{key} differs between verified and timing execution")
                native["repeat"] = repeat + 1
                result["runs"].append(native)
            result["summary"] = summarize_runs(result["runs"], case)
            result["warnings"] = []
            if any(run["total_ns"] < 10000000 for run in result["runs"]):
                result["warnings"].append("less than 10 ms of timed API work in a run; increase frames for small-regression measurements")
            if result["summary"]["cv_percent"] is not None and result["summary"]["cv_percent"] > 5:
                result["warnings"].append("run mean CV exceeds 5%; check CPU contention/frequency and repeat the experiment")
            result["status"] = "passed"
        except (BenchError, OSError) as exc:
            result["error"] = str(exc)
            # Partially completed runs remain diagnostic data, not an aggregate.
        atomic_json(directory / "results.json", report)
    report["status"] = "passed" if all(r["status"] == "passed" for r in results) else "failed"
    report["finished_at_utc"] = dt.datetime.now(dt.timezone.utc).isoformat()
    atomic_json(directory / "results.json", report)
    if args.csv:
        write_csv(directory / "summary.csv", results)
    if not args.no_text:
        print(f"Library: {args.lib_name}; CPU: {args.cpu}; repeats: {cfg['repeats']}")
        print_table(results)
    print(f"RESULT_JSON={directory / 'results.json'}")
    return 0 if report["status"] == "passed" else 1


def parser():
    p = argparse.ArgumentParser(description="CPU-bound SILK/CELT/HYBRID encode/decode benchmark (schema v2)")
    p.add_argument("-c", "--config", required=True)
    p.add_argument("--cpu", type=int, required=True, help="required logical CPU ID, checked against the allowed affinity mask")
    p.add_argument("--fixtures-dir", default="fixtures", help="shared immutable baseline decoder packet corpus directory")
    p.add_argument("--prepare-fixtures", action="store_true", help="prepare baseline packet corpora without running timings")
    p.add_argument("--case", action="append", default=[], help="select a case name; repeat to select several")
    p.add_argument("--repeats", type=int, help="override independent-run count; one run has no confidence interval")
    p.add_argument("--seed", type=int, help="override deterministic input seed (also changes fixture identity)")
    p.add_argument("-l", "--lib-name", default="opus")
    p.add_argument("-o", "--output-dir", default="output")
    p.add_argument("-C", "--csv", action="store_true", help="also save a compact CSV summary")
    p.add_argument("--no-text", action="store_true", help="suppress console table (JSON is always saved)")
    p.add_argument("--worker-dir", help=argparse.SUPPRESS)
    return p


def main(argv=None):
    args = parser().parse_args(argv)
    try:
        if not hasattr(os, "sched_getaffinity"):
            raise BenchError("this benchmark requires Linux CPU affinity support")
        allowed = os.sched_getaffinity(0)
        if args.cpu < 0 or args.cpu not in allowed:
            raise BenchError(f"CPU {args.cpu} is unavailable; allowed CPUs: {sorted(allowed)}")
        cfg = load_config(args.config)
        if args.repeats is not None:
            cfg["repeats"] = _integer(args.repeats, "--repeats", 1, 1000)
        if args.seed is not None:
            cfg["seed"] = _integer(args.seed, "--seed", 1, 4294967295)
        requested = set(args.case)
        known = {case["name"] for case in cfg["test_cases"]}
        if requested - known:
            raise BenchError("unknown selected cases: " + ", ".join(sorted(requested - known)))
        cases = [case for case in cfg["test_cases"] if not requested or case["name"] in requested]
        with workers(args.worker_dir) as (root, metadata):
            if args.prepare_fixtures:
                prepare_fixtures(root, metadata, cases, args, cfg["seed"])
                return 0
            return run_benchmark(root, metadata, cfg, cases, args)
    except (BenchError, OSError, ValueError, zipfile.BadZipFile, KeyError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
