# Builds maps/<name>.calib.json from anchor pairs - run once per extracted map image.
#
#   python calibrate.py --image nc.png --out maps/nc.calib.json \
#       --pair -1756.6 -1939.3 1180 5240 \
#       --pair   673.6 -1402.7 3910 4620
#
# Each --pair is: world_x world_y pixel_x pixel_y. World coordinates come from our own
# session logs (respawn points, /setspawn coordinates, a -sync-trace drive past a known
# corner) or from the CET console in game; pixel coordinates are read off the image in
# any editor. Two well-separated anchors fit the transform; a third or fourth spreads
# the error and the tool reports the residual so a bad click is caught immediately.
import argparse

from worldmap import Calibration


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", required=True, help="map image filename (inside maps/)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--pair", nargs=4, type=float, action="append", required=True,
                    metavar=("WX", "WY", "PX", "PY"))
    ap.add_argument("--bounds", nargs=4, type=float,
                    metavar=("XMIN", "YMIN", "XMAX", "YMAX"),
                    help="optional world-space clip, e.g. the city without the badlands")
    args = ap.parse_args()

    pairs = [((p[0], p[1]), (p[2], p[3])) for p in args.pair]
    calib = Calibration.fit(pairs, args.image, list(args.bounds) if args.bounds else None)

    worst = 0.0
    for (w, p) in pairs:
        px, py = calib.to_pixel(*w)
        worst = max(worst, abs(px - p[0]), abs(py - p[1]))

    calib.save(args.out)
    print(f"wrote {args.out}  (scale {calib.sx:.4f}/{calib.sy:.4f} px per metre, "
          f"worst anchor residual {worst:.1f}px)")
    if len(pairs) >= 3 and worst > 25:
        print("residual is large - one of the anchors is probably misread; re-check the clicks")


if __name__ == "__main__":
    main()
