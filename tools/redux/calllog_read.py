#!/usr/bin/env python3
"""Read a XeniaRedux call log (--call_log_functions, --call_log_path).

The log holds, in the order they happened across all guest threads, one record
as each chosen function starts (entry) and one as it returns (exit): guest
thread, sequence number, LR, r1, r3-r10 and f1-f13. At an exit, r3 (and f1 for
floating-point results) hold the return values. Layout: src/xenia/cpu/call_log.h.

Commands:
  summary LOG            calls per function and whether Xenia can be trusted
  dump LOG [-f ADDR] [-n N]   the records, one per line
  pairs LOG [-f ADDR] [-o OUT.jsonl]   matched entry/exit pairs as JSON lines
  trust LOG [-o OUT.txt] one line per function: trusted / caution / not-trusted

Trust (DP's judge, not Xenia, settles a disagreement in a not-trusted function):
  not-trusted  estimate instructions (fres, frsqrte, vrefp, vrsqrtefp,
               vexptefp, vlogefp: Xenia computes exact values, the Xenon CPU
               returns table estimates), or fused multiply-add (fmadd family,
               vmaddfp, vnmsubfp: fused only when the host CPU has FMA)
  caution      stfs (an out-of-range double rounds in Xenia; the hardware
               selects bits), fctiw/fctiwz (compare the low word only), traps
               (ignored by Xenia by default)
"""
import argparse
import json
import pathlib
import struct
import sys
from collections import defaultdict

MAGIC = b"XRCL"
HEADER_SIZE = 64
RECORD = struct.Struct("<4I4Q8Q13Q")  # 216 bytes
ENTRY, EXIT, FUNCTION_INFO, TRUNCATED, HOST_INFO = 1, 2, 3, 4, 5
FLAGS = ["estimate", "fma", "stfs", "fctiw", "trap"]
NOT_TRUSTED = {"estimate", "fma"}


class Record:
    __slots__ = ("kind", "thread", "function", "lr", "seq", "entry_seq", "vblank", "r1", "r", "f")

    def __init__(self, t):
        (self.kind, self.thread, self.function, self.lr, self.seq, self.entry_seq,
         self.vblank, self.r1) = t[:8]
        self.r = t[8:16]
        self.f = t[16:29]

    def fdouble(self, i):
        return struct.unpack("<d", struct.pack("<Q", self.f[i]))[0]

    def regs(self):
        return {"lr": f"{self.lr:08x}", "r1": f"{self.r1:016x}",
                **{f"r{3 + i}": f"{v:016x}" for i, v in enumerate(self.r)},
                **{f"f{1 + i}": f"{v:016x}" for i, v in enumerate(self.f)}}


def read(path):
    """(records, info) with info = {'host_fma', 'functions': {addr: (end, flags, counts)}, 'truncated'}."""
    data = pathlib.Path(path).read_bytes()
    if data[:4] != MAGIC:
        sys.exit(f"{path}: not a XeniaRedux call log")
    version, rec_size, hdr_size = struct.unpack_from("<3I", data, 4)
    if version != 1 or rec_size != RECORD.size or hdr_size != HEADER_SIZE:
        sys.exit(f"{path}: unsupported layout (version {version}, record {rec_size})")
    info = {"host_fma": None, "functions": {}, "truncated": False, "partial_tail": 0}
    records = []
    end = len(data) - (len(data) - HEADER_SIZE) % RECORD.size
    info["partial_tail"] = len(data) - end  # a record cut short by a kill
    for pos in range(HEADER_SIZE, end, RECORD.size):
        r = Record(RECORD.unpack_from(data, pos))
        if r.kind == FUNCTION_INFO:
            info["functions"][r.function] = (r.lr, r.entry_seq, r.r[:len(FLAGS)])
        elif r.kind == HOST_INFO:
            info["host_fma"] = bool(r.entry_seq & 1)
        elif r.kind == TRUNCATED:
            info["truncated"] = True
        elif r.kind in (ENTRY, EXIT):
            records.append(r)
    return records, info


def verdict(flags):
    names = [n for i, n in enumerate(FLAGS) if flags >> i & 1]
    bad = [n for n in names if n in NOT_TRUSTED]
    if bad:
        return "not-trusted", names
    return ("caution" if names else "trusted"), names


def flag_text(entry, host_fma):
    end, flags, counts = entry
    level, names = verdict(flags)
    parts = [f"{n} x{counts[FLAGS.index(n)]}" for n in names]
    if "fma" in names:
        parts.append("host FMA " + ("on: fused" if host_fma else "off: rounded twice"))
    return level, ", ".join(parts)


def pairs(records):
    by_seq = {r.seq: r for r in records if r.kind == ENTRY}
    for r in records:
        if r.kind == EXIT:
            yield by_seq.get(r.entry_seq), r


def cmd_summary(a):
    records, info = read(a.log)
    calls = defaultdict(lambda: [0, 0, 0, set()])
    for r in records:
        c = calls[r.function]
        c[3].add(r.thread)
        if r.kind == ENTRY:
            c[0] += 1
        elif r.entry_seq:
            c[1] += 1
        else:
            c[2] += 1
    print(f"{a.log}: {len(records)} records, host FMA {info['host_fma']}, "
          f"{'TRUNCATED at size cap' if info['truncated'] else 'complete'}"
          f"{', last record cut short' if info['partial_tail'] else ''}")
    if records:
        print(f"seq {records[0].seq}..{records[-1].seq}, vblank {records[0].vblank}..{records[-1].vblank}")
    print(f"{'function':>10} {'entries':>8} {'exits':>8} {'unmatched':>9} {'threads':>7}  trust")
    for fn in sorted(set(calls) | set(info["functions"])):
        e, x, u, th = calls[fn]
        level, why = flag_text(info["functions"][fn], info["host_fma"]) if fn in info["functions"] else ("?", "not compiled")
        print(f"{fn:10x} {e:8d} {x:8d} {u:9d} {len(th):7d}  {level}{': ' + why if why else ''}")


def cmd_dump(a):
    records, _ = read(a.log)
    want = int(a.function, 16) if a.function else None
    shown = 0
    for r in records:
        if want is not None and r.function != want:
            continue
        kind = "ENTER" if r.kind == ENTRY else "EXIT "
        link = f" of {r.entry_seq}" if r.kind == EXIT else ""
        args = " ".join(f"r{3 + i}={v:x}" for i, v in enumerate(r.r))
        fl = " ".join(f"f{1 + i}={r.fdouble(i):.9g}" for i in range(4))
        print(f"{r.seq:8d} vb{r.vblank:6d} t{r.thread:<3d} {kind} {r.function:08x}{link} lr={r.lr:08x} {args} {fl}")
        shown += 1
        if a.n and shown >= a.n:
            break


def cmd_pairs(a):
    records, info = read(a.log)
    want = int(a.function, 16) if a.function else None
    out = open(a.out, "w", encoding="utf-8") if a.out else sys.stdout
    n = 0
    for entry, exit_ in pairs(records):
        if entry is None or (want is not None and exit_.function != want):
            continue
        level, _ = flag_text(info["functions"].get(exit_.function, (0, 0, [0] * 5)), info["host_fma"])
        out.write(json.dumps({"function": f"{exit_.function:08x}", "thread": entry.thread,
                              "seq": entry.seq, "vblank": entry.vblank, "xenia": level,
                              "in": entry.regs(), "out": exit_.regs()}) + "\n")
        n += 1
    if a.out:
        out.close()
        print(f"{n} pairs -> {a.out}")


def cmd_trust(a):
    _, info = read(a.log)
    lines = []
    for fn in sorted(info["functions"]):
        level, why = flag_text(info["functions"][fn], info["host_fma"])
        lines.append(f"{fn:08x} {level}{'  # ' + why if why else ''}")
    text = "\n".join(lines) + "\n"
    if a.out:
        pathlib.Path(a.out).write_text(text, encoding="ascii")
        print(f"{len(lines)} functions -> {a.out}")
    else:
        sys.stdout.write(text)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name, fn in (("summary", cmd_summary), ("dump", cmd_dump), ("pairs", cmd_pairs), ("trust", cmd_trust)):
        p = sub.add_parser(name)
        p.add_argument("log")
        p.set_defaults(fn=fn)
        if name in ("dump", "pairs"):
            p.add_argument("-f", "--function", help="only this function (hex)")
        if name == "dump":
            p.add_argument("-n", type=int, default=0, help="at most N records")
        if name in ("pairs", "trust"):
            p.add_argument("-o", "--out")
    a = ap.parse_args(argv)
    a.fn(a)


if __name__ == "__main__":
    main()
