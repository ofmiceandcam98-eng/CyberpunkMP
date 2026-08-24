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
        steps = [_dist(out[i][1], out[i-1][1]) for i in range(1, len(out))]
        pops = sum(1 for step in steps if step > max_step)
        correction_distance = sum(max(0.0, step - max_step) for step in steps)
        max_correction = max([max(0.0, step - max_step) for step in steps] or [0.0])
        # jerk proxy: mean |second difference|
        jerk = sum(_dist([out[i][1][j] - out[i-1][1][j] for j in range(3)] + [0]*0,
                         [out[i-1][1][j] - out[i-2][1][j] for j in range(3)])
                   for i in range(2, len(out))) / (len(out) - 2) * 1000.0 / frame_ms
        row = {"name": s.name, "pops": pops, "correction_m": correction_distance,
             "max_correction_m": max_correction, "jerk": jerk,
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
    cols = ["name", "err_mean", "err_p95", "pops", "correction_m", "jerk", "starve%", "delay_ms"] if truth \
        else ["name", "pops", "correction_m", "jerk", "starve%"]
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
    with open(path, encoding="utf-8-sig") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            rec = json.loads(line)
            (ins if rec.get("k") == "in" else outs).append(rec)
    ins.sort(key=lambda r: r["tr"])
    return ins, outs


def validate_trace_metadata(records, cell_size=60000, world_revision=1):
    """Return integrity errors for server-derived map and authority metadata."""
    errors = []
    last_sequence = None
    last_epoch = None
    for index, record in enumerate(records):
        if record.get("wr", world_revision) != world_revision:
            errors.append(f"record {index}: world revision {record.get('wr')} != {world_revision}")
        if cell_size:
            expected_x = math.floor(record["p"][0] / cell_size)
            expected_y = math.floor(record["p"][1] / cell_size)
            if record.get("cx", expected_x) != expected_x or record.get("cy", expected_y) != expected_y:
                errors.append(f"record {index}: cell does not match position")
        sequence = record.get("seq")
        if sequence is not None and last_sequence is not None and sequence <= last_sequence:
            errors.append(f"record {index}: sequence {sequence} is not increasing")
        if sequence is not None:
            last_sequence = sequence
        epoch = record.get("epoch")
        if epoch is not None and last_epoch is not None and epoch < last_epoch:
            errors.append(f"record {index}: authority epoch went backwards")
        if epoch is not None:
            last_epoch = epoch
    return errors


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--synth", help="profile name or 'all'")
    ap.add_argument("--trace", help="sync-trace ndjson from a real session")
    ap.add_argument("--kind", default="player", choices=["player", "vehicle"])
    ap.add_argument("--rate", type=int, default=30)
    ap.add_argument("--validate", action="store_true",
                    help="trace mode: compare python baseline to the game's own 'out' records")
    ap.add_argument("--plot", help="write a PNG comparing paths (needs matplotlib)")
    ap.add_argument("--map", help="maps/*.calib.json - draw plots over your extracted city map")
    ap.add_argument("--path", help="a banked drive from paths/ to run through channel profiles")
    ap.add_argument("--profile", default="rimtek", help="channel profile for --path runs")
    ap.add_argument("--save-path", help="trace mode: bank the trace's drive into paths/<name>.json")
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
                    plot(args.plot, truth, records, kind, args.rate, pname, map_calib=args.map)
        return

    if args.path:
        import os
        pf = args.path if args.path.endswith(".json") else os.path.join(
            os.path.dirname(os.path.abspath(__file__)), "paths", args.path + ".json")
        truth = synth.load_path(pf)
        wire = synth.sample_wire(truth, args.rate)
        records = synth.receive(wire, PROFILES[args.profile])
        board = run(records, args.kind, args.rate, truth=truth)
        print_board(f"path {args.path} / {args.profile} / {args.kind}", board, truth=True)
        if args.plot:
            plot(args.plot, truth, records, args.kind, args.rate,
                 f"{args.path}/{args.profile}", map_calib=args.map)
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
        if args.save_path:
            import os
            out = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                               "paths", args.save_path + ".json")
            n = synth.save_path(records, out, args.save_path, kind)
            print(f"banked {n} samples -> {out}")
        board = run(records, kind, args.rate)
        print_board(f"trace {args.trace} / {ent}", board, truth=False)
        failed = False
        if args.validate:
            entity_outs = [o for o in outs if o["id"] == ent]
            failed = not entity_outs or not validate(records, entity_outs, kind, args.rate)
        metadata_errors = validate_trace_metadata(records)
        if metadata_errors:
            print("trace metadata: FAIL")
            for error in metadata_errors:
                print(f"- {error}")
        else:
            print("trace metadata: PASS")
        return 1 if failed or metadata_errors else 0

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
                mean = sum(errs) / len(errs)
                p95 = errs[int(0.95 * (len(errs) - 1))]
                print(f"validate: python-baseline vs game render - mean {mean:.3f}m, "
                            f"p95 {p95:.3f}m over {len(errs)} frames "
                            f"(>0.5m p95 means the port has drifted from the C++ - fix the port first)")
                return p95 <= 0.5
        print("validate: no comparable rendered frames")
        return False


def plot(path, truth, records, kind, rate, pname, map_calib=None):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    fig, ax = plt.subplots(figsize=(9, 7))
    if map_calib:
        import worldmap
        worldmap.draw_underlay(ax, map_calib)
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
