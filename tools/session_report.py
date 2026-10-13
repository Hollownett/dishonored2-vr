"""One-shot report of a headset session for debugging: game-mode timeline, combat timeline, contact-hit outcome, and frame
sheets around swings/hits from the mirror recording (tools/session_recorder.py).

    python tools/session_report.py [--since HHMMSS] [--out artifacts/recordings/report-<stamp>]

Reads artifacts/recordings/*-status.jsonl and *.mp4, artifacts/native-combat.csv and the newest run's xr-live-status.json.
Writes report.md plus combat-<tick>.jpg sheets (frame 0.3 s before, at, and 0.3 s after each event). CPU only."""
import argparse
import collections
import csv
import glob
import json
import os

import cv2
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REC = os.path.join(ROOT, 'artifacts', 'recordings')
MODES = {2: 'gameplay', 4: 'pause', 16: 'notes', 64: 'press a button', 128: 'journal', 256: 'wheel', 512: 'dialogue',
         8192: 'game over', 32768: 'loading', 0: 'title/none'}


def segments(since):
    out = []
    for f in glob.glob(os.path.join(REC, '*-*.mp4')):
        stamp, tick = os.path.basename(f)[:-4].split('-', 1)
        if tick.isdigit() and stamp[9:] >= since:
            out.append((int(tick), f))
    return sorted(out)


def frame(segs, tick):
    cand = [s for s in segs if s[0] <= tick]
    if not cand:
        return None
    start, f = cand[-1]
    c = cv2.VideoCapture(f)
    c.set(cv2.CAP_PROP_POS_FRAMES, int((tick - start) / 100))
    ok, im = c.read()
    return im if ok else None


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--since', default='000000', help='recordings started at or after this local time HHMMSS')
    ap.add_argument('--out', default=None)
    a = ap.parse_args()
    rows = []
    for f in sorted(glob.glob(os.path.join(REC, '*-status.jsonl'))):
        if os.path.basename(f)[9:15] >= a.since:
            rows += [json.loads(l) for l in open(f)]
    rows.sort(key=lambda r: r['tick'])
    if not rows:
        raise SystemExit('no status rows')
    out = a.out or os.path.join(REC, 'report-%d' % rows[0]['tick'])
    os.makedirs(out, exist_ok=True)
    md = ['# Session report', '', 'status rows %d, ticks %d..%d (%.0f s)' % (len(rows), rows[0]['tick'], rows[-1]['tick'], (rows[-1]['tick'] - rows[0]['tick']) / 1000), '']
    # game-mode timeline
    spans, cur = [], None
    for r in rows:
        m = r.get('logic_mask')
        if cur is None or m != cur[0]:
            if cur:
                spans.append(cur)
            cur = [m, r['tick'], r['tick']]
        cur[2] = r['tick']
    spans.append(cur)
    md += ['## Modes', '', '| mode | start | seconds |', '|---|---|---|']
    md += ['| %s | %d | %.1f |' % (MODES.get(s[0], s[0]), s[1], (s[2] - s[1]) / 1000) for s in spans if (s[2] - s[1]) >= 500 or s[0] != 2]
    md += ['', 'left hand: ' + ', '.join('%s %d' % kv for kv in collections.Counter(r.get('left_equip') for r in rows).most_common()), '']
    # left arm drop: controller pose invalid (tracking) vs native animation owning the arm vs body solver failing
    play = [r for r in rows if r.get('logic_mask') == 2 and 'grip_tracked_hands' in r]
    if play:
        lost = [r['tick'] for r in play if not (r['grip_tracked_hands'] or 0) & 1]
        owned = [r['tick'] for r in play if ((r.get('full_body') or {}).get('owned') or [0] * 4)[2] > .5]
        fails = (play[-1].get('full_body') or {}).get('solver_failures', 0) - (play[0].get('full_body') or {}).get('solver_failures', 0)
        md += ['left arm: untracked %d/%d gameplay rows %s, native-owned %d rows %s, solver failures %d' % (
            len(lost), len(play), lost[:5], len(owned), owned[:5], fails), '']
    last = rows[-1]
    md += ['## Final counters', '', '```', json.dumps({k: last.get(k) for k in ('combat', 'contact_hits', 'contact_rejects', 'npc_blade_contacts', 'impact_fx',
                                                                            'takedown_flips', 'takedown_hints', 'prompt_shows_hides', 'overlay_hidden', 'slash_speed', 'takedown_range')}, indent=1), '```', '']
    # combat timeline
    path = os.path.join(ROOT, 'artifacts', 'native-combat.csv')
    events = [r for r in csv.reader(open(path)) if len(r) >= 4] if os.path.exists(path) else []
    events = [e for e in events if rows[0]['tick'] <= int(e[0]) <= rows[-1]['tick']]
    md += ['## Combat (%d events)' % len(events), '', dict(collections.Counter(e[1] for e in events)).__repr__(), '']
    segs = segments(a.since)
    shown = 0
    for e in events:
        if e[1] not in ('slash', 'thrust', 'overhead', 'contact_hit') or shown >= 30:
            continue
        t = int(e[0])
        ims = [frame(segs, t + d) for d in (-300, 0, 300)]
        ims = [cv2.resize(im, (im.shape[1] // 2, im.shape[0] // 2)) for im in ims if im is not None]
        if ims:
            name = 'combat-%d-%s.jpg' % (t, e[1])
            cv2.imwrite(os.path.join(out, name), np.hstack(ims))
            extra = e[4] if len(e) > 4 else ''
            md.append('- %d %s speed %s flags %s: ![](%s)' % (t, e[1], e[3], extra, name))
            shown += 1
    open(os.path.join(out, 'report.md'), 'w', encoding='utf-8').write('\n'.join(md) + '\n')
    print(os.path.join(out, 'report.md'))


if __name__ == '__main__':
    main()
