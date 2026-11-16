# Isolated Autotools validation

**PASS**, 2026-11-16. Default cutoff and vector-finish switches were both on.
No production files were changed by this validation.

The source base was copied from
`/workspace/spreading-neon-validation/autotools-source` to
`/workspace/spreading-v2-validation/autotools-source`. Exactly these six current
files were overlaid: `celt/arm/bands_neon_intr.c`,
`celt/arm/spreading_cutoffs.h`, `celt_headers.mk`, `CMakeLists.txt`,
`Makefile.am`, and `celt/tests/test_spreading_cutoffs.c`. Their SHA-256 hashes
are recorded in `autotools-overlay-manifest.json`. A separate read-only audit
confirmed the six-file list, hashes, isolated execution paths, default
switches, compiler, and flags.

Autoreconf, configure, and `make -j8 libopus.la` passed. The compiler was
AArch64 GCC 14.2.0 (Debian 14.2.0-19), with the existing local cross sysroot.
Configuration used `--host=aarch64-linux-gnu --disable-sve --disable-shared
--enable-static --disable-fixed-point` and `CFLAGS=-O3`.

Three standalone tests were linked to this actual newly built static archive
and run under QEMU AArch64 with CPU `max,sve=off`:

| Test | Result |
|---|---|
| `test_unit_spreading_neon` | 150,016 exact aggregate/decision/history cases; 2,344 guard-page sequences |
| `test_encoder_spreading_neon` | 300 paired CELT frames; mono/stereo, 2.5/10/20 ms; exact packets, decoded PCM, and final ranges; 296 NEON calls scanning 165,384 coefficients |
| `test_spreading_cutoffs` | 5,162,040 exact cutoff predicates; 643,200 scan/decision/history cases across all four rounding modes; 6,432 guard-page sequences; saved environment restored |

The encoder test used `-Wl,--wrap=spreading_statistics_neon`. The cutoff test
used `-frounding-math -ffp-contract=off`. All compile and execution statuses
were zero, as recorded in `autotools-statuses.json`. Complete commands and the
selected build environment are in `autotools-commands.json`; each command has
a matching `autotools-*.log` file. `validate_autotools.py` is the exact driver.

Archive:
`/workspace/spreading-v2-validation/autotools-build/.libs/libopus.a`

SHA-256:
`8f3d75a5bb2a1e1e810445b048a51c9547e4e1a773631f5ac666e920363ae265`

The archive hash was computed after the build and checked again after the
tests. It is also saved in `autotools-archive-sha256.txt`. QEMU execution is
correctness evidence, not a native performance measurement.
