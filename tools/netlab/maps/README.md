# maps/ — your own copy of Night City

Everything in this folder except the READMEs and `*.calib.json` is **gitignored on
purpose**: the world-map image is CDPR's asset, so every dev extracts their own copy
from their own game and it never enters the public repo. The calibration files ARE
committed — coordinates we measured in our own sessions are our data, and one person
calibrating means nobody else has to.

## Getting the image (once, your machine)

Either of these works; both use only your own game install:

1. **WolvenKit** (best quality): open your game in WolvenKit, search the asset browser
   for `world_map`, add the fullscreen world-map texture to a project, then
   Tools → Export Tool → export to PNG. Save it here as `nc.png`.
2. **Screenshots**: open the in-game map, zoom to a consistent level, screenshot and
   stitch. Cruder, works in a pinch.

## Calibrating (once per image)

Pick two or more well-separated spots whose WORLD coordinates you know — our session
logs are full of them (respawn points, `/setspawn` output), or stand somewhere in game
and read the CET console — find each spot's PIXEL position in your image, then:

    ..\.venv\Scripts\python ..\calibrate.py --image nc.png --out nc.calib.json ^
        --pair <world_x> <world_y> <pixel_x> <pixel_y> ^
        --pair <world_x> <world_y> <pixel_x> <pixel_y>

Three or four pairs are better than two — the tool prints the worst residual so a
misread click is caught immediately. Commit the `.calib.json`; keep the png.

Known world anchors from our own logs (2026-08-22):
- old respawn point: `(-1756.6, -1939.3)` — badlands edge
- tonight's respawn point: `(673.6, -1402.7)` — city
- a `-sync-trace` drive past any landmark gives you as many anchors as you like

## Using it

    ..\.venv\Scripts\python ..\replay.py --synth rimtek --kind vehicle --plot out.png --map nc.calib.json

Traces and strategy paths render over the actual city instead of a blank grid, and a
`--bounds` clip in the calibration (set it to the city rectangle) keeps everything
"Night City only" when the badlands are noise.
