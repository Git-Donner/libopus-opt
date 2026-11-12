"""Regression checks for workload identity, six paths and invalid-result rejection."""
import copy
import importlib.util
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import bench

spec = importlib.util.spec_from_file_location("comparison", ROOT / "scripts/compare.py")
comparison = importlib.util.module_from_spec(spec)
spec.loader.exec_module(comparison)


class StatisticsTests(unittest.TestCase):
    def test_speedup_is_not_time_reduction(self):
        result = comparison.paired_summary([(110, 100)] * 6)
        self.assertAlmostEqual(result["speedup_ratio"], 1.1)
        self.assertAlmostEqual(result["time_reduction_percent"], 100 / 11)
        self.assertEqual(result["verdict"], "faster")

    def test_insufficient_repeats_have_no_confidence_interval(self):
        result = comparison.paired_summary([(1, 1)])
        self.assertIsNone(result["paired_bootstrap_ci95"])
        self.assertEqual(result["verdict"], "insufficient")

    def test_aa_and_regression(self):
        self.assertEqual(comparison.paired_summary([(1, 1)] * 6)["verdict"], "inconclusive")
        self.assertEqual(comparison.paired_summary([(1, 2)] * 6)["verdict"], "slower")
        with self.assertRaises(ValueError):
            comparison.paired_summary([(1, 0)])


class IntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.executable = Path(os.environ.get("BENCH_EXECUTABLE", ROOT / "opus_perf_bench")).resolve()
        if not cls.executable.exists():
            raise RuntimeError("build opus_perf_bench before running the tests")
        cls.cpu = min(os.sched_getaffinity(0))
        cls.temp = tempfile.TemporaryDirectory(prefix="opus-bench-tests-")
        cls.work = Path(cls.temp.name)
        cls.config = json.loads((ROOT / "configs/six_scenarios.json").read_text())
        cls.config["repeats"] = 3
        for case in cls.config["test_cases"]:
            case["frames"] = 32
            case["warmup"] = 8
        cls.cfg = cls.work / "config.json"
        cls.cfg.write_text(json.dumps(cls.config))
        cls.fixtures = cls.work / "fixtures"
        result = cls.invoke("--prepare-fixtures")
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    @classmethod
    def invoke(cls, *extra, executable=None, config=None, output=None):
        return subprocess.run([str(executable or cls.executable), "-c", str(config or cls.cfg),
                               "--cpu", str(cls.cpu), "--fixtures-dir", str(cls.fixtures),
                               "-o", str(output or cls.work / "results"), *extra],
                              capture_output=True, text=True, timeout=120)

    def read_report(self, result):
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        markers = [line.split("=", 1)[1] for line in result.stdout.splitlines()
                   if line.startswith("RESULT_JSON=")]
        self.assertEqual(len(markers), 1, result.stdout)
        return json.loads(Path(markers[0]).read_text())

    def test_all_six_paths_and_decode_has_no_encode(self):
        report = self.read_report(self.invoke())
        self.assertEqual(report["status"], "passed")
        self.assertEqual(len(report["results"]), 6)
        observed = set()
        for result in report["results"]:
            case = result["case"]
            observed.add((case["mode"], case["operation"]))
            self.assertEqual(result["status"], "passed")
            verification = result["verification"]
            counts = verification["operator_calls"]
            for codec in (["silk", "celt"] if case["mode"] == "hybrid" else [case["mode"]]):
                self.assertGreaterEqual(counts[f"{codec}_{case['operation']}"], case["frames"])
            self.assertEqual(len(result["runs"]), 3)
            for run in result["runs"]:
                self.assertEqual(run["frames"], 32)
                self.assertEqual(len(run["samples_ns"]), 32)
                self.assertEqual(run["total_ns"], sum(run["samples_ns"]))
                other = "encode" if case["operation"] == "decode" else "decode"
                self.assertEqual(run[f"measured_{other}_calls"], 0)
                self.assertEqual(run[f"measured_{case['operation']}_calls"], 32)
                self.assertTrue(all(value > 0 for value in run["samples_ns"]))
                self.assertEqual(run["packet_mode_counts"].get(case["mode"]), 32)
        self.assertEqual(observed, {(m, op) for m in ["silk", "celt", "hybrid"] for op in ["encode", "decode"]})

    def test_config_errors_are_fatal(self):
        mutations = [
            lambda c: c["test_cases"][0].update(frames=0),
            lambda c: c["test_cases"][0].update(frames=True),
            lambda c: c["test_cases"][0].update(impulse_period=800),
            lambda c: c["test_cases"][0].update(operation="encdoe"),
            lambda c: c["test_cases"][0].update(channels=3),
            lambda c: c["test_cases"][0].update(frame_size=123),
            lambda c: c["test_cases"][1].update(name=c["test_cases"][0]["name"]),
        ]
        for mutation in mutations:
            config = copy.deepcopy(self.config)
            mutation(config)
            path = self.work / "invalid.json"
            path.write_text(json.dumps(config))
            with self.subTest(config=config["test_cases"][0]):
                self.assertNotEqual(self.invoke(config=path).returncode, 0)
        path.write_text('{"test_cases": [')
        self.assertNotEqual(self.invoke(config=path).returncode, 0)

    def test_cpu_is_required_and_outside_affinity_is_fatal(self):
        missing = subprocess.run([str(self.executable), "-c", str(self.cfg)], capture_output=True)
        self.assertNotEqual(missing.returncode, 0)
        disallowed = max(os.sched_getaffinity(0)) + 1000
        result = self.invoke("--cpu", str(disallowed))
        self.assertNotEqual(result.returncode, 0)

    def test_missing_corpus_cannot_be_silently_generated(self):
        result = self.invoke("--fixtures-dir", str(self.work / "absent"))
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.work / "absent").exists())

    def test_corrupt_corpus_is_rejected(self):
        corpus_files = [p for p in self.fixtures.rglob("*") if p.is_file() and p.suffix != ".json"]
        self.assertTrue(corpus_files)
        path = corpus_files[0]
        original = path.read_bytes()
        try:
            path.write_bytes(original[:-1] + bytes([original[-1] ^ 1]))
            self.assertNotEqual(self.invoke().returncode, 0)
        finally:
            path.write_bytes(original)

    def test_copyable_binary_and_unique_reports(self):
        copied = self.work / "binary_tmp/github-01"
        copied.parent.mkdir(exist_ok=True)
        shutil.copy2(self.executable, copied)
        case = self.config["test_cases"][0]["name"]
        first = self.invoke("--case", case, "--repeats", "1", executable=copied)
        second = self.invoke("--case", case, "--repeats", "1", executable=copied)
        a, b = self.read_report(first), self.read_report(second)
        self.assertEqual(a["results"][0]["runs"][0]["pcm_hash"], b["results"][0]["runs"][0]["pcm_hash"])
        self.assertNotEqual(first.stdout.split("RESULT_JSON=")[-1], second.stdout.split("RESULT_JSON=")[-1])


if __name__ == "__main__":
    unittest.main()
