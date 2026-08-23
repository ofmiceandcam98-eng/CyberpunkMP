# paths/ — the drive corpus: our real copy of Night City's roads

This is the map that actually matters to the algorithms. A path file is one real drive
(or run) recorded in a live session with `-sync-trace`, reduced to its kinematics:
positions, yaw, speed, at real timing — true Night City corners, true stops at lights,
true highway sweeps. These are OUR measurements, so unlike the map image they are
committed and shared.

Bank a drive from a captured trace:

    ..\.venv\Scripts\python ..\replay.py --trace sync-trace-XXXX.ndjson --save-path corpo_plaza_loop

Re-simulate that exact road under any link, forever:

    ..\.venv\Scripts\python ..\replay.py --path corpo_plaza_loop --kind vehicle
    ..\.venv\Scripts\python ..\replay.py --path corpo_plaza_loop --kind vehicle --profile rimtek --plot out.png --map ..\maps\nc.calib.json

One capture session builds the whole corpus: drive the routes vehicle sync must
survive — tight downtown grid, a highway stretch, a parking maneuver, stop-and-go
traffic — each becomes a named path here. "Night City only" is enforced the honest
way: the corpus contains exactly the roads someone actually drove.

Naming: `<area>_<what>.json` — e.g. `japantown_grid.json`, `nc_highway_north.json`.
Keep files under ~1MB (a few minutes of driving each); long sessions split better by
route anyway.
