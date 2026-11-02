"""tools/alt_pose_trace_check.py joins the probe's alt-pose-trace.csv rows (knob build/alt-pose-trace). Alternate mode: a synthetic
trace of the fixed glue passes, and a trace made the way plan F task 1 stamped its Setups (for the next Present instead of the one after,
the engine pipeline being one frame deep) fails on every alternated Present with the other eye's image in the swapchain. Paired mode: a
trace whose layers carry the Setup recorded for the Present passes; one whose layers carry the latest Setup (the legacy paired path, which
recorded the view into GameXr at Setup time) fails on every Present with layer_pose_is_the_next_setup, the one-frame pose-versus-image
skew; a static head is reported as unable to show it."""
import csv
import io
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / 'tools'))
import alt_pose_trace_check as checker  # noqa: E402

HEADER = 'event,presented,display,eye,px,py,pz,qx,qy,qz,qw,fl,fr,fu,fd,gx,gy,gz,bx,by,bz,tracked,fresh'


def xr_pose(eye, present, moving=True):
    # a head that moves every frame, with the eye's own IPD offset: the pose is unique per (eye, present)
    step = present if moving else 0
    return [0.1 * step + (0.032 if eye else -0.032), 1.6, 0.01 * step, 0, 0, 0, 1, -0.9 if eye == 0 else -0.8, 0.8 if eye == 0 else 0.9, 0.7, -0.7]


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


def paired_trace(legacy, frames=40, moving=True):
    """The paired path: both eyes are Setup while the counter reads c and shown at Present c+2 (two Begins in the next interval). The
    legacy glue recorded the views into GameXr at Setup, so Present P submitted the latest Setup (made at counter P-1, shown at P+1);
    the display-keyed glue submits the Setup recorded for P."""
    rows = []
    setups = {}
    for present in range(1, frames + 1):
        c = present - 1
        truth = c + 2
        setups[truth] = {e: (xr_pose(e, truth, moving), game_pose(e, truth)) for e in (0, 1)}
        for e in (0, 1):
            xr, game = setups[truth][e]
            rows.append(['setup', c, truth, e] + xr + game + ['', ''])
        if present in setups:
            for e in (0, 1):
                rows.append(['begin', c, present, e] + [''] * 11 + setups[present][e][1] + ['', ''])
        src = present + 1 if legacy else present
        if present in setups and src in setups:
            for e in (0, 1):
                rows.append(['present', present, present, e] + setups[src][e][0] + [''] * 6 + [1, -1])
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
assert '38 tracked one-eye Presents and 0 tracked paired Presents checked, 0 mismatches' in text, text

rc, text = run(trace(1))   # the bug: stamped for the next Present, so the Setup recorded for a Present is not the one its Begin rendered,
assert rc == 1, text       # and the one it did render is the other eye's
assert 'begin_pose_is_not_the_setup_for_this_present' in text and 'shown_image_eye_differs_from_fresh_eye' in text, text
assert 'fresh_layer_pose_differs_from_setup' not in text, text   # the glue copied the pose it believed in faithfully: only the frame was wrong

rc, text = run(HEADER + '\n')
assert rc == 1 and 'no tracked Presents' in text

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

# ---- paired mode
rc, text = run(paired_trace(legacy=False))
assert rc == 0, text
assert '0 tracked one-eye Presents and 39 tracked paired Presents checked, 0 mismatches' in text, text
assert 'paired: 39 of 39 Presents distinguishable' in text, text
assert 'orientation deg p50 0.0000  p95 0.0000  max 0.0000; position mm p50 0.0000  p95 0.0000  max 0.0000' in text, text

rc, text = run(paired_trace(legacy=True))   # the legacy paired path: every layer carries the Setup made after its image's
assert rc == 1, text
assert 'layer_pose_is_the_next_setup' in text and '    78 (first at Present 2)' in text, text   # 2 layers x 39 Presents
assert 'begin_rendered_setup' not in text and 'layer_pose_matches_no_setup' not in text, text    # the image itself is the Setup for P
assert 'position mm p50 100.4988' in text, text   # one frame of the synthetic head motion (0.1, 0, 0.01) per Present

rc, text = run(paired_trace(legacy=True, moving=False))   # a static head: the skew is invisible, and the checker says so
assert rc == 1 and 'no distinguishable paired Present' in text and 'layer_pose_is_the_next_setup' not in text, text

# a Begin that rendered another Setup than the one recorded for its Present (an image from the wrong frame)
lines = paired_trace(legacy=False).splitlines()
for i, line in enumerate(lines):
    if line.startswith('begin,19,20,0,'):
        lines[i] = 'begin,19,20,0,' + ',' * 11 + ','.join(str(v) for v in game_pose(0, 21)) + ',,'
rc, text = run('\n'.join(lines) + '\n')
assert rc == 1 and 'begin_rendered_setup_for_display+1' in text, text


# ---- v790 temporal pass rows (alternate mode)
def with_passes(text, receipt_offset=0):
    """A native TAA and an upscaler pass per Begin, recorded at the Begin's counter c for the image shown at c+1. receipt_offset 1 plans
    each pass from the next Setup's receipt, whose eye (in alternation) is the other one: that eye's history gets this image."""
    rows = list(csv.reader(io.StringIO(text)))
    eye_of = {int(r[2]): int(r[3]) for r in rows[1:] if r[0] == 'setup'}
    out = list(rows)
    for r in rows[1:]:
        if r[0] == 'begin' and int(r[1]) + 1 + receipt_offset in eye_of:
            c = int(r[1])
            d = c + 1 + receipt_offset
            for kind in (1, 2):
                out.append(['pass', c, d, eye_of[d]] + [''] * 17 + [kind, ''])
    buf = io.StringIO()
    csv.writer(buf).writerows(out)
    return buf.getvalue()


rc, text = run(with_passes(trace(2)))
assert rc == 0, text
assert 'temporal passes: 76 checked against the eye shown, 2 unjoined' in text, text
assert 'pass_' not in text, text

rc, text = run(with_passes(trace(2), receipt_offset=1))   # every pass planned from the next Setup: each eye's history gets the other's image
assert rc == 1, text
assert 'pass_receipt_is_the_next_setup' in text and 'pass_eye_differs_from_shown_eye' in text, text
assert 'taa eye 0 ran as the other eye 19' in text and 'upscaler eye 1 ran as the other eye 19' in text, text   # 38 shown, 19 per eye


# ---- v792 stale-eye warp: the stale layer carries the companion view recorded with the Present's Setup
def with_companions(text, break_at=None):
    rows = list(csv.reader(io.StringIO(text)))
    out = [rows[0]]
    for r in rows[1:]:
        if r[0] == 'present' and r[22] not in ('', '-1') and r[3] != r[22]:   # the stale layer of a one-eye Present
            p, e = int(r[2]), int(r[3])
            pose = xr_pose(e, p + 1000)                                          # a pose no Setup wrote: only the companion explains it
            out.append(['companion', p - 2, p, e] + pose + [''] * 8)
            layer = xr_pose(e, p + 2000) if p == break_at else pose
            r = r[:4] + [str(v) for v in layer] + r[15:]
        out.append(r)
    buf = io.StringIO()
    csv.writer(buf).writerows(out)
    return buf.getvalue()


rc, text = run(with_companions(trace(2)))
assert rc == 0, text
assert 'stale layers: eye 0 warped 19, eye 1 warped 19' in text, text
rc, text = run(with_companions(trace(2), break_at=20))   # a stale layer that is neither its companion nor its kept view
assert rc == 1 and 'stale_layer_pose_differs_from_its_setup' in text, text

print('PASS: alt-pose-trace checker (alternate: fixed trace passes, next-Present stamping names the wrong eye, pose drifts are caught; '
      'v792: a stale layer carrying its companion view passes, any other pose fails; '
      'paired: display-keyed layers pass, latest-Setup layers are the one-frame skew, a static head is inconclusive; '
      'temporal passes: a pass planned from the next Setup runs as the other eye)')
