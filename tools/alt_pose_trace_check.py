#!/usr/bin/env python3
"""Join the rows of alt-pose-trace.csv (probe knob build/alt-pose-trace; alternate-eye, paired or perf-mono mode) and check that every
submitted eye layer carries exactly the pose and FOV its image was rendered with.

Rows (see dxgi_probe.cpp, alt_pose_trace_*):
  setup,presented,display,eye,px..qw,fl..fd,gx,gy,gz,bx,by,bz,,      the view a Setup wrote (XR pose + FOV recorded for the layer, game-space
                                                                      position and forward axis written into the engine view) and the display
                                                                      frame that shows its image (presented + 2: the engine pipeline is one frame deep)
  begin,presented,display,eye,,,...,gx,gy,gz,bx,by,bz,,               the pose in the view's render copy when the engine begins rendering it; its
                                                                      image is on screen at `display` (presented + 1)
  present,display,display,eye,px..qw,fl..fd,,...,tracked,fresh        the layer submitted at that Present for that eye (fresh: the one eye rendered
                                                                      for this Present in alternate mode, -1 = both eyes, a paired frame)

One-eye Presents (alternate mode, fresh >= 0), for each tracked Present P:
  1. the Begin shown at P rendered the game pose the Setup FOR P wrote (image <-> Setup link; proves the pipeline depth);
  2. that Setup's eye is f (the fresh eye really is the eye of the image in its swapchain; when the Begin rendered a different
     Setup, the eye of the Setup it did render is compared with f);
  3. the layer of eye f carries that Setup's XR pose and FOV;
  4. the layer of the other eye carries the XR pose and FOV of the latest earlier Setup of that eye (its kept image).
Paired Presents (fresh < 0: the paired path, perf-mono, or an alternate-mode paired frame), for each tracked Present P:
  1. each Begin shown at P rendered the game pose of the Setup FOR P of its eye (otherwise: which Setup it did render, by display offset);
  2. each layer carries the XR pose and FOV of the Setup FOR P of its eye. A layer carrying the Setup for P+1 instead (the Setup made in the
     interval just before the Present, i.e. the latest one) is the one-frame pose-versus-image skew: layer_pose_is_the_next_setup.
  A Present is "distinguishable" when the Setups for P and P+1 differ in XR pose (a static head cannot show the skew). The pose error of
  each layer against its image's Setup (orientation in degrees, position in mm) is summarised: that is what the compositor's reprojection
  works from, so it should be 0.
Exit 0 when every checked Present passes, 1 otherwise; prints the counts either way.
"""
import bisect
import csv
import math
import sys
from collections import defaultdict

POSE_TOL = 1e-6       # XR units; the values are copied, not computed
GAME_TOL = 1e-3       # game units; the render copy is the engine's own copy of what the Setup wrote


def parse(path):
    setups, begins, presents = {}, {}, defaultdict(dict)
    with open(path, newline="") as handle:
        for row in csv.DictReader(handle):
            event = row["event"]
            if event == "setup":
                display = int(row["display"])
                setups.setdefault(display, {})[int(row["eye"])] = row
            elif event == "begin":
                begins.setdefault(int(row["display"]), []).append(row)
            elif event == "present":
                presents[int(row["display"])][int(row["eye"])] = row
    return setups, begins, presents


def floats(row, keys):
    return [float(row[k]) for k in keys]


def close(a, b, tol):
    return all(abs(x - y) <= tol for x, y in zip(a, b))


XR_KEYS = ["px", "py", "pz", "qx", "qy", "qz", "qw", "fl", "fr", "fu", "fd"]
GAME_KEYS = ["gx", "gy", "gz", "bx", "by", "bz"]


def game_key(row):
    return tuple(round(v, 2) for v in floats(row, GAME_KEYS))


def pose_error(layer, setup):
    """(orientation error in degrees, position error in mm) between a layer and the Setup its image came from."""
    a, b = floats(layer, XR_KEYS), floats(setup, XR_KEYS)
    norm = lambda q: math.sqrt(sum(x * x for x in q)) or 1.0  # noqa: E731  (float32 quaternions: not exactly unit length)
    dot = min(1.0, abs(sum(x * y for x, y in zip(a[3:7], b[3:7]))) / (norm(a[3:7]) * norm(b[3:7])))
    return 2 * math.degrees(math.acos(dot)), 1000 * math.sqrt(sum((x - y) ** 2 for x, y in zip(a[:3], b[:3])))


def quantiles(values):
    if not values:
        return "n/a"
    v = sorted(values)
    pick = lambda q: v[min(len(v) - 1, int(q * len(v)))]  # noqa: E731
    return "p50 %.4f  p95 %.4f  max %.4f" % (pick(.5), pick(.95), v[-1])


def check(path, verbose=False):
    setups, begins, presents = parse(path)
    by_game = defaultdict(dict)   # rounded game pose -> {display: eyes} of the Setups that wrote it (for naming the Setup a Begin did render)
    for d in setups:
        for e in setups[d]:
            by_game[game_key(setups[d][e])].setdefault(d, set()).add(e)

    def rendered_setups(begin_row, display):
        """The (display, eye) Setups within 4 frames of `display` whose written game pose the Begin's render copy carries."""
        near = by_game.get(game_key(begin_row), {})
        return [(d, e) for d in range(display - 4, display + 5) for e in sorted(near.get(d, ()))
                if close(floats(begin_row, GAME_KEYS), floats(setups[d][e], GAME_KEYS), GAME_TOL)]

    displays_of_eye = {e: sorted(d for d in setups if e in setups[d]) for e in (0, 1)}   # for the stale eye's latest earlier Setup

    def latest_earlier(eye, display):
        i = bisect.bisect_left(displays_of_eye[eye], display)
        return displays_of_eye[eye][i - 1] if i else None

    checked = paired_checked = distinguishable = 0
    failures = defaultdict(int)
    first = {}
    deg_errors, mm_errors = [], []
    for display in sorted(presents):
        layers = presents[display]
        if len(layers) != 2 or layers[0]["tracked"] != "1" or layers[1]["tracked"] != "1":
            continue
        fresh = int(layers[0]["fresh"])
        problems = []
        if fresh < 0:
            # ---- a paired frame: both layers are the Setup recorded for this Present
            paired_checked += 1
            setup = setups.get(display, {})
            nxt = setups.get(display + 1, {})
            if len(setup) != 2:
                problems.append("no_paired_setup_for_this_present")
            else:
                shown = begins.get(display, [])
                if not shown:
                    problems.append("no_begin")
                for b in shown:
                    e = int(b["eye"])
                    if e in setup and close(floats(b, GAME_KEYS), floats(setup[e], GAME_KEYS), GAME_TOL):
                        continue
                    rendered = [d for d, re_ in rendered_setups(b, display) if re_ == e]
                    problems.append("begin_rendered_setup_for_display%+d" % (min(rendered, key=lambda d: abs(d - display)) - display)
                                    if rendered else "begin_pose_matches_no_setup")
                for e in (0, 1):
                    deg, mm = pose_error(layers[e], setup[e])
                    deg_errors.append(deg)
                    mm_errors.append(mm)
                    if close(floats(layers[e], XR_KEYS), floats(setup[e], XR_KEYS), POSE_TOL):
                        continue
                    if e in nxt and close(floats(layers[e], XR_KEYS), floats(nxt[e], XR_KEYS), POSE_TOL):
                        problems.append("layer_pose_is_the_next_setup")      # the one-frame skew: the latest Setup, not the image's
                    else:
                        problems.append("layer_pose_matches_no_setup")
                if any(e in nxt and not close(floats(setup[e], XR_KEYS), floats(nxt[e], XR_KEYS), POSE_TOL) for e in (0, 1)):
                    distinguishable += 1
        else:
            # ---- a one-eye frame (alternate mode)
            checked += 1
            setup = setups.get(display, {})
            if fresh not in setup:
                problems.append("no_setup_for_fresh_eye")
                other_setup = None
            else:
                s = setup[fresh]
                if len(setup) != 1:
                    problems.append("setup_was_paired")
                # 1. the Begin shown at this Present rendered this Setup's game pose
                shown = begins.get(display, [])
                if not shown:
                    problems.append("no_begin")
                elif not any(close(floats(b, GAME_KEYS), floats(s, GAME_KEYS), GAME_TOL) for b in shown):
                    problems.append("begin_pose_is_not_the_setup_for_this_present")
                    # 2. which Setup did the engine render, then? With a Setup stamped for the wrong Present its eye is the other one.
                    rendered = [de for b in shown for de in rendered_setups(b, display)]
                    if rendered and all(e != fresh for _, e in rendered):
                        problems.append("shown_image_eye_differs_from_fresh_eye")
                # 3. the fresh layer carries the Setup's XR pose and FOV
                if not close(floats(layers[fresh], XR_KEYS), floats(s, XR_KEYS), POSE_TOL):
                    problems.append("fresh_layer_pose_differs_from_setup")
                # 4. the other layer carries that eye's latest earlier Setup
                other = 1 - fresh
                earlier = latest_earlier(other, display)
                other_setup = setups[earlier][other] if earlier is not None else None
                if other_setup is None:
                    problems.append("no_earlier_setup_for_stale_eye")
                elif not close(floats(layers[other], XR_KEYS), floats(other_setup, XR_KEYS), POSE_TOL):
                    problems.append("stale_layer_pose_differs_from_its_setup")
                if earlier is not None and display - earlier > 2:
                    problems.append("stale_eye_older_than_two_frames")
            # the eye of the Setup recorded for this Present must be the fresh eye (a view promoted for the wrong eye)
            if fresh not in setup and setup:
                problems.append("setup_for_this_present_names_eye_%d_not_fresh_%d" % (next(iter(setup)), fresh))
        for p in problems:
            failures[p] += 1
            first.setdefault(p, display)
        if verbose and problems:
            print("present %d fresh %d: %s" % (display, fresh, ", ".join(problems)))
    bad = sum(failures.values())
    print("alt-pose-trace: %d tracked one-eye Presents and %d tracked paired Presents checked, %d mismatches" % (checked, paired_checked, bad))
    if paired_checked:
        print("  paired: %d of %d Presents distinguishable (the Setup for P and for P+1 differ in XR pose)" % (distinguishable, paired_checked))
        print("  paired layer-vs-image pose error: orientation deg %s; position mm %s" % (quantiles(deg_errors), quantiles(mm_errors)))
    for name, count in sorted(failures.items()):
        print("  %-55s %6d (first at Present %d)" % (name, count, first[name]))
    if checked == 0 and paired_checked == 0:
        print("  no tracked Presents in the trace")
        return 1
    if paired_checked and checked == 0 and distinguishable == 0:
        print("  no distinguishable paired Present: a static head cannot show the skew")
        return 1
    return 1 if failures else 0


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    return check(argv[1], verbose="-v" in argv[2:])


if __name__ == "__main__":
    sys.exit(main(sys.argv))
