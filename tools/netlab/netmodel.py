# The channel between one player's game and another's: one-way delay, jitter, loss,
# reordering. Profiles are named after links we have actually measured on this server,
# so a result here predicts a person, not an abstraction.
import random
from dataclasses import dataclass


@dataclass
class Channel:
    name: str
    base_ms: float          # one-way delay
    jitter_ms: float        # stddev of per-packet jitter (gaussian, floored)
    loss: float             # independent drop probability
    burst_ms: float = 0.0   # occasional extra spike amplitude
    burst_p: float = 0.0    # probability a packet rides a spike

    def deliver(self, send_times, rng=None):
        """send_times -> list of (recv_time, index) for packets that survive,
        sorted by arrival (so reordering falls out naturally)."""
        rng = rng or random.Random(1234)  # deterministic: comparisons must be fair
        arrivals = []
        for i, t in enumerate(send_times):
            if rng.random() < self.loss:
                continue
            delay = self.base_ms + max(0.0, rng.gauss(0, self.jitter_ms))
            if self.burst_p and rng.random() < self.burst_p:
                delay += rng.uniform(0.5, 1.0) * self.burst_ms
            arrivals.append((t + delay, i))
        arrivals.sort()
        return arrivals


PROFILES = {
    # Cam-class: same city, direct wireguard. Measured ~20ms RTT.
    "lan": Channel("lan", base_ms=12, jitter_ms=4, loss=0.0),
    # The far player, live on 2026-08-22: ~170ms RTT, 0-1% loss, visible chop.
    "rimtek": Channel("rimtek", base_ms=85, jitter_ms=30, loss=0.01),
    # Spiky home wifi: fine median, ugly tail.
    "wifi_burst": Channel("wifi_burst", base_ms=30, jitter_ms=10, loss=0.02,
                          burst_ms=150, burst_p=0.06),
}
