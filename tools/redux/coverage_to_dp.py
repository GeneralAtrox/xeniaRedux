#!/usr/bin/env python3
"""Turn Xenia's --trace_function_coverage data into DecompPipeline's coverage.txt.

Xenia (with --trace_functions --trace_function_coverage
--trace_function_data_path=X.bin) writes X.0, X.1, ...: one block per compiled
function with a run count for each of its instructions. DP's coverage.txt is the
start address of every block of code that ran, one hex address per line (the
Flycast reference lists the blocks its dynarec compiled; a block ends at every
branch and starts wherever control enters it).

This script rebuilds the same set from the run counts. An instruction X that ran
starts a block when:
  - X is a function's first instruction, or
  - the instruction before X did not run (or is in no traced function), or
  - the instruction before X is a branch (b, bc, bclr, bcctr, with or without
    link): a block ends there, so whatever runs next starts one, or
  - X ran more often than the instruction before it: something jumped to X.
The last two need the instruction words, read from the decrypted game image
(DP's decomp/image.bin; --image, --base).

Usage:
  coverage_to_dp.py TRACE.bin --image image.bin --base 0x82000000 -o coverage.txt
"""
import argparse
import pathlib
import struct
import sys

HEADER = struct.Struct("<IIIIQQ4I")  # FunctionTraceData::Header, 48 bytes


def chunk_paths(trace):
    """X.bin -> X.0, X.1, ... (Xenia's ChunkedMappedMemoryWriter names)."""
    trace = pathlib.Path(trace)
    if trace.suffix[1:].isdigit() and trace.exists():
        return [trace]
    out = []
    i = 0
    while (p := trace.with_suffix(f".{i}")).exists():
        out.append(p)
        i += 1
    return out


def read_functions(paths):
    """{start: (end, call_count, [instruction run counts])}"""
    funcs = {}
    for path in paths:
        data = path.read_bytes()
        pos = 0
        while pos + HEADER.size <= len(data):
            size, start, end, _type, _threads, calls, *_hist = HEADER.unpack_from(data, pos)
            if size == 0:
                break  # the unused tail of the chunk
            n = (size - HEADER.size) // 8
            counts = list(struct.unpack_from(f"<{n}Q", data, pos + HEADER.size)) if n else []
            old = funcs.get(start)
            if old and old[2] and counts:  # compiled twice: add the runs
                counts = [a + b for a, b in zip(old[2], counts)]
                calls += old[1]
            funcs[start] = (end, calls, counts)
            pos += size
    return funcs


def is_branch(word):
    op = word >> 26
    if op in (16, 18):  # bc, b
        return True
    return op == 19 and ((word >> 1) & 0x3FF) in (16, 528)  # bclr, bcctr


def block_starts(funcs, image, base):
    ran = {}
    for start, (end, calls, counts) in funcs.items():
        for i, c in enumerate(counts):
            if c:
                ran[start + 4 * i] = c
    starts = set()
    for start, (end, calls, counts) in funcs.items():
        if calls and not counts:
            starts.add(start)  # coverage off for this one: at least it was entered
    for x, c in ran.items():
        prev = ran.get(x - 4)
        if prev is None or x in funcs or c > prev:
            starts.add(x)
            continue
        off = x - 4 - base
        if 0 <= off <= len(image) - 4 and is_branch(struct.unpack_from(">I", image, off)[0]):
            starts.add(x)
    return starts, ran


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("trace", help="the --trace_function_data_path given to Xenia (or one chunk, X.0)")
    ap.add_argument("--image", required=True, help="decrypted game image (DP decomp/image.bin)")
    ap.add_argument("--base", default="0x82000000", help="load address of the image")
    ap.add_argument("-o", "--out", default="coverage.txt")
    a = ap.parse_args(argv)
    paths = chunk_paths(a.trace)
    if not paths:
        sys.exit(f"no trace chunks for {a.trace}")
    funcs = read_functions(paths)
    image = pathlib.Path(a.image).read_bytes()
    starts, ran = block_starts(funcs, image, int(a.base, 0))
    pathlib.Path(a.out).write_text("".join(f"{x:08X}\n" for x in sorted(starts)), encoding="ascii")
    entered = sum(1 for f in funcs.values() if f[1])
    print(f"{len(paths)} chunk(s), {len(funcs)} functions compiled, {entered} entered, "
          f"{len(ran)} instructions ran, {len(starts)} block starts -> {a.out}")


if __name__ == "__main__":
    main()
