# Independent spreading NEON v2 review

Review date: 2026-11-16. Scope: exact magnitude cutoffs, floating-point
environment selection, and AArch64 vector finishing. This file and the two
`review_*` programs are validation artifacts, not production Opus inputs.

## Cutoff definition and proof

For a positive integer band length `N` and binary32 threshold `T`, define

```
P_N,T(x) = RN32(RN32(x*x) * float(N)) < T
a_N,T   = the smallest nonnegative binary32 x for which P_N,T(x) is false
```

`RN32` means an IEEE binary32 operation rounded to nearest, ties to even;
the square and scale are distinct operations. Every table-supported `N` is
exactly representable in binary32. Squaring nonnegative arguments, multiplying
by positive `N`, and rounding each result to nearest are monotone. Therefore
the set accepted by `P` is exactly the initial interval of nonnegative
binary32 values strictly below `a`. Squaring removes the sign, so for every
finite input the equivalent test is `fabsf(x) < a`. Both signs of zero are
accepted, both infinities are rejected, and all NaNs produce false comparisons.

`review_cutoffs.py` independently implements binary32 multiplication by integer
significands, powers of two, and exact tie-to-even integer rounding. It uses no
host floating-point arithmetic to find or check the boundaries. A monotone
binary search finds each of the 45 first-failing values. For each it checks
the predecessor and first-failing value and a neighborhood of 131,073 binary32
patterns. Total: **5,898,285 exact comparisons, all passed**.

| N | T = 0.25 | T = 0.0625 | T = 0.015625 |
|---:|---:|---:|---:|
| 12 | `0x3e13cd3b` | `0x3d93cd3b` | `0x3d13cd3b` |
| 16 | `0x3e000000` | `0x3d800000` | `0x3d000000` |
| 18 | `0x3df15bef` | `0x3d715bef` | `0x3cf15bef` |
| 22 | `0x3dda514a` | `0x3d5a514a` | `0x3cda514a` |
| 24 | `0x3dd105ec` | `0x3d5105ec` | `0x3cd105ec` |
| 32 | `0x3db504f4` | `0x3d3504f4` | `0x3cb504f4` |
| 36 | `0x3daaaaab` | `0x3d2aaaab` | `0x3caaaaab` |
| 44 | `0x3d9a5fb2` | `0x3d1a5fb2` | `0x3c9a5fb2` |
| 48 | `0x3d93cd3b` | `0x3d13cd3b` | `0x3c93cd3b` |
| 64 | `0x3d800000` | `0x3d000000` | `0x3c800000` |
| 72 | `0x3d715bef` | `0x3cf15bef` | `0x3c715bef` |
| 88 | `0x3d5a514a` | `0x3cda514a` | `0x3c5a514a` |
| 96 | `0x3d5105ec` | `0x3cd105ec` | `0x3c5105ec` |
| 144 | `0x3d2aaaab` | `0x3caaaaab` | `0x3c2aaaab` |
| 176 | `0x3d1a5fb2` | `0x3c9a5fb2` | `0x3c1a5fb2` |

The latter two columns are exactly the first column minus `0x00800000` and
`0x01000000`. At these normal-valued boundaries those bit differences divide
the represented magnitude by two and four. It is sufficient to store one
`uint32_t` cutoff per supported `N` and derive the other two with integer
subtractions. Reinterpreting duplicated integer bits as FP32 avoids aliasing
and decimal conversion concerns. Unsupported `N` values must use the original
two multiplications; a zero table entry is an unambiguous sentinel.

## Independent host floating-point check

`review_host_oracle.c` explicitly stores the square and scaled result into
separate volatile floats. It was compiled on the x86_64 review host with:

```
gcc -O2 -std=c99 -frounding-math -ffp-contract=off -fno-tree-vectorize \
    review_host_oracle.c -lm -o review_host_oracle
```

The RNE boundary check covers both signs and offsets -1024 through +1024 from
every cutoff. Additional deterministic random inputs span all binary32 bit
patterns, including nonfinite encodings. The complete program made
**12,534,120 comparisons** including the intentional non-RNE probes. RNE
comparisons all passed. Its output is saved in `review_host_oracle.log`.

Nondefault rounding probes demonstrate that the fallback is necessary:

- Downward and toward zero: `N=22, T=.25, xbits=0x3dda514a` is accepted by
  the original arithmetic and rejected by the RNE cutoff.
- Upward: `N=12, T=.25, xbits=0x3e13cd3a` is rejected by the original
  arithmetic and accepted by the RNE cutoff.

These host probes supplement the exact integer proof. They are not ARM
execution or performance evidence.

## Floating-point environment

The cutoff is valid when AArch64 FPCR.RMode bits [23:22] are `00` (RNE).
Reading FPCR once per function invocation is sufficient when the caller keeps
its floating-point environment stable throughout the call. A 64-bit integer
output operand is needed for the AArch64 `MRS ..., FPCR` assembly instruction.

Traps should select the original arithmetic: the FPCR exception-enable bits
are IOE[8], DZE[9], OFE[10], UFE[11], IXE[12], and IDE[15], mask `0x9f00`.
The combined unsuitable-environment mask is `0x00c09f00`. Testing all exception
enables is conservative and avoids changing whether the arithmetic can trap.

FPCR input/output flushing and default-NaN behavior do not change integer
classification. Every cutoff is normal and far larger than a subnormal input;
flushing any subnormal to zero leaves it below every cutoff. NaNs are rejected
by either implementation. ARM32, compilers without the chosen FPCR access,
unsupported `N`, and `__FAST_MATH__` should retain the original arithmetic.

The optimization preserves result classification, integer aggregates, and
codec decisions. It does **not** preserve FPSR cumulative exception flags
when traps are disabled: removed multiplies may have raised inexact,
underflow, or overflow. The host probe demonstrates matching classifications
but differing flags for `0.1f`, `FLT_MAX`, and the smallest positive subnormal.
Do not describe the optimization as full floating-point-environment
equivalence. With enabled traps, the original-operation fallback addresses
the changed-trap concern.

## AArch64 vector finishing

For count vectors `c0`, `c1`, and `c2` with four partial counts each:

```
p01    = vpaddq_u32(c0, c1)
p2     = vpaddq_u32(c2, zero)
totals = vpaddq_u32(p01, p2)
```

`totals` is exactly `[sum(c0), sum(c1), sum(c2), 0]`. Compare
`2*totals >= N`, shift each unsigned comparison mask right by 31, then
horizontally add to obtain `tmp`. The fourth lane is false because processed
bands have `N > 8`. Pairwise addition of `totals` with itself gives the HF
numerator's count sum in lane zero, so that scalar extraction is needed only
when `update_hf && i > m->nbEBands-4`. Keep the existing integer division per
band; moving it outside the loop would change truncation.

For `N % 4 != 0`, retaining the original vector-to-scalar reduction and scalar
tail preserves safe loads and avoids extra tail machinery. The vector finish
switch can operate independently of the cutoff switch because it only
consumes the same integer count vectors.

## Source/specification references

- Production baseline: `libopus-opt/celt/arm/bands_neon_intr.c` (two distinct
  `vmulq_f32` operations; three strict comparisons; per-band HF division).
- Scalar reference: `libopus-opt/celt/bands.c`, `spreading_statistics_c`.
- Standard band edges: `libopus-opt/celt/modes.c`, `eband5ms`, and
  `libopus-opt/celt/static_modes_float.h`; multiplying the standard edge
  differences by M in {1,2,4,8} yields the 15 supported lengths above N=8.
- IEEE 754-2019 sections 4.3.1 (roundTiesToEven) and 5.4.1 (arithmetic
  operations), as the mathematical basis for the two rounded multiplies.
- Arm A-profile Architecture Reference Manual, FPCR (RMode and exception
  enable fields), and the FMUL/FABS/FCMLT instruction definitions.
- [Arm Advanced SIMD intrinsics reference](https://arm-software.github.io/acle/neon_intrinsics/advsimd.html)
  for `vpaddq_u32`, `vaddvq_u32`, `vabsq_f32`, and `vcltq_f32` mappings.

## Production code review result

Reviewed `celt/arm/bands_neon_intr.c` and `celt/arm/spreading_cutoffs.h` after
implementation. **No outstanding correctness finding.** The review identified
one portability issue in the first draft: an `unsigned long` FPCR assembly
output is only 32 bits on AArch64 ILP32 and Windows LLP64. The implementation
now uses `opus_uint64`, guaranteeing an X-register operand.

The actual header was parsed independently and checked against the integer
oracle: exactly 177 entries, exactly the 15 supported nonzero positions, and
all 45 stored/derived thresholds match. The implementation reads FPCR once,
checks the RNE/trap mask, bounds the table access after skipping N<=8, keeps
unsupported lengths on the original arithmetic, and derives cutoffs through
integer bit operations. Cutoff and vector-finish switches are independent.
The packed totals and majority logic match the algebra above. Odd/custom
tails retain bounded vector loads and the existing scalar arithmetic; the HF
path retains its strict band bound and per-band integer division.

ARM compilation/execution, codec regression checks, switch-matrix checks,
and performance measurements belong to the separate build/test workstream.
