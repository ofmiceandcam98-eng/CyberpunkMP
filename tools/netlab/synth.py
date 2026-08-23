# Drives a fake player or vehicle along a city-ish path and emits what the RECEIVER
# would see through a channel: samples at the server update rate, delayed, jittered,
# dropped. Truth is kept at render granularity so error can be scored exactly.
#
# The path is deliberately hostile to interpolation: straights (easy), 90-degree
# corners (velocity direction flips), a stop-and-go (velocity magnitude collapses),
# and a highway stretch (small angular error = metres of position error).
import math
from dataclasses import dataclass


@dataclass
class Truth:
    t: float          # ms
    pos: tuple
    yaw: float
    speed: float      # m/s, what the wire carries


def make_truth(kind="player", duration_ms=60_000, dt_ms=16.0):
    """Waypoint-follower with per-kind speeds. Returns list[Truth] at frame rate."""
    speed_cruise = 5.0 if kind == "player" else 22.0     # sprint vs city driving
    speed_fast = 7.0 if kind == "player" else 33.0       # highway stretch
    # A loop: straight, corner, straight, stop, corner, long fast straight.
    legs = [
        (400, speed_cruise), ("turn_left", None), (250, speed_cruise),
        ("stop", 1500), ("turn_right", None), (900, speed_fast),
        ("turn_left", None), (400, speed_cruise),
    ]
    x, y, yaw = 0.0, 0.0, 0.0
    t = 0.0
    speed = 0.0
    out = []
    leg_i, leg_prog = 0, 0.0
    stop_until = -1.0
    accel = 8.0 if kind == "player" else 6.0             # m/s^2 toward target speed

    target = speed_cruise
    turn_left_remaining = 0.0
    turn_dir = 0.0
    TURN_RATE = math.radians(120 if kind == "player" else 55)  # rad/s

    while t < duration_ms:
        leg = legs[leg_i % len(legs)]
        if isinstance(leg[0], (int, float)):
            target = leg[1]
            if leg_prog >= leg[0]:
                leg_i += 1
                leg_prog = 0.0
        elif leg[0] == "stop":
            if stop_until < 0:
                stop_until = t + leg[1]
            target = 0.0
            if t >= stop_until:
                leg_i += 1
                stop_until = -1.0
        else:  # turns
            if turn_left_remaining <= 0:
                turn_left_remaining = math.pi / 2
                turn_dir = 1.0 if leg[0] == "turn_left" else -1.0
            step = TURN_RATE * (dt_ms / 1000.0)
            step = min(step, turn_left_remaining)
            yaw += turn_dir * step
            turn_left_remaining -= step
            if turn_left_remaining <= 1e-6:
                leg_i += 1

        # approach target speed
        ds = accel * (dt_ms / 1000.0)
        if speed < target:
            speed = min(target, speed + ds)
        else:
            speed = max(target, speed - ds)

        dist = speed * (dt_ms / 1000.0)
        x += -math.sin(yaw) * dist   # matches the client's heading convention
        y += math.cos(yaw) * dist
        if isinstance(leg[0], (int, float)):
            leg_prog += dist

        out.append(Truth(t, (x, y, 0.0), yaw, speed))
        t += dt_ms
    return out


def sample_wire(truth, update_rate=30, epoch=1_787_000_000_000):
    """What the sender's client puts on the wire: one sample per server update."""
    period = 1000.0 / update_rate
    samples, next_t = [], 0.0
    for tr in truth:
        if tr.t >= next_t:
            samples.append({
                "tick": epoch + tr.t,       # sender's synced-clock tick, like the wire
                "p": list(tr.pos),
                "r": [0.0, 0.0, tr.yaw],
                "v": tr.speed,
            })
            next_t += period
    return samples


def receive(samples, channel, epoch=1_787_000_000_000):
    """Push wire samples through the channel -> receiver-side 'in' records."""
    send_times = [s["tick"] - epoch for s in samples]
    arrivals = channel.deliver(send_times)
    records = []
    for recv_t, i in arrivals:
        s = samples[i]
        records.append({"k": "in", "id": "e6", "tick": s["tick"],
                        "tr": epoch + recv_t, "p": s["p"], "r": s["r"], "v": s["v"]})
    return records


def load_path(path_file):
    """A banked real drive (paths/*.json) as truth - real roads, real kinematics."""
    import json
    with open(path_file, encoding="utf-8") as f:
        d = json.load(f)
    return [Truth(p["t"], tuple(p["p"]), p["yaw"], p["v"]) for p in d["samples"]]


def save_path(records, out_file, name, kind):
    """Reduce a trace's 'in' records to a reusable path. Positions on the wire ARE the
    sender's path at update-rate resolution, and the tick spacing keeps the timing."""
    import json
    t0 = records[0]["tick"]
    samples = [{"t": r["tick"] - t0, "p": [round(c, 3) for c in r["p"]],
                "yaw": round(r["r"][2], 4), "v": round(r["v"], 3)} for r in records]
    with open(out_file, "w", encoding="utf-8") as f:
        json.dump({"name": name, "kind": kind, "count": len(samples),
                   "duration_ms": samples[-1]["t"] if samples else 0,
                   "samples": samples}, f)
    return len(samples)
