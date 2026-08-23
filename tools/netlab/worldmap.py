# Draws lab output over the actual Night City map - YOUR OWN copy of it.
#
# The map image is CDPR's asset, so it is never committed here: maps/ is gitignored and
# each dev extracts their own (see maps/README.md). What IS committed is the
# calibration - two or more (world x,y) <-> (pixel u,v) anchor pairs and the affine fit
# between them - because coordinates measured in our sessions are our data.
#
# Night City world space runs roughly +-4000 on X and Y; the world map texture is a
# top-down render, so an affine transform (scale + flip + offset, no rotation worth
# modelling) maps one onto the other to within a road's width once two well-separated
# anchors are given.
import json
import os


class Calibration:
    def __init__(self, sx, sy, ox, oy, image, bounds=None):
        self.sx, self.sy, self.ox, self.oy = sx, sy, ox, oy
        self.image = image
        self.bounds = bounds  # optional [xmin, ymin, xmax, ymax] world clip ("city only")

    def to_pixel(self, x, y):
        return x * self.sx + self.ox, y * self.sy + self.oy

    @staticmethod
    def fit(pairs, image, bounds=None):
        """pairs: [((wx, wy), (px, py)), ...] - least squares per axis, 2+ pairs."""
        n = len(pairs)
        if n < 2:
            raise ValueError("need at least two anchor pairs")

        def axis(wi, pi):
            w = [p[0][wi] for p in pairs]
            px = [p[1][pi] for p in pairs]
            wm, pm = sum(w) / n, sum(px) / n
            denom = sum((a - wm) ** 2 for a in w)
            s = sum((a - wm) * (b - pm) for a, b in zip(w, px)) / denom
            return s, pm - s * wm

        sx, ox = axis(0, 0)
        sy, oy = axis(1, 1)
        return Calibration(sx, sy, ox, oy, image, bounds)

    def save(self, path):
        with open(path, "w", encoding="utf-8") as f:
            json.dump({"sx": self.sx, "sy": self.sy, "ox": self.ox, "oy": self.oy,
                       "image": self.image, "bounds": self.bounds}, f, indent=1)

    @staticmethod
    def load(path):
        with open(path, encoding="utf-8") as f:
            d = json.load(f)
        return Calibration(d["sx"], d["sy"], d["ox"], d["oy"], d["image"], d.get("bounds"))


def draw_underlay(ax, calib_path):
    """Puts the map under a matplotlib axes whose data space is WORLD coordinates."""
    import matplotlib.image as mpimg
    calib = Calibration.load(calib_path)
    img_path = os.path.join(os.path.dirname(os.path.abspath(calib_path)), calib.image)
    img = mpimg.imread(img_path)
    h, w = img.shape[0], img.shape[1]
    # Invert the affine to find the world rectangle the image spans.
    x0, x1 = (0 - calib.ox) / calib.sx, (w - calib.ox) / calib.sx
    y0, y1 = (0 - calib.oy) / calib.sy, (h - calib.oy) / calib.sy
    ax.imshow(img, extent=(min(x0, x1), max(x0, x1), min(y0, y1), max(y0, y1)),
              origin="upper" if calib.sy < 0 else "lower", alpha=0.7, zorder=0)
    return calib
