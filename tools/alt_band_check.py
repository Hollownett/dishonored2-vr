#!/usr/bin/env python3
"""v790 eye band bursts (written while knob build/alt-pose-trace is on): alt-band-index.csv and alt-band-<burst>-<seq>-left|right.rgba.

Per burst and Present (seq), it prints:
  disp       the left-right disparity: the horizontal shift that best aligns the right band onto the left one (px; a still view keeps it
             constant from Present to Present);
  dL, dR     the shift from the previous Present's image of the same eye, with its match (normalised correlation). On a still view both
             stay near 0 px with a match near 1. An eye that shows two viewpoints in turn jumps by about the disparity between consecutive
             images;
  gL, gR     the ghost score: the normalised autocorrelation of the eye's high-passed band at the disparity lag, above its neighbourhood.
             A copy of the other eye's view inside one eye raises it in that eye only.
It also writes alt-band-<burst>.png (left | right for each Present, half size) next to the index, or into --out.

  python tools/alt_band_check.py [artifacts dir] [--out DIR]
"""
import csv
import os
import sys
from collections import defaultdict
from pathlib import Path

import numpy as np
from PIL import Image

MAX_SHIFT = 200


def luma(rgba):
    return 0.299 * rgba[..., 0] + 0.587 * rgba[..., 1] + 0.114 * rgba[..., 2]


def highpass(y, k=9):
    """y minus its k x k box blur (edges and texture only)."""
    pad = k // 2
    p = np.pad(y, pad, mode="edge")
    c = p.cumsum(0).cumsum(1)
    c = np.pad(c, ((1, 0), (1, 0)))
    box = (c[k:, k:] - c[:-k, k:] - c[k:, :-k] + c[:-k, :-k]) / (k * k)
    return y - box


def xcorr(a, b):
    """Normalised correlation of rows of a against rows of b shifted by lag (b[x + lag] ~ a[x]), lags -MAX_SHIFT..MAX_SHIFT."""
    h, w = a.shape
    n = 2 * w
    fa, fb = np.fft.rfft(a, n, axis=1), np.fft.rfft(b, n, axis=1)
    raw = np.fft.irfft(np.conj(fa) * fb, n, axis=1).sum(0)          # raw[lag] = sum_x a[x] * b[x + lag] (circular over 2w)
    lags = np.arange(-MAX_SHIFT, MAX_SHIFT + 1)
    overlap = (w - np.abs(lags)).astype(float)
    norm = np.sqrt((a * a).sum() * (b * b).sum())
    return lags, raw[lags % n] * w / (overlap * max(norm, 1e-9))   # 1 for identical bands at lag 0


def best(a, b):
    lags, c = xcorr(a, b)
    i = int(np.argmax(c))
    return int(lags[i]), float(c[i])


def ghost(hp, lag):
    """Autocorrelation at |lag| above the median of its neighbourhood (5..25 px either side), relative to lag 0."""
    lags, c = xcorr(hp, hp)
    c = c / max(c[MAX_SHIFT], 1e-9)
    d = min(abs(lag), MAX_SHIFT - 30)
    if d < 12:
        return float("nan")   # the disparity is too small to separate a copy from the image's own blur
    at = c[MAX_SHIFT + d]
    near = np.concatenate([c[MAX_SHIFT + d - 25:MAX_SHIFT + d - 5], c[MAX_SHIFT + d + 5:MAX_SHIFT + d + 25]])
    return float(at - np.median(near))


def main(argv):
    folder = Path(argv[1]) if len(argv) > 1 and not argv[1].startswith("--") else Path(os.environ.get("LOCALAPPDATA", ".")) / "DishonoredVR" / "artifacts"
    out = Path(argv[argv.index("--out") + 1]) if "--out" in argv else folder
    index = folder / "alt-band-index.csv"
    if not index.exists():
        print("no %s" % index)
        return 2
    bursts = defaultdict(dict)
    with open(index, newline="") as handle:
        for r in csv.DictReader(handle):
            bursts[int(r["burst"])].setdefault(int(r["seq"]), {})[int(r["eye"])] = r
    for b in sorted(bursts):
        seqs = bursts[b]
        any_row = next(iter(next(iter(seqs.values())).values()))
        w, h = int(any_row["w"]), int(any_row["h"])
        print("burst %d: alternate %s, parity_flip %s, %d Presents, band %dx%d of %sx%s" % (
            b, any_row["alternate"], any_row["parity_flip"], len(seqs), w, h, any_row["full_w"], any_row["full_h"]))
        print("  seq present fresh   disp |   dL   mL    gL |   dR   mR    gR")
        prev = {}
        tiles = []
        stats = defaultdict(list)
        for s in sorted(seqs):
            rows = seqs[s]
            img = {}
            for e in (0, 1):
                f = folder / ("alt-band-%d-%d-%s.rgba" % (b, s, "right" if e else "left"))
                if e in rows and f.exists() and f.stat().st_size == w * h * 4:
                    img[e] = np.frombuffer(f.read_bytes(), dtype=np.uint8).reshape(h, w, 4)
            if len(img) != 2:
                continue
            hp = {e: highpass(luma(img[e].astype(np.float32))) for e in (0, 1)}
            disp, _ = best(hp[0], hp[1])
            line = "  %3d %7s %5s %6d |" % (s, rows[0]["present"], rows[0]["fresh"], disp)
            for e in (0, 1):
                if e in prev:
                    dx, m = best(prev[e], hp[e])
                    stats["d%d" % e].append(abs(dx))
                    stats["m%d" % e].append(m)
                    line += " %4d %4.2f" % (dx, m)
                else:
                    line += "    -    -"
                g = ghost(hp[e], disp)
                stats["g%d" % e].append(g)
                line += " %5.3f |" % g
            print(line.rstrip("|"))
            prev = hp
            tiles.append(np.concatenate([img[0][::2, ::2, :3], np.full((h // 2, 8, 3), 255, np.uint8), img[1][::2, ::2, :3]], axis=1))
        med = lambda k: float(np.nanmedian(stats[k])) if stats[k] else float("nan")  # noqa: E731
        print("  median: |dL| %.1f px match %.2f ghost %.3f   |dR| %.1f px match %.2f ghost %.3f" % (
            med("d0"), med("m0"), med("g0"), med("d1"), med("m1"), med("g1")))
        if tiles:
            sheet = np.concatenate([np.concatenate([t, np.full((4, t.shape[1], 3), 255, np.uint8)]) for t in tiles])
            png = out / ("alt-band-%d.png" % b)
            Image.fromarray(sheet).save(png)
            print("  montage %s" % png)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
