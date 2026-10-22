"""Which fma model do the hardware-generated Xenia vectors (instr__gen_fmadd*.s) follow?

    python -I fma_vectors.py DIR

For each finite case: fused = one rounding of a*c+b; two = round(round(a*c)+b) (Ghidra's p-code).
Counts the cases where the two models disagree and which one the recorded result matches.
"""
import math
import pathlib
import re
import struct
import sys
from fractions import Fraction

CASE = re.compile(r"^(test_\w+?):\s*$")
REG = re.compile(r"#_ REGISTER_(IN|OUT) (f\d+) 0x([0-9A-Fa-f]+)")
OPS = {"fmadd": (1, 1), "fmsub": (1, -1), "fnmadd": (-1, 1), "fnmsub": (-1, -1)}


def d(bits):
    return struct.unpack(">d", bytes.fromhex(f"{bits:016x}"))[0]


def bits_d(x):
    return int.from_bytes(struct.pack(">d", x), "big")


def round_fmt(q, mant, emin, emax):
    """q (Fraction) rounded to nearest-even in a binary format with `mant` significand bits; None on overflow."""
    if q == 0:
        return 0.0
    s = -1 if q < 0 else 1
    q = abs(q)
    e = math.floor(math.log2(q.numerator) - math.log2(q.denominator))
    while Fraction(2) ** e > q:
        e -= 1
    while Fraction(2) ** (e + 1) <= q:
        e += 1
    e = max(e, emin)
    scale = Fraction(2) ** (e - (mant - 1))
    m = q / scale
    n = math.floor(m)
    r = m - n
    if r > Fraction(1, 2) or (r == Fraction(1, 2) and n % 2):
        n += 1
    v = n * scale
    if v >= Fraction(2) ** (emax + 1):
        return None
    return s * float(v)


def rd(q):
    return round_fmt(q, 53, -1022, 1023)


def rs(q):
    return round_fmt(q, 24, -126, 127)


def main(folder):
    for name in ("fmadd", "fmadds"):
        text = (pathlib.Path(folder) / f"instr__gen_{name}.s").read_text()
        stats = {"cases": 0, "differ": 0, "fused": 0, "two": 0, "neither": 0}
        examples = []
        cur = None
        for line in text.splitlines():
            m = CASE.match(line)
            if m:
                cur = {"IN": {}, "OUT": {}, "op": None, "name": m.group(1)}
                continue
            if cur is None:
                continue
            m = REG.search(line)
            if m:
                cur[m.group(1)][m.group(2)] = int(m.group(3), 16)
                continue
            parts = line.split()
            if parts and parts[0].rstrip(".") in (name,):
                cur["op"] = [p.strip(",") for p in parts[1:]]
            if parts and parts[0] == "blr":
                continue
            if line.strip() == "" and cur.get("op") and cur["OUT"]:
                evaluate(cur, name, stats, examples)
                cur = None
        print(name, stats)
        for ex in examples[:4]:
            print("  ", ex)


def evaluate(cur, name, stats, examples):
    dst, fa, fc, fb = cur["op"]
    try:
        a, c, b = (d(cur["IN"][r]) for r in (fa, fc, fb))
        want = cur["OUT"][dst]
    except KeyError:
        return
    if not all(math.isfinite(x) for x in (a, b, c)):
        return
    exact = Fraction(a) * Fraction(c) + Fraction(b)
    single = name.endswith("s")
    fused = (rs if single else rd)(exact)
    p = rd(Fraction(a) * Fraction(c))
    if fused is None or p is None:
        return
    two = rd(Fraction(p) + Fraction(b))
    if two is None:
        return
    if single:
        two = rs(Fraction(two))
        if two is None:
            return
    stats["cases"] += 1
    fb_, tb_ = bits_d(fused), bits_d(two)
    if fb_ == tb_:
        return
    stats["differ"] += 1
    if want == fb_:
        stats["fused"] += 1
    elif want == tb_:
        stats["two"] += 1
    else:
        stats["neither"] += 1
    examples.append((cur["name"], f"want {want:016x} fused {fb_:016x} two {tb_:016x}"))


if __name__ == "__main__":
    main(sys.argv[1])
