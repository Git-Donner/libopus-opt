#!/usr/bin/env python3
"""Independent exact-integer IEEE binary32 RNE cutoff verification.

No host floating-point arithmetic participates in finding or validating a
cutoff.  This script is a review artifact, not part of the Opus build.
"""
import struct

NS = [12, 16, 18, 22, 24, 32, 36, 44, 48, 64, 72, 88, 96, 144, 176]
TS = [0x3E800000, 0x3D800000, 0x3C800000]


def components(bits):
    exp, frac = (bits >> 23) & 255, bits & 0x7fffff
    assert exp < 255
    return ((frac | 0x800000), exp - 150) if exp else (frac, -149)


def rne_shift(n, shift):
    if shift <= 0:
        return n << -shift
    q, r = divmod(n, 1 << shift)
    h = 1 << (shift - 1)
    return q + (r > h or (r == h and (q & 1)))


def pack_rne(m, e):
    if not m:
        return 0
    exp = m.bit_length() - 1 + e
    if exp < -126:
        return rne_shift(m, -149 - e)
    q = rne_shift(m, exp - 23 - e)
    if q == 1 << 24:
        q >>= 1
        exp += 1
    if exp > 127:
        return 0x7f800000
    return ((exp + 127) << 23) | (q & 0x7fffff)


def two_multiply(bits, n):
    if bits >= 0x7f800000:
        return 0x7f800000
    m, e = components(bits)
    sq = pack_rne(m * m, e * 2)
    if sq == 0x7f800000:
        return sq
    m, e = components(sq)
    return pack_rne(m * n, e)


def first_fail(n, t):
    lo, hi = 0, 0x7f800000
    while lo < hi:
        mid = (lo + hi) // 2
        if two_multiply(mid, n) < t:
            lo = mid + 1
        else:
            hi = mid
    return lo


def as_float(bits):
    return struct.unpack('>f', struct.pack('>I', bits))[0]


def main():
    checks = 0
    print('N cutoff(T=.25) cutoff(T=.0625) cutoff(T=.015625)')
    for n in NS:
        cutoffs = [first_fail(n, t) for t in TS]
        for cutoff, t in zip(cutoffs, TS):
            assert two_multiply(cutoff - 1, n) < t
            assert two_multiply(cutoff, n) >= t
            # Thresholds differ by exactly two powers of two. Away from any
            # subnormal/overflow region the first-failing x scales by 1/2.
            for offset in range(-65536, 65537):
                b = cutoff + offset
                assert (two_multiply(b, n) < t) == (b < cutoff)
                checks += 1
        assert cutoffs[0] - cutoffs[1] == 0x800000
        assert cutoffs[1] - cutoffs[2] == 0x800000
        print(f'{n:3d} ' + ' '.join(f'0x{x:08x}' for x in cutoffs))
    print(f'PASS: {checks} exact comparisons plus all predecessor/fail boundaries')


if __name__ == '__main__':
    main()
