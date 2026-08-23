# netlab — the netcode algorithm lab

**Charter:** Python is the LAB, C++ is the SHIP. Nothing in here runs at play time, ships
to a player, or is imported by the product. This directory exists so interpolation /
prediction / delay algorithms for cars and puppets get argued about with measurements on
recorded traces instead of with two people standing in Night City going "did that look
better to you?". An algorithm graduates by winning here on real traces, then gets ported
to `code/client/App/World/InterpolationSystem.cpp` by hand.

## Setup (dev machine, once)

    cd tools/netlab
    python -m venv .venv
    .venv/Scripts/pip install -r requirements.txt   # numpy, matplotlib

## Run it today (no game needed)

    .venv/Scripts/python replay.py --synth rimtek --kind player
    .venv/Scripts/python replay.py --synth rimtek --kind vehicle --plot out.png
    .venv/Scripts/python replay.py --synth all

Synthetic profiles model the links we have actually measured: `lan` (Cam-class, ~20ms),
`rimtek` (~170ms RTT, 30ms jitter, 1% loss - the far-player case from 2026-08-22), and
`wifi_burst` (spiky home wifi). `--synth all` runs every profile x player/vehicle.

## Real traces (the point of all this)

Launch the game with the dev flag `-sync-trace` (add it to the launch arguments by
hand; it is deliberately not something the launcher offers players). The client then
writes `logs/sync-trace-<timestamp>.ndjson` next to its other logs inside the mod
folder, and it ships to the NAS with the rest of the session logs. Two record kinds:

    {"k":"in", "id":"e6", "tick":1787..., "tr":1787..., "p":[x,y,z], "r":[rx,ry,rz], "v":speed, "veh":0}
    {"k":"out","id":"e6", "rt":1787...,  "p":[x,y,z]}

`in` = a movement sample as received (tick = sender's synced clock, tr = local receive
time) - this is the network's side of the story. `out` = what the current C++ actually
rendered that frame - which lets `replay.py --trace file --validate` check that the
Python baseline reproduces the shipped behaviour on the same input before anyone trusts
a comparison. Feed a trace in with:

    .venv/Scripts/python replay.py --trace sync-trace-XXXX.ndjson

Real traces have no ground truth (the sender's true path lives on the sender's
machine), so trace runs score smoothness, pops, starvation and effective delay;
synthetic runs also score positional error against perfect truth.

## What is in here

- `netmodel.py` - the channel: one-way delay, jitter distribution, loss, reordering.
- `synth.py` - drives a fake player/vehicle along city-ish paths (straights, turns,
  stops, a highway stretch) and emits truth + received-samples for a channel profile.
- `strategies.py` - the contenders. `baseline` is a faithful port of today's
  InterpolationSystem (fixed 50+1500/rate delay, segment lerp with clamp, <=250ms
  heading extrapolation for players, NO extrapolation for vehicles - the port matches
  the freeze-history comments in the C++, integer ticks and all). The others are what
  we suspect will beat it: `adaptive` (delay follows measured jitter instead of a
  constant), `hermite` (velocity-tangent cubic instead of lerp), `vehicle_dr` (dead
  reckoning with projective blending so cars neither freeze on loss nor pop on
  recovery).
- `replay.py` - runs strategies over a trace, prints the scoreboard, optionally plots.

## Scoreboard columns

- `err_mean / err_p95` (m): distance from truth (synthetic only).
- `pops`: frames that jumped further than physics allows (teleport-pop count).
- `jerk`: mean second-difference of rendered position - the "does it look like a
  puppet on a string" number; lower is smoother.
- `starve%`: frames with nothing to interpolate towards.
- `delay_ms`: effective added latency (how far behind truth the render runs) - the
  price paid for the smoothness; every strategy is a trade along this axis.
