#!/usr/bin/env python3
"""Summarise a G2 spike run (camera plan Task 4): artifacts/cinematic-probe.csv from the build/cinematic-probe knob.

Usage: cinematic_probe_report.py RUN_DIR   (a ~/d2vr-test-builds/sim-runs/<stamp> snapshot, or its artifacts/ folder)

Prints the logic-mask timeline, then for every exact-32 (Cinematic) run: Setups and Begins per frame, native_views().size(), whether
the Setup views match the native views and are distinct (stereo), whether each frame's Begins consume the previous frame's Setups
(N -> N+1), the native player camera identity and its own view against the view the game renders from, and the paired-binds
baseline. With the "offset" experiment it diffs each capture burst: frames whose Setup carried the +5 cm test write against the
frames around them (the mean absolute per-pixel change between consecutive Presents, 8-bit luminance, 4x downsampled).
Exit 0 always (a report, not a gate); the go/no-go reading is written up in g2-spike.md.
"""
import csv
import math
import statistics
import sys
from collections import Counter
from pathlib import Path


def load(path):
    rows, logic = [], []
    with open(path, newline="") as f:
        reader = csv.reader(f)
        header = next(reader)
        for rec in reader:
            if not rec:
                continue
            if rec[0] == "logic":
                logic.append({"frame": int(rec[1]), "t_ms": int(rec[2]), "logic": int(rec[3]), "from": rec[4].split("=", 1)[-1]})
            elif rec[0] == "frame" and len(rec) >= len(header) - 1:
                row = dict(zip(header, rec + [""] * (len(header) - len(rec))))
                for k, v in row.items():
                    if k in ("kind", "capture", "s0_view", "s1_view", "player", "camera"):
                        continue
                    try:
                        row[k] = float(v)
                    except ValueError:
                        row[k] = math.nan
                rows.append(row)
    return header, rows, logic


def runs32(rows, logic):
    """Rows of each exact-32 run, bounded by the logic-change rows (frame rows are subsampled, so frames are not contiguous)."""
    out = []
    for i, e in enumerate(logic):
        if e["logic"] != 32:
            continue
        end = logic[i + 1]["frame"] if i + 1 < len(logic) else math.inf
        run = [r for r in rows if r["logic"] == 32 and e["frame"] <= r["frame"] < end]
        if run:
            out.append(run)
    return out


def hist(values):
    return dict(sorted(Counter(values).items()))


def luminance(path, width, height):
    import numpy as np
    raw = np.fromfile(path, dtype=np.uint8)
    if raw.size != width * height * 4:
        return None
    img = raw.reshape(height, width, 4)[:, :, :3].astype(np.float32)
    y = img[:, :, 0] * .299 + img[:, :, 1] * .587 + img[:, :, 2] * .114
    h, w = (height // 4) * 4, (width // 4) * 4
    return y[:h, :w].reshape(h // 4, 4, w // 4, 4).mean(axis=(1, 3))


def bursts(rows, art):
    """Consecutive captured Presents; each with whether the Setup two frames earlier (display = Setup frame + 2) or one earlier wrote."""
    import numpy as np
    shots = [r for r in rows if r.get("capture")]
    groups, cur = [], []
    for r in shots:
        if cur and r["frame"] != cur[-1]["frame"] + 1:
            groups.append(cur)
            cur = []
        cur.append(r)
    if cur:
        groups.append(cur)
    written = {int(r["frame"]) - 1 for r in rows if r["writes"] and r["writes"] > 0}  # a row's Setups ran with frames()==frame-1
    for g in groups:
        imgs = [luminance(art / r["capture"], int(r["width"]), int(r["height"])) for r in g]
        print(f"  burst Presents {int(g[0]['frame'])}..{int(g[-1]['frame'])}: test-write Setup frames "
              f"{sorted(f for f in written if g[0]['frame'] - 4 <= f <= g[-1]['frame'])}")
        for i, r in enumerate(g):
            n = int(r["frame"])
            d = float(np.abs(imgs[i] - imgs[i - 1]).mean()) if i and imgs[i] is not None and imgs[i - 1] is not None else math.nan
            print(f"    present {n}: shows Setup {n - 2} written={int((n - 2) in written)} (lag 1: {int((n - 1) in written)})  "
                  f"mean |change| vs previous = {d:.3f}")


def main(argv):
    if len(argv) != 1:
        print(__doc__.strip())
        return 2
    base = Path(argv[0]).expanduser()
    art = base / "artifacts" if (base / "artifacts").is_dir() else base
    path = art / "cinematic-probe.csv"
    if not path.is_file():
        print(f"no {path} (knob build/cinematic-probe absent?)")
        return 0
    header, rows, logic = load(path)
    with open(path) as f:
        f.readline()
        mode = f.readline().strip()
    print(f"{path}: {len(rows)} frame rows, {len(logic)} logic changes; {mode}")
    t0 = logic[0]["t_ms"] if logic else 0
    for e in logic:
        print(f"  logic {e['from']:>5} -> {e['logic']:<5} at Present {e['frame']} (+{(e['t_ms'] - t0) / 1000:.1f} s)")
    gameplay = [r for r in rows if r["logic"] == 2]
    if gameplay:
        print(f"logic 2 sample ({len(gameplay)} rows): setups {hist(int(r['setups']) for r in gameplay)}, matched "
              f"{hist(int(r['matched']) for r in gameplay if not math.isnan(r.get('matched', math.nan)))}, begins {hist(int(r['begins']) for r in gameplay)}, "
              f"N->N+1 {sum(1 for r in gameplay if r['begin_n_plus_1'] == 1)}/{len(gameplay)}, native_views "
              f"{hist(int(r['native_views']) for r in gameplay)}, render-minus-camera median "
              f"{statistics.median([r['render_minus_cam_m'] for r in gameplay if not math.isnan(r['render_minus_cam_m'])] or [math.nan]):.4f} m")
    for k, run in enumerate(runs32(rows, logic)):
        dur = (run[-1]["t_ms"] - run[0]["t_ms"]) / 1000
        ok = lambda key: sum(1 for r in run if r[key] == 1)  # noqa: E731
        sep = [r["s01_sep"] for r in run if not math.isnan(r["s01_sep"])]
        rmc = [r["render_minus_cam_m"] for r in run if not math.isnan(r["render_minus_cam_m"])]
        ids = Counter((r["generation"], r["cam_mode"], r["cam_target_mode"]) for r in run)
        print(f"Cinematic32 run {k + 1}: {len(run)} logged frames (all of a run's first 60, then every 4th, bursts and cuts), {dur:.1f} s, "
              f"Presents {int(run[0]['frame'])}..{int(run[-1]['frame'])}")
        print(f"  stereo_active {hist(int(r['stereo']) for r in run)}; native_views {hist(int(r['native_views']) for r in run)}")
        print(f"  Setups/frame {hist(int(r['setups']) for r in run)}; Begins/frame {hist(int(r['begins']) for r in run)}; "
              f"Begins consume the previous frame's Setups (N->N+1): {ok('begin_n_plus_1')}/{len(run)}")
        print(f"  Setups matching a native view/frame {hist(int(r['matched']) for r in run if not math.isnan(r.get('matched', math.nan)))}; "
              f"shown call 0 eye {hist(int(r['s0_eye']) for r in run)}, call 1 eye {hist(int(r['s1_eye']) for r in run)}")
        if sep:
            print(f"  Setup pair separation: min {min(sep):.4f} median {statistics.median(sep):.4f} max {max(sep):.4f} m "
                  f"({sum(1 for s in sep if s > .001)}/{len(sep)} distinct)")
        print(f"  Setup 0 FOV h {hist(round(r['s0_hfov'], 3) for r in run if not math.isnan(r['s0_hfov']))}")
        print(f"  player camera (generation, mode, target_mode) {dict(ids)}; distinct player/camera pointers "
              f"{len({(r.get('player'), r.get('camera')) for r in run})}")
        if rmc:
            print(f"  |render view - player camera view|: min {min(rmc):.3f} median {statistics.median(rmc):.3f} max {max(rmc):.3f} m")
        print(f"  paired binds/frame {hist(int(r['binds']) for r in run)} (per eye {hist(int(r['binds_per_eye']) for r in run)}); "
              f"baseline holds {ok('baseline_holds')}/{len(run)}")
        print(f"  test writes {int(sum(r['writes'] for r in run if not math.isnan(r['writes'])))}")
    status = art / "xr-live-status.json"
    if status.is_file():
        import json
        try:
            print(f"status cinematic_probe (all mask-32 frames; setups_per_frame = 0..4, 5+; cuts = Setup moves > 0.5 m in a frame): "
                  f"{json.loads(status.read_text()).get('cinematic_probe')}")
        except ValueError:
            pass
    if any(r.get("capture") for r in rows):
        print("offset experiment captures:")
        bursts(rows, art)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
