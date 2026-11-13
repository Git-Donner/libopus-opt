# NEON PVQ validation results

Validation platform: Linux x86-64 host, Debian AArch64 GCC 14.2.0 cross compiler,
QEMU user-mode AArch64 10.0.13. This proves ARM instruction execution and
functional invariants under emulation, NOT native ARM performance.

The compiler, binutils, AArch64 runtime and QEMU packages were downloaded by APT
against Debian's signed package index with the existing trusted keyring and
normal package checksum/TLS verification. Packages were extracted under
`/workspace/pvq-neon-validation/toolchain`; no system installation was performed.

## Completed results

* Full AArch64 floating-point Release library, demo programs and existing test
  executables cross-built successfully through CMake/Ninja.
* Current `celt/tests/test_unit_pvq_neon.c`: **8824 cases passed**, plus explicit
  equal-score tie checks for N=4/N=8 and all-zero K=1 selection. Lengths2..208,
  K0..1024, deterministic normalized random vectors, one-hot, equal magnitudes,
  signed zeros, tiny values, and4 projection-defence cases (NaN,+Inf,-Inf,sum>=64).
* Linux guard pages placed immediately after the requested N input floats and
  N output integers. No tail guard-page fault or preceding canary corruption.
  Input-preservation, L1 pulse budget exactlyK, signs, finite/exact returned
  squared-pulse energy and direct/presumed dispatch agreement all passed.
* The scalar C output pulse vector differed in818 cases. Largest relative
  scalar-alignment-score loss was **0.01685245%**. This is a directional score
  sanity measurement on synthetic vectors, not a perceptual audio-quality claim
  and not a cross-ISA bit-exactness guarantee. The broad failure guard was5%.
* New kernel + unit harness built with **UBSan** and
  `-fno-sanitize-recover=all`:8824cases passed, no UB diagnostic, exit0.
* Actual public CELT encode/decode pipeline:50 mono48kHz20ms frames at64kbps,
  complexity5. Linker wrapper observed **2629 calls to op_pvq_search_neon**;
  frame counts and entropy final ranges matched throughout.
* Real ARM RTCD table compiled separately with NEON-presume flags removed:
  slots0..2 equal scalarC, slots3..4 equal NEON. A call through slot3 produced
  exactly the same pulse vector and energy as direct NEON.
* Existing `test_opus_padding`:passed, exit0.
* Existing `test_opus_api`:passed, exit0. Its allocator-failure group explicitly
  SKIPPED because the runner reports its GLIBC/valgrind conditions unsupported;
  this subgroup is not claimed passed.

## ASan limitation

The kernel+harness built with ASan+UBSan exited with targetSIGSEGV before useful
ASan diagnostics. A minimal separately built AArch64 program containing only
`puts("AArch64 toolchain/QEMU ready")` fails identically with ASan+UBSan under
this QEMU/runtime combination; the unsanitized minimal program passes.
Therefore ASan validation is **unavailable here**, not passed and not attributed
to the PVQ kernel. ASAN_OPTIONS=detect_leaks=0 was used because leak detection
under user-mode emulation is not the target of this test. Native ARM ASan
validation remains advisable. Guard-page memory checks and UBSan did pass.

## Existing decode-fuzz runner

`test_opus_decode` completed under QEMU with **exit0**. All listed sample-rate/channel
initializations, initial PLC, exhaustive short packet prefixes, randomized packet
and mode-pair tests, and opus_pcm_soft_clip checks passed. The full runner log
is `test_opus_decode.log`.

## Commands and evidence

* `aarch64-toolchain.cmake`:actual cross-toolchain/CMake emulator configuration.
* `configure.log`, `build.log`:full AArch64 build logs.
* `test-compile-command.txt`:actual standalone-unit compiler command, with the
  selected CMake target's real -D/-I flags copied from compile_commands.json.
* `final-validation-commands.txt`:unit and diagnostic command lines.
* `test_unit_pvq_neon.log`, `test_unit_pvq_neon_ubsan.log`:final unit outputs.
* `test_encoder_dispatch.c/.log`:actual codec-pipeline wrapper proof.
* `test_rtcd_dispatch.c/.log`, `rtcd-compile-command.txt`:actual table proof.
* `hello.c`, `hello.log`, `hello_sanitize.log`:ASan runtime isolation evidence.
* `test_opus_api.log`, `test_opus_padding.log`, `test_opus_decode.log`:existing
  runner outputs, including explicit skipped groups.

All paths above are relative to `/workspace/pvq-neon-validation`.

The compiler commands require the extracted binutils support library path:

```sh
export LD_LIBRARY_PATH=/workspace/pvq-neon-validation/toolchain/usr/lib/x86_64-linux-gnu
```

Core build commands used:

```sh
/workspace/opus-tools-env/bin/cmake -S /workspace/libopus-opt \
  -B /workspace/pvq-neon-validation/arm-build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=/workspace/pvq-neon-validation/aarch64-toolchain.cmake \
  -DCMAKE_MAKE_PROGRAM=/workspace/opus-tools-env/bin/ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS=-ffp-contract=off \
  -DOPUS_BUILD_TESTING=ON -DOPUS_BUILD_PROGRAMS=ON \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
/workspace/opus-tools-env/bin/cmake --build /workspace/pvq-neon-validation/arm-build --parallel 5
```

Programs were run explicitly as:

```sh
/workspace/pvq-neon-validation/toolchain/usr/bin/qemu-aarch64 \
  -L /workspace/pvq-neon-validation/toolchain/usr/aarch64-linux-gnu \
  /workspace/pvq-neon-validation/arm-build/test_opus_api
```

The project-specific CTest wrapper executes binaries directly, so user-mode
emulation tests were invoked explicitly rather than claiming CTest emulator
integration.
