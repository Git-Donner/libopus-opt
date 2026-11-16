#!/usr/bin/env python3
"""Verify spreading cutoffs with integer-only IEEE-754 binary32 arithmetic.

No host floating-point operation, sqrt, C compiler, or production helper is used
by the arithmetic oracle.  Run from anywhere; --help describes the controls.
"""

import argparse
import hashlib
import json
import pathlib
import re
import sys


STANDARD_NS = (12, 16, 18, 22, 24, 32, 36, 44, 48, 64, 72, 88, 96, 144, 176)
THRESHOLDS = (0x3E800000, 0x3D800000, 0x3C800000)
INFINITY = 0x7F800000
QUIET_NAN = 0x7FC00000


def round_binary32(significand, exponent, mode="nearest"):
    """Round nonnegative integer * 2**exponent to IEEE-754 binary32."""
    if not significand:
        return 0
    leading_exponent = significand.bit_length() - 1 + exponent
    unit_exponent = max(-149, leading_exponent - 23)
    shift = exponent - unit_exponent
    if shift >= 0:
        rounded = significand << shift
    else:
        divisor = 1 << -shift
        rounded, remainder = divmod(significand, divisor)
        twice_remainder = remainder << 1
        if (mode == "upward" and remainder) or (
            mode == "nearest" and (
                twice_remainder > divisor or (
                    twice_remainder == divisor and rounded & 1
                )
            )
        ):
            rounded += 1
    if rounded < 1 << 23:
        # The only possible unit here is 2**-149, including underflow to zero.
        return rounded
    if rounded >= 1 << 24:
        rounded >>= 1
        unit_exponent += 1
    exponent_field = unit_exponent + 150
    if exponent_field >= 255:
        return 0x7F7FFFFF if mode in ("downward", "towardzero") else INFINITY
    return (exponent_field << 23) | (rounded - (1 << 23))


def decode_finite(bits):
    """Return the exact nonnegative binary32 value as integer * 2**exponent."""
    exponent_field, fraction = bits >> 23, bits & 0x7FFFFF
    if exponent_field == 0:
        return fraction, -149
    return fraction | (1 << 23), exponent_field - 150


def multiply_binary32(a, b, mode="nearest"):
    """Multiply nonnegative binary32 bit patterns with one explicit rounding."""
    if a > INFINITY or b > INFINITY:
        return QUIET_NAN
    if a == INFINITY or b == INFINITY:
        return QUIET_NAN if a == 0 or b == 0 else INFINITY
    ma, ea = decode_finite(a)
    mb, eb = decode_finite(b)
    return round_binary32(ma * mb, ea + eb, mode)


def original_predicate(bits, n_bits, threshold, mode="nearest"):
    magnitude = bits & 0x7FFFFFFF
    squared = multiply_binary32(magnitude, magnitude, mode)
    product = multiply_binary32(squared, n_bits, mode)
    # Every NaN code exceeds every finite positive threshold in this ordering.
    return product < threshold


def find_cutoff(n_bits, threshold, mode="nearest"):
    """Find the first nonnegative binary32 bit pattern failing the old test."""
    low, high = 0, INFINITY
    assert original_predicate(low, n_bits, threshold, mode)
    assert not original_predicate(high, n_bits, threshold, mode)
    while high - low > 1:
        middle = (low + high) // 2
        if original_predicate(middle, n_bits, threshold, mode):
            low = middle
        else:
            high = middle
    return high


def self_test():
    checks = (
        (round_binary32(1, -150), 0),  # Half a subnormal ULP; even is zero.
        (round_binary32(3, -150), 2),
        (round_binary32((1 << 24) + 1, -24), 0x3F800000),
        (round_binary32((1 << 24) + 3, -24), 0x3F800002),
        (round_binary32((1 << 24) - 1, 104), 0x7F7FFFFF),
        (round_binary32((1 << 25) - 1, 103), INFINITY),
        (multiply_binary32(0x3F800000, 0x3F800000), 0x3F800000),
        (multiply_binary32(1, 0x3F000000), 0),
        (multiply_binary32(3, 0x3F000000), 2),
        (multiply_binary32(0x7F7FFFFF, 0x40000000), INFINITY),
        (multiply_binary32(0, INFINITY), QUIET_NAN),
        (round_binary32(1, -150, "upward"), 1),
        (round_binary32(3, -150, "downward"), 1),
        (round_binary32(3, -150, "towardzero"), 1),
        (round_binary32((1 << 25) - 1, 103, "downward"), 0x7F7FFFFF),
    )
    for actual, expected in checks:
        if actual != expected:
            raise AssertionError(f"oracle self-test: {actual:08x} != {expected:08x}")


def parse_header(path):
    """Read the dense N-indexed table, checking every unsupported entry."""
    source = path.read_text()
    uncommented = re.sub(r"/\*.*?\*/|//[^\n]*", "", source, flags=re.S)
    maximum_match = re.search(
        r"#\s*define\s+SPREADING_CUTOFF_MAX_N\s+(\d+)\b", uncommented
    )
    if not maximum_match or int(maximum_match.group(1)) != 176:
        raise ValueError("expected SPREADING_CUTOFF_MAX_N == 176")
    table_match = re.search(
        r"\bspreading_cutoff_bits\s*\[[^]]+\]\s*=\s*\{([^}]+)\}",
        uncommented, flags=re.S
    )
    if not table_match:
        raise ValueError("missing spreading_cutoff_bits initializer")
    tokens = [token.strip() for token in table_match.group(1).split(",") if token.strip()]
    bits = []
    for token in tokens:
        if not re.fullmatch(r"0[xX][0-9a-fA-F]{8}[uUlL]*", token):
            raise ValueError(f"unexpected table expression: {token}")
        bits.append(int(token.rstrip("uUlL"), 16))
    if len(bits) != 177:
        raise ValueError(f"expected 177 explicit entries, parsed {len(bits)}")
    supported = tuple(n for n, value in enumerate(bits) if value)
    if supported != STANDARD_NS:
        raise ValueError(f"expected supported Ns {STANDARD_NS}; parsed {supported}")
    # This mirrors only the public bit representation, not its arithmetic.
    # Each derived value is independently checked with the integer oracle.
    return {
        n: (bits[n], bits[n] - 0x00800000, bits[n] - 0x01000000)
        for n in supported
    }


def xorshift32(state):
    state ^= (state << 13) & 0xFFFFFFFF
    state ^= state >> 17
    state ^= (state << 5) & 0xFFFFFFFF
    return state & 0xFFFFFFFF


def verify(rows, boundary_ulps, random_samples):
    checked = 0
    report_rows = []
    specials = (
        0, 0x80000000, 1, 0x80000001, 0x007FFFFF, 0x807FFFFF,
        0x00800000, 0x80800000, 0x7F7FFFFF, 0xFF7FFFFF,
        0x7F800000, 0xFF800000, 0x7F800001, 0xFF800001,
        0x7FBFFFFF, 0xFFBFFFFF, 0x7FC00000, 0xFFC00000,
        0x7FFFFFFF, 0xFFFFFFFF,
    )
    state = 0xC45A731D
    for n in STANDARD_NS:
        n_bits = round_binary32(n, 0)
        cutoffs = rows[n]
        for t, threshold in enumerate(THRESHOLDS):
            exact = find_cutoff(n_bits, threshold)
            if cutoffs[t] != exact:
                raise AssertionError(
                    f"N={n} threshold[{t}]: header=0x{cutoffs[t]:08x}, "
                    f"integer oracle=0x{exact:08x}"
                )
            for delta in range(-boundary_ulps, boundary_ulps + 1):
                magnitude = exact + delta
                for bits in (magnitude, magnitude | 0x80000000):
                    expected = original_predicate(bits, n_bits, threshold)
                    actual = (bits & 0x7FFFFFFF) < cutoffs[t]
                    if actual != expected:
                        raise AssertionError(
                            f"N={n} threshold[{t}] x=0x{bits:08x} differs"
                        )
                    checked += 1
        for bits in specials:
            for t, threshold in enumerate(THRESHOLDS):
                expected = original_predicate(bits, n_bits, threshold)
                actual = (bits & 0x7FFFFFFF) < cutoffs[t]
                if actual != expected:
                    raise AssertionError(f"N={n} special x=0x{bits:08x} differs")
                checked += 1
        for _ in range(random_samples):
            state = xorshift32(state)
            for t, threshold in enumerate(THRESHOLDS):
                expected = original_predicate(state, n_bits, threshold)
                actual = (state & 0x7FFFFFFF) < cutoffs[t]
                if actual != expected:
                    raise AssertionError(
                        f"N={n} threshold[{t}] random x=0x{state:08x} differs"
                    )
                checked += 1
        report_rows.append({
            "N": n, "cutoff_bits": [f"0x{value:08x}" for value in cutoffs]
        })
    directed_cases = []
    for mode in ("downward", "upward", "towardzero"):
        differences = 0
        witness = None
        for n in STANDARD_NS:
            n_bits = round_binary32(n, 0)
            for t, threshold in enumerate(THRESHOLDS):
                directed = find_cutoff(n_bits, threshold, mode)
                nearest = rows[n][t]
                if directed != nearest:
                    differences += 1
                    bits = min(directed, nearest)
                    actual = original_predicate(bits, n_bits, threshold, mode)
                    replacement = bits < nearest
                    assert actual != replacement
                    if witness is None:
                        witness = {
                            "N": n, "threshold_index": t,
                            "x_bits": f"0x{bits:08x}",
                            "original_predicate": actual,
                            "RN_cutoff_predicate": replacement,
                        }
        if not differences:
            raise AssertionError(f"expected an RN-cutoff counterexample under {mode}")
        directed_cases.append({
            "rounding_mode": mode, "cutoffs_different_from_RN": differences,
            "counterexample": witness,
        })
    return {
        "result": "PASS",
        "oracle": "integer-only IEEE-754 binary32 RN ties-to-even, two separately rounded products",
        "boundary_ulps_each_side": boundary_ulps,
        "random_float_bit_patterns_per_N": random_samples,
        "random_predicates_per_N": 3 * random_samples,
        "checked_predicates": checked,
        "directed_rounding_counterexamples": directed_cases,
        "cutoffs": report_rows,
        "scope": "Numerical correctness and monotonicity proof; no performance measurement",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "header", nargs="?", type=pathlib.Path,
        default=pathlib.Path("/workspace/libopus-opt/celt/arm/spreading_cutoffs.h")
    )
    parser.add_argument("--boundary-ulps", type=int, default=4096)
    parser.add_argument("--random-samples", type=int, default=65536)
    parser.add_argument("--json", type=pathlib.Path)
    args = parser.parse_args()
    if args.boundary_ulps < 1 or args.random_samples < 0:
        parser.error("boundary-ulps must be positive and random-samples nonnegative")
    self_test()
    report = verify(parse_header(args.header), args.boundary_ulps, args.random_samples)
    report["header_sha256"] = hashlib.sha256(args.header.read_bytes()).hexdigest()
    report["verifier_sha256"] = hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest()
    report["table_entries"] = 177
    report["zero_entries_for_unsupported_N"] = 162
    report["derived_cutoff_shift_bits"] = [0, 0x00800000, 0x01000000]
    if args.json:
        args.json.write_text(json.dumps(report, indent=2) + "\n")
    print(
        f"PASS: 45 exact cutoffs; {report['checked_predicates']:,} integer-oracle "
        f"predicates; +/-{args.boundary_ulps} ULPs with both signs and "
        f"{args.random_samples:,} random float bit patterns per N"
    )


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, OSError, ValueError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
