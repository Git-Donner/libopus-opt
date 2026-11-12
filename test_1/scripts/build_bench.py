#!/usr/bin/env python3
"""Build and atomically package a copyable benchmark; no codec modifications."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import zipapp

ROOT = Path(__file__).resolve().parents[1]
SYMBOLS = ("silk_Encode", "celt_encode_with_ec", "silk_Decode",
           "celt_decode_with_ec", "celt_decode_with_ec_dred")


def sha(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def capture(command, cwd=None):
    return subprocess.check_output(command, cwd=cwd, text=True, stderr=subprocess.STDOUT).strip()


def cmake_defines(build):
    """Obtain actual opus target -D/-U flags, not guesses from CPU architecture."""
    commands = build / "compile_commands.json"
    if commands.exists():
        for item in json.loads(commands.read_text()):
            if Path(item["file"]).name == "opus_decoder.c":
                parts = item.get("arguments") or shlex.split(item["command"])
                return [p for p in parts if p.startswith(("-D", "-U"))]
    flags = build / "CMakeFiles/opus.dir/flags.make"
    if flags.exists():
        for line in flags.read_text().splitlines():
            if line.startswith("C_DEFINES ="):
                return shlex.split(line.split("=", 1)[1])
    cache = build / "CMakeCache.txt"
    settings = {}
    if cache.exists():
        for line in cache.read_text().splitlines():
            if not line.startswith(("#", "//")) and "=" in line and ":" in line.split("=", 1)[0]:
                key, value = line.split("=", 1)
                settings[key.split(":", 1)[0]] = value
    program = settings.get("CMAKE_MAKE_PROGRAM")
    if (build / "build.ninja").exists() and program:
        for line in capture([program, "-C", str(build), "-t", "commands"]).splitlines():
            if "opus_decoder.c" in line and " -c " in line:
                return [p for p in shlex.split(line) if p.startswith(("-D", "-U"))]
    raise ValueError("Cannot determine selected CMake opus target ABI defines; enable CMAKE_EXPORT_COMPILE_COMMANDS=ON and rebuild.")


def git_info(source):
    try:
        return {"commit": capture(["git", "rev-parse", "HEAD"], source),
                "dirty": bool(capture(["git", "status", "--porcelain", "--untracked-files=no"], source))}
    except (subprocess.CalledProcessError, FileNotFoundError):
        return {"commit": None, "dirty": None}


def build(args):
    source, out = args.source_dir.resolve(), args.build_dir.resolve()
    if not (source / "include/opus.h").is_file():
        raise ValueError(f"OPUS_DIR must contain include/opus.h: {source}")
    candidates = [out / ".libs/libopus.a", out / "libopus.a"]
    existing = [p for p in candidates if p.is_file()]
    if args.library:
        archive = Path(args.library).resolve()
        if not archive.is_file() or archive.suffix != ".a":
            raise ValueError(f"OPUS_LIB must name an existing static archive: {archive}")
    else:
        if len(existing) > 1:
            raise ValueError("Both .libs/libopus.a and libopus.a exist; select the matching archive explicitly with OPUS_LIB=/path/libopus.a")
        archive = existing[0] if existing else None
    if archive is None:
        raise ValueError(f"Build the static Opus library first; no libopus.a in {out} or {out / '.libs'}")
    config = out / "config.h"
    if not config.exists():
        raise ValueError(f"Missing generated {config}; OPUS_BUILD_DIR must identify the matching build.")
    compiler = shlex.split(args.cc)
    if not compiler:
        raise ValueError("CC must not be empty")
    abi = cmake_defines(out) if (out / "CMakeCache.txt").exists() else ["-DHAVE_CONFIG_H", "-DOPUS_BUILD"]
    headers = {}
    for folder in ("include", "celt", "silk", "dnn"):
        for path in sorted((source / folder).rglob("*.h")):
            headers[str(path.relative_to(source))] = sha(path)
    metadata = {
        "format_version": 2,
        "library_path": str(archive), "library_sha256": sha(archive),
        "source_dir": str(source), "build_dir": str(out),
        "library_source": git_info(source),
        "compiler": compiler,
        "compiler_version": capture(compiler + ["--version"]).splitlines()[0],
        "benchmark_cflags": args.cflags, "benchmark_cppflags": args.cppflags,
        "benchmark_ldflags": args.ldflags, "probe_abi_flags": abi,
        "config_sha256": sha(config),
        "headers_sha256": hashlib.sha256(json.dumps(headers, sort_keys=True).encode()).hexdigest(),
        "probe_symbols": list(SYMBOLS),
    }
    cmake_cache = out / "CMakeCache.txt"
    if cmake_cache.exists():
        metadata["library_build_configuration"] = [line for line in cmake_cache.read_text().splitlines()
            if line.startswith(("OPUS_", "CMAKE_C_FLAGS", "CMAKE_C_COMPILER:",
                                "CMAKE_BUILD_TYPE:", "CMAKE_SYSTEM_PROCESSOR:")) and "=" in line]
    else:
        # This generated, trusted build script prints its configured options.
        status = out / "config.status"
        metadata["library_build_configuration"] = capture([str(status), "--config"], out) if status.exists() else None
    for name in ("src/bench_worker.c", "src/verify_calls.c", "bench.py"):
        if not (ROOT / name).exists():
            raise ValueError(f"Missing benchmark source: {ROOT / name}")
    metadata["benchmark_sources"] = {name: sha(ROOT / name) for name in
        ("src/bench_worker.c", "src/verify_calls.c", "bench.py", "scripts/build_bench.py", "GNUmakefile")}
    fingerprint = hashlib.sha256(json.dumps(metadata, sort_keys=True).encode()).hexdigest()
    target = ROOT / "build" / fingerprint[:20]
    target.mkdir(parents=True, exist_ok=True)
    include = ["-I" + str(source / "include")]
    standard = ["-std=c99", "-Wall", "-Wextra"]
    flags = shlex.split(args.cppflags) + shlex.split(args.cflags)
    link = [str(archive)] + shlex.split(args.ldflags) + ["-lm", "-pthread"]
    worker = target / "worker"
    proof = target / "verify_worker"
    if not worker.exists():
        subprocess.run(compiler + standard + flags + include +
            [str(ROOT / "src/bench_worker.c")] + link + ["-o", str(worker)], check=True)
    if not proof.exists():
        internal = ["-I" + str(out)] + ["-I" + str(source / d) for d in ("celt", "silk", "dnn")]
        subprocess.run(compiler + standard + flags + include + internal + abi + ["-DBENCH_PROBES"] +
            [str(ROOT / "src/bench_worker.c"), str(ROOT / "src/verify_calls.c")] +
            ["-Wl,--wrap=" + symbol for symbol in SYMBOLS] + link + ["-o", str(proof)], check=True)
    metadata["worker_sha256"] = sha(worker)
    metadata["verify_worker_sha256"] = sha(proof)
    metadata["build_fingerprint"] = fingerprint
    (target / "build_metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    # Always repackage so the top-level executable tracks the selected build,
    # even if another variant was built between two invocations.
    with tempfile.TemporaryDirectory(dir=target) as package:
        stage = Path(package)
        shutil.copy2(ROOT / "bench.py", stage / "__main__.py")
        for name in ("worker", "verify_worker", "build_metadata.json"):
            shutil.copy2(target / name, stage / name)
        bundle = target / "opus_perf_bench.tmp"
        zipapp.create_archive(stage, bundle, interpreter="/usr/bin/env python3", compressed=True)
        bundle.chmod(0o755)
        os.replace(bundle, ROOT / "opus_perf_bench")
    print(f"Built opus_perf_bench: {archive} (SHA256 {metadata['library_sha256'][:16]})")
    print("Timed worker is uninstrumented; verify_worker provides separate actual operator-call proof.")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source-dir", type=Path, required=True)
    p.add_argument("--build-dir", type=Path, required=True)
    p.add_argument("--library", default="", help="explicit static archive; otherwise require an unambiguous build output")
    p.add_argument("--cc", default="cc")
    p.add_argument("--cflags", default="-O2 -g")
    p.add_argument("--cppflags", default="")
    p.add_argument("--ldflags", default="")
    try:
        build(p.parse_args())
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        print(f"build error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
