"""tools/alt_pose_trace_check.py joins the probe's alt-pose-trace.csv rows (knob build/alt-pose-trace, alternate-eye on): a synthetic trace of
the fixed glue passes, and a trace made the way plan F task 1 stamped its Setups (for the next Present instead of the one after, the engine
pipeline being one frame deep) fails on every alternated Present with the other eye's image in the swapchain."""
import csv
import io
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / 'tools'))
import alt_pose_trace_check as checker  # noqa: E402

HEADER = 'event,presented,display,eye,px,py,pz,qx,qy,qz,qw,fl,fr,fu,fd,gx,gy,gz,bx,by,bz,tracked,fresh'


def xr_pose(eye, present):
    # a head that moves every frame, with the eye's own IPD offset: the pose is unique per (eye, present)
    return [0.1 * present + (0.032 if eye else -0.032), 1.6, 0.01 * present, 0, 0, 0, 1, -0.9 if eye == 0 else -0.8, 0.8 if eye == 0 else 0.9, 0.7, -0.7]


def game_pose(eye, present):
    return [10 + present + (0.05 if eye else -0.05), 20.0, 30.0, 1.0, 0.0, 0.0]


def trace(stamp_depth, frames=40):
    """The probe's rows for Presents 1..frames with a one-frame pipeline: the Setup made while the counter reads c renders for Present
    c+2; the glue stamps it for c+stamp_depth and submits the layer with the view it promoted for that Present."""
    rows = []
    setups = {}      # true display -> (eye, xr pose, game pose)
    believed = {}    # the glue's record: believed display -> (eye, xr pose)
    last_pose = {}   # per eye: the xr pose the glue last submitted fresh (its kept record)
    for present in range(1, frames + 1):
        c = present - 1
        truth = c + 2
        eye = (c + stamp_depth) & 1        # the glue renders the parity eye of the frame it believes it renders
        xr, game = xr_pose(eye, truth), game_pose(eye, truth)
        setups[truth] = (eye, xr, game)
        believed[c + stamp_depth] = (eye, xr)
        rows.append(['setup', c, c + stamp_depth, eye] + xr + game + ['', ''])
        # the Begin of this interval renders the Setup of the previous interval (shown at this Present)
        if present in setups:
            b_eye, _, b_game = setups[present]
            rows.append(['begin', c, present, 0] + [''] * 11 + b_game + ['', ''])
        # the Present: the glue submits the view it believes belongs to this Present, the other eye its kept record
        if present in believed and (1 - believed[present][0]) in last_pose:
            f_eye, f_xr = believed[present]
            rows.append(['present', present, present, f_eye] + f_xr + [''] * 6 + [1, f_eye])
            rows.append(['present', present, present, 1 - f_eye] + last_pose[1 - f_eye] + [''] * 6 + [1, f_eye])
        if present in believed:
            last_pose[believed[present][0]] = believed[present][1]
    out = io.StringIO()
    w = csv.writer(out)
    w.writerow(HEADER.split(','))
    w.writerows(rows)
    return out.getvalue()


def run(text):
    with tempfile.NamedTemporaryFile('w', suffix='.csv', delete=False) as f:
        f.write(text)
        path = f.name
    try:
        import contextlib
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = checker.check(path)
        return rc, buf.getvalue()
    finally:
        Path(path).unlink()


rc, text = run(trace(2))
assert rc == 0, text
assert 'Presents checked, 0 mismatches' in text and ' 0 tracked' not in text, text

rc, text = run(trace(1))   # the bug: stamped for the next Present, so the Setup recorded for a Present is not the one its Begin rendered,
assert rc == 1, text       # and the one it did render is the other eye's
assert 'begin_pose_is_not_the_setup_for_this_present' in text and 'shown_image_eye_differs_from_fresh_eye' in text, text
assert 'fresh_layer_pose_differs_from_setup' not in text, text   # the glue copied the pose it believed in faithfully: only the frame was wrong

rc, text = run(HEADER + '\n')
assert rc == 1 and 'no tracked one-eye Presents' in text

# a fresh layer with a pose that is not its Setup's
bad = trace(2).replace('present,10,10,0,0.968,', 'present,10,10,0,9.999,')
assert bad != trace(2)
rc, text = run(bad)
assert rc == 1 and 'fresh_layer_pose_differs_from_setup' in text, text

# a stale layer that carries a newer pose than its image
lines = trace(2).splitlines()
for i, line in enumerate(lines):
    if line.startswith('present,12,12,1,'):   # Present 12: fresh eye 0, stale eye 1 (kept at 11)
        lines[i] = 'present,12,12,1,' + ','.join(str(v) for v in xr_pose(1, 13)) + ',,,,,,,1,0'
rc, text = run('\n'.join(lines) + '\n')
assert rc == 1 and 'stale_layer_pose_differs_from_its_setup' in text, text

print('PASS: alt-pose-trace checker (fixed trace passes; next-Present stamping names the wrong eye; pose drifts are caught)')
