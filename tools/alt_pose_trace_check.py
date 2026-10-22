#!/usr/bin/env python3
"""Join the rows of alt-pose-trace.csv (probe knob build/alt-pose-trace, alternate-eye on) and check that every submitted eye layer
carries exactly the pose and FOV its image was rendered with.

Rows (see dxgi_probe.cpp, alt_pose_trace_*):
  setup,presented,display,eye,px..qw,fl..fd,gx,gy,gz,bx,by,bz,,      the view a Setup wrote (XR pose + FOV recorded for the layer, game-space
                                                                      position and forward axis written into the engine view) and the display
                                                                      frame that shows its image (presented + 2: the engine pipeline is one frame deep)
  begin,presented,display,eye,,,...,gx,gy,gz,bx,by,bz,,               the pose in the view's render copy when the engine begins rendering it; its
                                                                      image is on screen at `display` (presented + 1)
  present,display,display,eye,px..qw,fl..fd,,...,tracked,fresh        the layer submitted at that Present for that eye

For each tracked Present P with a fresh eye f:
  1. the Begin shown at P rendered the game pose the Setup FOR P wrote (image <-> Setup link; proves the pipeline depth);
  2. that Setup's eye is f (the fresh eye really is the eye of the image in its swapchain; when the Begin rendered a different
     Setup, the eye of the Setup it did render is compared with f);
  3. the layer of eye f carries that Setup's XR pose and FOV;
  4. the layer of the other eye carries the XR pose and FOV of the latest earlier Setup of that eye (its kept image).
Exit 0 when every checked Present passes, 1 otherwise; prints the counts either way.
"""
import csv
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


def check(path, verbose=False):
    setups, begins, presents = parse(path)
    by_game = defaultdict(list)   # rounded game pose -> the Setups that wrote it (for naming the Setup a Begin did render)
    for d in setups:
        for e in setups[d]:
            by_game[game_key(setups[d][e])].append((d, e))
    checked = 0
    failures = defaultdict(int)
    first = {}
    for display in sorted(presents):
        layers = presents[display]
        if len(layers) != 2 or layers[0]["tracked"] != "1" or layers[1]["tracked"] != "1":
            continue
        fresh = int(layers[0]["fresh"])
        if fresh < 0:
            continue  # a paired frame: both layers are this Present's own records (covered by the paired path)
        checked += 1
        setup = setups.get(display, {})
        problems = []
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
                rendered = [(d, e) for b in shown for d, e in by_game.get(game_key(b), ())
                            if close(floats(b, GAME_KEYS), floats(setups[d][e], GAME_KEYS), GAME_TOL)]
                if rendered and all(e != fresh for _, e in rendered):
                    problems.append("shown_image_eye_differs_from_fresh_eye")
            # 3. the fresh layer carries the Setup's XR pose and FOV
            if not close(floats(layers[fresh], XR_KEYS), floats(s, XR_KEYS), POSE_TOL):
                problems.append("fresh_layer_pose_differs_from_setup")
            # 4. the other layer carries that eye's latest earlier Setup
            other = 1 - fresh
            earlier = [d for d in setups if d < display and other in setups[d]]
            other_setup = setups[max(earlier)][other] if earlier else None
            if other_setup is None:
                problems.append("no_earlier_setup_for_stale_eye")
            elif not close(floats(layers[other], XR_KEYS), floats(other_setup, XR_KEYS), POSE_TOL):
                problems.append("stale_layer_pose_differs_from_its_setup")
            if earlier and display - max(earlier) > 2:
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
    print("alt-pose-trace: %d tracked one-eye Presents checked, %d mismatches" % (checked, bad))
    for name, count in sorted(failures.items()):
        print("  %-55s %6d (first at Present %d)" % (name, count, first[name]))
    if checked == 0:
        print("  no tracked one-eye Presents in the trace")
        return 1
    return 1 if failures else 0


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    return check(argv[1], verbose="-v" in argv[2:])


if __name__ == "__main__":
    sys.exit(main(sys.argv))
