# Runs every strategy over a trace - synthetic or captured in game with -sync-trace -
# and prints the scoreboard. The whole point of the lab in one file: same input, every
# contender, numbers instead of impressions.
#
#   python replay.py --synth rimtek --kind vehicle
#   python replay.py --synth all
#   python replay.py --trace sync-trace-XXXX.ndjson [--validate] [--plot out.png]
import argparse
import json
import math
import sys

from netmodel import PROFILES
from strategies import ALL, Baseline
import synth


def _dist(a, b):
    return math.sqrt(sum((a[i]-b[i])**2 for i in range(3)))


def run(records, kind, update_rate=30, truth=None, epoch=1_787_000_000_000,
        frame_ms=16.0, strategies=None):
    """records: 'in' dicts sorted by tr. Renders at frame_ms cadence between the first
    and last arrival, feeding each strategy only what has arrived by that frame."""
    strategies = strategies or [S(kind, update_rate) for S in ALL]
    start = records[0]["tr"]
    end = records[-1]["tr"] + 500
    results = {s.name: {"out": [], "starved": 0, "frames": 0} for s in strategies}

    idx = 0
    now = start
    while now <= end:
        while idx < len(records) and records[idx]["tr"] <= now:
            for s in strategies:
                s.ingest(dict(records[idx]))
            idx += 1
        for s in strategies:
            pos = s.render(now)
            r = results[s.name]
            r["frames"] += 1
            if pos is None:
                r["starved"] += 1
                if r["out"]:
                    r["out"].append((now, r["out"][-1][1]))   # hold last pose, like the game
            else:
                r["out"].append((now, pos))
        now += frame_ms

    board = []
    for s in strategies:
        r = results[s.name]
        out = r["out"]
        if len(out) < 3:
            continue
        # pops: displacement beyond what any sane speed covers in a frame
        max_step = (12.0 if kind == "player" else 45.0) * (frame_ms / 1000.0) * 3.0
        pops = sum(1 for i in range(1, len(out)) if _dist(out[i][1], out[i-1][1]) > max_step)
        # jerk proxy: mean |second difference|
        jerk = sum(_dist([out[i][1][j] - out[i-1][1][j] for j in range(3)] + [0]*0,
                         [out[i-1][1][j] - out[i-2][1][j] for j in range(3)])
                   for i in range(2, len(out))) / (len(out) - 2) * 1000.0 / frame_ms
        row = {"name": s.name, "pops": pops, "jerk": jerk,
               "starve%": 100.0 * r["starved"] / max(1, r["frames"])}
        if truth is not None:
            # error + effective delay vs truth: for each rendered frame, distance to the
            # truth position at that WALL time, and the time-shift that minimises error.
            tmap = {round(t.t): t.pos for t in truth}
            errs = []
            for t_ms, pos in out[::4]:
                key = round(t_ms - epoch)
                tp = tmap.get(key) or tmap.get(key - (key % 16)) or tmap.get(key - (key % 16) + 16)
                if tp:
                    errs.append(_dist(pos, list(tp)))
            errs.sort()
            if errs:
                row["err_mean"] = sum(errs) / len(errs)
                row["err_p95"] = errs[int(0.95 * (len(errs) - 1))]
            # effective delay: shift that best aligns rendered x with truth x
            best = (None, None)
            for shift in range(0, 400, 10):
                tot, n = 0.0, 0
                for t_ms, pos in out[::8]:
                    key = round(t_ms - epoch - shift)
                    tp = tmap.get(key - (key % 16))
                    if tp:
                        tot += _dist(pos, list(tp)); n += 1
                if n and (best[1] is None or tot / n < best[1]):
                    best = (shift, tot / n)
            row["delay_ms"] = best[0]
        board.append(row)
    return board


def print_board(title, board, truth):
    cols = ["name", "err_mean", "err_p95", "pops", "jerk", "starve%", "delay_ms"] if truth \
        else ["name", "pops", "jerk", "starve%"]
    print(f"\n=== {title}")
    print("  " + "".join(f"{c:>10}" for c in cols))
    for row in board:
        cells = []
        for c in cols:
            v = row.get(c, "-")
            cells.append(f"{v:>10.3f}" if isinstance(v, float) else f"{str(v):>10}")
        print("  " + "".join(cells))


def load_trace(path):
    ins, outs = [], []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            rec = json.loads(line)
            (ins if rec.get("k") == "in" else outs).append(rec)
    ins.sort(key=lambda r: r["tr"])
    return ins, outs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--synth", help="profile name or 'all'")
    ap.add_argument("--trace", help="sync-trace ndjson from a real session")
    ap.add_argument("--kind", default="player", choices=["player", "vehicle"])
    ap.add_argument("--rate", type=int, default=30)
    ap.add_argument("--validate", action="store_true",
                    help="trace mode: compare python baseline to the game's own 'out' records")
    ap.add_argument("--plot", help="write a PNG comparing paths (needs matplotlib)")
    args = ap.parse_args()

    if args.synth:
        profiles = PROFILES.keys() if args.synth == "all" else [args.synth]
        kinds = ["player", "vehicle"] if args.synth == "all" else [args.kind]
        for pname in profiles:
            for kind in kinds:
                truth = synth.make_truth(kind)
                wire = synth.sample_wire(truth, args.rate)
                records = synth.receive(wire, PROFILES[pname])
                board = run(records, kind, args.rate, truth=truth)
                print_board(f"{pname} / {kind}", board, truth=True)
                if args.plot:
                    plot(args.plot, truth, records, kind, args.rate, pname)
        return

    if args.trace:
        ins, outs = load_trace(args.trace)
        if not ins:
            sys.exit("no 'in' records in that trace")
        by_id = {}
        for r in ins:
            by_id.setdefault(r["id"], []).append(r)
        ent = max(by_id, key=lambda k: len(by_id[k]))
        records = by_id[ent]
        kind = "vehicle" if records[0].get("veh") else "player"
        print(f"entity {ent}: {len(records)} samples, kind={kind}")
        board = run(records, kind, args.rate)
        print_board(f"trace {args.trace} / {ent}", board, truth=False)
        if args.validate and outs:
            validate(records, [o for o in outs if o["id"] == ent], kind, args.rate)
        return

    ap.print_help()


def validate(records, outs, kind, rate):
    """Replays the python baseline on the trace's own arrival times and reports how far
    it lands from what the C++ actually rendered - the trust anchor for the whole lab."""
    s = Baseline(kind, rate)
    omap = sorted((o["rt"], o["p"]) for o in outs)
    idx, errs = 0, []
    for rt, real in omap:
        while idx < len(records) and records[idx]["tr"] <= rt + s.delay:
            s.ingest(dict(records[idx])); idx += 1
        mine = s.render(rt + s.delay)
        if mine is not None:
            errs.append(_dist(mine, real))
    if errs:
        errs.sort()
        print(f"validate: python-baseline vs game render - mean {sum(errs)/len(errs):.3f}m, "
              f"p95 {errs[int(0.95*(len(errs)-1))]:.3f}m over {len(errs)} frames "
              f"(>0.5m p95 means the port has drifted from the C++ - fix the port first)")


def plot(path, truth, records, kind, rate, pname):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    fig, ax = plt.subplots(figsize=(9, 7))
    ax.plot([t.pos[0] for t in truth], [t.pos[1] for t in truth],
            color="#9aa3b2", lw=3, alpha=.4, label="truth")
    for S in ALL:
        s = S(kind, rate)
        board_out = []
        idx, now = 0, records[0]["tr"]
        end = records[-1]["tr"] + 500
        while now <= end:
            while idx < len(records) and records[idx]["tr"] <= now:
                s.ingest(dict(records[idx])); idx += 1
            p = s.render(now)
            if p:
                board_out.append(p)
            now += 16.0
        ax.plot([p[0] for p in board_out], [p[1] for p in board_out], lw=1, label=s.name)
    ax.set_title(f"{pname} / {kind}")
    ax.legend()
    ax.set_aspect("equal")
    fig.savefig(path, dpi=120, facecolor="white")
    print(f"plot -> {path}")


if __name__ == "__main__":
    main()
