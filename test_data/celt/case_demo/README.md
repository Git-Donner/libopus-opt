# CELT demo matching the original celt-encode case

Parameters: CELT-only, 48000 Hz, mono, 960 samples/frame, 64000 bit/s,
complexity 5, FULLBAND, maximum bandwidth FULLBAND, CBR, DTX/FEC disabled.
100 warmup frames + 1000 test frames, same encoder state, no reset.

This matches the CURRENT test_1/src/bench_worker.c initialization, which uses
OPUS_APPLICATION_RESTRICTED_CELT. Both tested libraries must support this
application constant. Do not manually define the numeric value or silently
replace it with RESTRICTED_LOWDELAY while claiming identical settings.

Input: raw signed 16-bit little-endian PCM, 48000 Hz MONO, 22 seconds,
2112000 bytes. Original mixed signal, seed42, exported from the worker.

https://raw.githubusercontent.com/Git-Donner/libopus-opt/e16594a555fd78dd48114898bd5b5eccffc95e48/test_data/celt/celt_encode_mixed_48k_mono_22s.pcm

Input SHA256:
f6cbf41a41e2fe93c4fbab0e8fa3a653418e7dc83c422f37b54cbf16d6e673eb

Usage:
    ./celt_source input_celt_mixed_mono.pcm

Expected:
    Encoded 1100 CELT frames (100 warmup + 1000 test).

The program preloads and converts exactly 1100 frames before creating the
encoder. Shorter/unreadable inputs fail; longer files use only their first
22 seconds. Static buffers avoid large stack allocations. The hot loop has
only the API call, an encoding-error check, and loop control. No decoder,
timer, hash, per-frame I/O or per-frame configuration switching is present.
Encoded packets are discarded; only a final frame count is printed.

IMPORTANT: Whole-process DevKit/perf still includes input loading/conversion,
initialization and ALL1100frames. Naming the first100frames warmup does not
exclude them from PMU counters. This is not exact region-of-interest control.

Build example (replace library/source locations with actual paths):
    gcc -O2 -g -std=c99 -Wall -Wextra -I"$SOURCE_DIR/include" \
      celt_encode_demo.c "$SOURCE_DIR/.libs/libopus.a" -lm -o celt_source
    gcc -O2 -g -std=c99 -Wall -Wextra -I"$BRANCH_DIR/include" \
      celt_encode_demo.c "$BRANCH_DIR/.libs/libopus.a" -lm -o celt_branch

Run with the same CPU and input:
    taskset -c "$CPU_ID" ./celt_source input_celt_mixed_mono.pcm
    taskset -c "$CPU_ID" ./celt_branch input_celt_mixed_mono.pcm

Validation: Linux x86_64 GCC14.2, float and fixed libraries; -Werror builds.
For each library, all1100 packet payloads, lengths and final ranges match
original-worker packets exactly. A separate validation wrapper checks PCM,
all encoder controls and CELT TOCs, forbids decoder calls, and is NOT part of
the delivered program. Six invalid-argument/input tests per library passed.
No ARM hardware validation or performance improvement is claimed.
