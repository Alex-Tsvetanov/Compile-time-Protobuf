#!/usr/bin/env python3
"""Turn eval_runtime_split.sh output into medians, ranges, and LaTeX rows.

Two programs, alternated across reps, so a median over every round of every rep
spreads drift across both arms rather than letting it settle on whichever ran
last. The range is printed because a tight range is the only evidence that the
median means anything.

    ./eval_runtime_split.sh | tee runtime_raw.txt
    ./summarize_runtime.py runtime_raw.txt
"""
import sys
from statistics import median


def fmt(vals):
    return f"{median(vals):.2f} [{min(vals):.2f}, {max(vals):.2f}]"


def main(path):
    text = open(path).read() if path != "-" else sys.stdin.read()

    enc, dec, payload, parity = {}, {}, set(), None
    for line in text.splitlines():
        if line.startswith("wire_parity="):
            parity = line.split("=", 1)[1].split()[0]
        if not line.startswith("rep="):
            continue
        f = dict(kv.split("=", 1) for kv in line.split())
        arm = f["arm"]
        enc.setdefault(arm, []).append(float(f["enc_ns"]))
        dec.setdefault(arm, []).append(float(f["dec_ns"]))
        payload.add(int(f["payload_bytes"]))

    if not enc:
        sys.exit("no `rep=` lines found -- is this eval_runtime_split.sh output?")
    if parity != "identical":
        sys.exit(f"refusing to summarise: wire_parity={parity!r}. The arms did "
                 f"not encode the same bytes, so the timings are not comparable.")
    if len(payload) != 1:
        sys.exit(f"refusing to summarise: arms disagree on payload size {payload}")

    size = payload.pop()
    n = len(next(iter(enc.values())))
    print(f"# throughput, median [min, max] over {n} rounds per arm, "
          f"{size}-byte message, wire bytes identical across arms\n")
    print(f"{'operation':>10} {'ctpb (ns/op)':>24} {'libprotobuf (ns/op)':>24} {'ratio':>8}")
    rows = []
    for label, d in (("encode", enc), ("decode", dec)):
        c, p = d["ctpb"], d["protobuf"]
        ratio = median(p) / median(c)
        rows.append((label, c, p, ratio))
        print(f"{label:>10} {fmt(c):>24} {fmt(p):>24} {ratio:>7.2f}x")

    def mib(ns):
        return (size / (ns * 1e-9)) / (1024.0 * 1024.0)

    print(f"\n{'operation':>10} {'ctpb (MiB/s)':>16} {'libprotobuf (MiB/s)':>22}")
    for label, c, p, _ in rows:
        print(f"{label:>10} {mib(median(c)):>16,.0f} {mib(median(p)):>22,.0f}")

    print("\n% --- LaTeX rows ---")
    for label, c, p, ratio in rows:
        print(f"{label.capitalize()} & \\textbf{{{median(c):.2f}}} "
              f"[{min(c):.2f}, {max(c):.2f}] & {median(p):.2f} "
              f"[{min(p):.2f}, {max(p):.2f}] & {ratio:.2f}$\\times$ \\\\")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "-")
