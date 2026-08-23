# The contenders. Every strategy answers one question per rendered frame: given the
# samples that have ARRIVED so far (never the future), where do you put the puppet NOW?
#
# `baseline` is a deliberate, faithful port of code/client/App/World/
# InterpolationSystem.cpp as of 2026-08-22 - including the integer-tick discipline (the
# float(epoch-tick) precision loss froze remote players for an evening; see the long
# comment in the C++), the drain-to-anchor segment walk, the clamp, the 250ms heading
# extrapolation for players, and the vehicle rule of NO extrapolation. If the baseline
# here does not match what the game does, every comparison is fiction - which is what
# `replay.py --validate` exists to check against real 'out' records.
import math
from collections import deque


def _dist(a, b):
    return math.sqrt((a[0]-b[0])**2 + (a[1]-b[1])**2 + (a[2]-b[2])**2)


def _lerp(a, b, r):
    return [a[i] + (b[i]-a[i]) * r for i in range(3)]


class Baseline:
    """Today's shipped algorithm. delay = 50 + 1500/update_rate (100ms at rate 30)."""
    name = "baseline"

    def __init__(self, kind, update_rate=30):
        self.kind = kind
        self.delay = 50.0 + 1500.0 / update_rate
        self.buf = deque()
        self.prev = None          # the anchor (PreviousFrame)

    def ingest(self, s):
        # out-of-order drop, exactly like HandleEntityMove
        newest = self.buf[-1]["tick"] if self.buf else (self.prev["tick"] if self.prev else None)
        if newest is not None and newest > s["tick"]:
            return
        self.buf.append(s)

    def render(self, now):
        render_tick = int(now) - int(self.delay)
        while self.buf and int(self.buf[0]["tick"]) <= render_tick:
            self.prev = self.buf.popleft()
        if self.prev is None:
            return None
        first = self.prev
        if not self.buf:
            ahead = float(render_tick - int(first["tick"]))
            if ahead <= 0.0 or ahead > 250.0:
                return None            # freeze in place (last applied pose holds)
            if self.kind == "vehicle":
                return None            # vehicles never extrapolate today
            yaw = first["r"][2]
            heading = (-math.sin(yaw), math.cos(yaw), 0.0)
            return [first["p"][i] + heading[i] * first["v"] * ahead * 0.001 for i in range(3)]
        second = self.buf[0]
        td = float(int(second["tick"]) - int(first["tick"]))
        ratio = 0.0
        if td > 0.0:
            ratio = max(0.0, min(1.0, (render_tick - int(first["tick"])) / td))
        return _lerp(first["p"], second["p"], ratio)


class AdaptiveDelay(Baseline):
    """Same interpolation, but the delay FOLLOWS the link instead of assuming it.

    Tracks per-sample lateness (arrival time vs sender tick, minus the running
    baseline offset) and holds the delay at a high percentile of recent jitter plus
    one update period - so a lan player pays ~50ms instead of 100, and a rimtek-class
    player gets enough budget that segments do not starve every corner. The delay
    SLEWS (max 5ms per rendered frame) because stepping it is a visible time-warp."""
    name = "adaptive"

    def __init__(self, kind, update_rate=30):
        super().__init__(kind, update_rate)
        self.period = 1000.0 / update_rate
        self.lateness = deque(maxlen=120)   # ~4s of samples
        self.offset = None                  # min observed (tr - tick): clock + base path
        self.current = self.delay           # start where the ship starts

    def ingest(self, s):
        super().ingest(s)
        raw = s["tr"] - s["tick"]
        self.offset = raw if self.offset is None else min(self.offset, raw)
        self.lateness.append(raw - (self.offset or 0.0))

    def _target_delay(self):
        if len(self.lateness) < 10:
            return self.delay
        j = sorted(self.lateness)[int(0.95 * (len(self.lateness) - 1))]
        # one period so a segment always exists, p95 jitter so late packets still land
        # in time, floored at one period + 10 so lan players do not run at zero buffer.
        return max(self.period + 10.0, j + self.period)

    def render(self, now):
        target = self._target_delay()
        step = 5.0
        self.current += max(-step, min(step, target - self.current))
        self.delay = self.current
        return super().render(now)


class Hermite(Baseline):
    """Cubic segment using the wire's own speed as tangents instead of a straight lerp.

    The wire already carries speed and yaw per sample; a lerp throws that away and
    corners become chords. Hermite keeps arrival times identical to baseline (same
    delay, same anchor walk) and only changes the curve between the two samples."""
    name = "hermite"

    def _vel(self, s):
        yaw = s["r"][2]
        return (-math.sin(yaw) * s["v"], math.cos(yaw) * s["v"], 0.0)

    def render(self, now):
        render_tick = int(now) - int(self.delay)
        while self.buf and int(self.buf[0]["tick"]) <= render_tick:
            self.prev = self.buf.popleft()
        if self.prev is None:
            return None
        first = self.prev
        if not self.buf:
            return super().render(now)     # same starvation behaviour as baseline
        second = self.buf[0]
        td = float(int(second["tick"]) - int(first["tick"]))
        if td <= 0.0:
            return list(first["p"])
        r = max(0.0, min(1.0, (render_tick - int(first["tick"])) / td))
        t = td * 0.001
        p0, p1 = first["p"], second["p"]
        v0 = [c * t for c in self._vel(first)]
        v1 = [c * t for c in self._vel(second)]
        h00 = 2*r**3 - 3*r**2 + 1
        h10 = r**3 - 2*r**2 + r
        h01 = -2*r**3 + 3*r**2
        h11 = r**3 - r**2
        return [h00*p0[i] + h10*v0[i] + h01*p1[i] + h11*v1[i] for i in range(3)]


class VehicleDR(Baseline):
    """Dead reckoning for cars: extrapolate through starvation, blend on recovery.

    Today a starved vehicle freezes (no extrapolation at all), then ForceMoveTo covers
    a whole gap in one segment - the stutter-then-lurch far passengers describe. Here
    a starved car keeps rolling along its last heading/speed (up to 500ms - a car's
    heading is far more inertial than a player's), and when a fresh sample arrives the
    rendered position BLENDS toward the corrected path over 150ms instead of snapping
    (projective blending). Players fall through to baseline behaviour untouched."""
    name = "vehicle_dr"
    MAX_EXTRAP_MS = 500.0
    BLEND_MS = 150.0

    def __init__(self, kind, update_rate=30):
        super().__init__(kind, update_rate)
        self.last_out = None
        self.blend_from = None
        self.blend_start = None
        self.was_guessing = False

    def render(self, now):
        if self.kind != "vehicle":
            return super().render(now)
        render_tick = int(now) - int(self.delay)
        while self.buf and int(self.buf[0]["tick"]) <= render_tick:
            self.prev = self.buf.popleft()
        if self.prev is None:
            return None
        first = self.prev
        target = None
        if not self.buf:
            ahead = float(render_tick - int(first["tick"]))
            if 0.0 < ahead <= self.MAX_EXTRAP_MS:
                yaw = first["r"][2]
                heading = (-math.sin(yaw), math.cos(yaw), 0.0)
                target = [first["p"][i] + heading[i] * first["v"] * ahead * 0.001 for i in range(3)]
            else:
                target = list(first["p"])
            self.blend_from = None      # while guessing there is nothing to blend to
            self.was_guessing = True
        else:
            second = self.buf[0]
            td = float(int(second["tick"]) - int(first["tick"]))
            r = max(0.0, min(1.0, (render_tick - int(first["tick"])) / td)) if td > 0 else 0.0
            target = _lerp(first["p"], second["p"], r)
            # Blend ONLY when recovering from a guess. The first cut blended on every
            # segment advance (drain happens each segment), which turned the blend into
            # a permanent 150ms drag on clean links - the lab's own first catch.
            if self.was_guessing and self.last_out is not None and _dist(self.last_out, target) > 0.05:
                self.blend_from = list(self.last_out)
                self.blend_start = now
            self.was_guessing = False
        if self.blend_from is not None and self.blend_start is not None:
            b = (now - self.blend_start) / self.BLEND_MS
            if b >= 1.0:
                self.blend_from = None
            else:
                target = _lerp(self.blend_from, target, b)
        self.last_out = list(target)
        return target


ALL = [Baseline, AdaptiveDelay, Hermite, VehicleDR]
