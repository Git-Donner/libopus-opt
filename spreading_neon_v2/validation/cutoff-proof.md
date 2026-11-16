# Exact spreading cutoffs under round-to-nearest

The replacement predicate is valid for each table row when every binary32
operation uses round-to-nearest, ties-to-even:

```
RN32(RN32(x * x) * N) < T    iff    abs(x) < cutoff(N, T)
```

Here `T` is exactly `1/4`, `1/16`, or `1/64`. The left side has two
separately rounded binary32 multiplications. Reassociating its products or
rounding a mathematical `sqrt(T/N)` is not the construction being proved.
The standard CELT widths greater than eight produce exactly these 15 values
of `N`: 12, 16, 18, 22, 24, 32, 36, 44, 48, 64, 72, 88, 96, 144, 176.
Each is an exactly representable positive binary32 integer.

The header stores a dense table indexed by `N`, with 177 explicit `uint32`
entries for lengths 0 through 176. Exactly those 15 supported lengths have
nonzero entries; the remaining 162 entries are zero. A stored entry is the
first-failing cutoff for `T=1/4`. The other two cutoff bit patterns are
derived by subtracting `0x00800000` and `0x01000000`. These subtractions lower
the exponent by one and two because the cutoffs remain positive normal
values. The verifier independently recomputes all three cutoffs rather
than assuming the derived shifts preserve the two-product predicate.

## Why two adjacent values suffice for the full domain

Order the nonnegative finite binary32 values numerically. Their unsigned bit
patterns have the same order. Exact squaring is nondecreasing on this domain.
Correct round-to-nearest, including ties-to-even, is a nondecreasing map:
rounding partitions the real line into ordered, adjoining rounding intervals;
the choice at an interval boundary cannot reverse the order. Multiplication
by the positive integer `N` is also nondecreasing. Therefore

```
F(x) = RN32(RN32(x*x)*N)
```

is nondecreasing. Underflow, subnormal results, and overflow to positive
infinity preserve this property. `F(0)=0<T`, and `F(+infinity)=+infinity>=T`.
Consequently there is a first nonnegative binary32 input `a` for which
`F(a)>=T`; all smaller inputs satisfy the strict comparison and all larger
inputs fail it. This is precisely the predicate `x<a`.

The integer verifier independently finds `a` with a binary search over
nonnegative binary32 bit patterns. For each table entry it checks that the
stored bits equal `a`. In particular, the predecessor of `a` passes the old
predicate and `a` itself fails. The monotonicity argument then proves the
equivalence for **every** nonnegative finite binary32 input; the random and
neighborhood checks provide additional implementation checks rather than
the completeness argument.

For a negative finite input, exact `x*x` is the same real value as
`abs(x)*abs(x)` and receives the same rounding. Both signed zeros produce
zero and satisfy all three predicates. Both signed infinities produce
positive infinity and fail all three predicates. Every quiet or signaling
NaN produces a NaN in the old expression, and ordered comparisons are false;
`abs(NaN)<a` is also false. This establishes the output equivalence for all
binary32 encodings. Floating-point exception flags and NaN payloads are not
part of the scanner's integer output contract.

## Independent verifier

`verify_cutoffs.py` uses Python integers only for its oracle. It decodes a
finite binary32 value to an integer significand times an integer power of
two, multiplies the significands exactly, then rounds the integer product to
24 significant bits (or the subnormal grid at `2^-149`). Rounding uses an
exact quotient and remainder and explicitly implements the ties-to-even
rule. It handles underflow and overflow and performs these steps separately
for each multiplication. It uses neither host floating-point arithmetic nor
the production lookup/scanner nor a square root.

Besides the complete-domain proof above, the default run checks 4,096 ULPs
on each side of every one of the 45 cutoffs, including the cutoff itself,
with both signs. It also checks 65,536 deterministic random 32-bit float
encodings per `N` against all three thresholds and an explicit set of
signed zeros, extreme subnormals/normals, infinities, and quiet/signaling
NaNs. The oracle has focused self-checks for ties at zero, subnormal and
normal values, and the overflow midpoint. The JSON report records SHA-256
hashes of the exact header and verifier sources.

```
python3 /workspace/spreading-v2-validation/verify_cutoffs.py \
  /workspace/libopus-opt/celt/arm/spreading_cutoffs.h \
  --json /workspace/spreading-v2-validation/exact-cutoffs.json
```

This numerical proof applies only to round-to-nearest, ties-to-even. The
production scanner must use the original two-product expression for other
rounding modes and for lengths absent from the table. ARM scanner and full
decision tests cover those paths separately. These checks make no native
ARM performance claim.

The integer verifier also computes the first-failing cutoff using each of
the three directed rounding modes. It records a concrete disagreement with
the RN cutoff for each mode, demonstrating why the fallback is required.
Because both products are nonnegative, rounding downward and rounding
toward zero give the same product values here. Those calculations use an
exact remainder to choose ceiling or truncation and handle directed
overflow correctly; they do not use the host's rounding mode.
