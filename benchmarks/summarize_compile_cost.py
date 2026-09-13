#!/usr/bin/env python3
"""Turn eval_compile_cost.sh output into medians, ranges, and LaTeX table rows.

The paper reports a median and a full range over the retained trials, with trial 0
dropped as a warm-up. Doing that by hand across two arms and three schema sizes is
how transcription errors get into a table, so it is done here instead: feed this
the raw stdout and paste what it prints.

    ./eval_compile_cost.sh | tee raw.txt
    ./summarize_compile_cost.py raw.txt
"""
import re
import sys
from statistics import median

WARMUP_TRIAL = 0  # excluded, as in the paper


def main(path):
    text = open(path).read() if path != "-" else sys.stdin.read()

    build = {}       # (fields, arm) -> [seconds]
    breakdown = {}   # fields -> {stage: [seconds]}
    foot = {}        # (fields, arm) -> dict of byte counts
    lines_gen = {}

    for line in text.splitlines():
        if line.startswith("trial="):
            f = dict(kv.split("=", 1) for kv in line.split())
            if int(f["trial"]) == WARMUP_TRIAL:
                continue
            n = int(f["fields"])
            build.setdefault((n, "ctpb"), []).append(float(f["ctpb_build_s"]))
            build.setdefault((n, "protoc"), []).append(float(f["protoc_build_s"]))
            stages = breakdown.setdefault(n, {})
            for k, v in f.items():
                if k.startswith("protoc_") and k != "protoc_build_s":
                    stages.setdefault(k[len("protoc_"):-len("_s")], []).append(float(v))
        elif line.startswith("footprint "):
            f = dict(kv.split("=", 1) for kv in line.split()[1:])
            foot[(int(f["fields"]), f["arm"])] = {
                k: (int(v) if v.isdigit() else None)
                for k, v in f.items() if k.endswith("_bytes")
            }
        elif line.startswith("generated_lines "):
            f = dict(kv.split("=", 1) for kv in line.split()[1:])
            lines_gen[int(f["fields"])] = int(f["protoc"])

    if not build:
        sys.exit("no `trial=` lines found -- is this eval_compile_cost.sh output?")

    sizes = sorted({n for n, _ in build})
    retained = len(next(iter(build.values())))
    print(f"# build cost, median [min, max] over {retained} retained trials "
          f"(trial {WARMUP_TRIAL} excluded as warm-up)\n")
    print(f"{'fields':>7} {'ctpb (s)':>22} {'protoc (s)':>22} {'ratio':>8}")
    for n in sizes:
        c, p = build[(n, "ctpb")], build[(n, "protoc")]
        mc, mp = median(c), median(p)
        print(f"{n:>7} {f'{mc:.2f} [{min(c):.2f}, {max(c):.2f}]':>22}"
              f" {f'{mp:.2f} [{min(p):.2f}, {max(p):.2f}]':>22} {mc / mp:>7.2f}x")

    print("\n# where the protoc arm's time goes (medians, s)\n")
    stage_names = sorted(next(iter(breakdown.values())))
    print(f"{'fields':>7} " + " ".join(f"{s:>20}" for s in stage_names))
    for n in sizes:
        print(f"{n:>7} " + " ".join(f"{median(breakdown[n][s]):>20.2f}"
                                    for s in stage_names))

    if foot:
        print("\n# footprint, exact sections (bytes)\n")
        print(f"{'fields':>7} {'arm':>7} {'file':>10} {'.text':>10} "
              f"{'.rodata':>10} {'.data':>10}")
        for n in sizes:
            for arm in ("ctpb", "protoc"):
                d = foot.get((n, arm))
                if not d:
                    continue
                cell = lambda k: (f"{d.get(k):,}" if d.get(k) is not None else "n/a")
                print(f"{n:>7} {arm:>7} {cell('file_bytes'):>10} "
                      f"{cell('text_bytes'):>10} {cell('rodata_bytes'):>10} "
                      f"{cell('data_bytes'):>10}")

    print("\n% --- LaTeX rows: build cost ---")
    for n in sizes:
        c, p = build[(n, "ctpb")], build[(n, "protoc")]
        print(f"{n} & \\textbf{{{median(c):.2f}}} [{min(c):.2f}, {max(c):.2f}] & "
              f"{median(p):.2f} [{min(p):.2f}, {max(p):.2f}] & "
              f"{median(c) / median(p):.2f}$\\times$ \\\\")

    if foot:
        print("\n% --- LaTeX rows: exact .text ---")
        for n in sizes:
            c = foot.get((n, "ctpb"), {}).get("text_bytes")
            p = foot.get((n, "protoc"), {}).get("text_bytes")
            if c is None or p is None:
                continue
            tex = lambda v: f"{v:,}".replace(",", "{,}")  # 13,315 -> 13{,}315
            print(f"{n} & \\textbf{{{tex(c)}}}\\,B & {tex(p)}\\,B \\\\")

    if lines_gen:
        print("\n% generated source lines: " +
              ", ".join(f"{n} fields -> {lines_gen[n]}" for n in sizes
                        if n in lines_gen))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "-")
