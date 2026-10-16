"""Replay a headset run's synchronized-diagnostics.jsonl: the drawn (IK) palm against the controller grip.

Each record is one native upload (source_kind 0 body, 1 left arm, 2 right arm); only the hand's OWN arm record carries its
palm and world_grip from the same publication. Pairing every record (the v558 analysis) paired a stale palm with a newer
grip: 10-50 cm "jumps" that were one or two frames of locomotion (since v640 the writer nulls solver_palm when the arm
was not exported for the publication). Prints the statistics for both pairings and the drawn palette palm, writes a PNG.

  python tools/replay_held_anchor.py <run>/synchronized-diagnostics.jsonl out.png
"""
import json, math, sys

path, png = sys.argv[1], sys.argv[2]
def L(a, b): return math.sqrt(sum((a[k] - b[k]) ** 2 for k in range(3)))
def pct(a, q): a = sorted(a); return a[min(len(a) - 1, int(q * len(a)))] if a else float('nan')
# series[hand][name] = [(tick, |offset|)]: 'every' = any record (old pairing), 'own' = the hand's own arm record, 'drawn' = submitted palette palm
series = {i: {'every': [], 'own': [], 'drawn': []} for i in (0, 1)}
with open(path) as f:
    for line in f:
        try:
            d = json.loads(line)
        except Exception:
            continue
        if d.get('source') != 'owned_native_palette' or not d.get('tracking_present'):
            continue
        for i, h in enumerate((d.get('hands') or [])[:2]):
            g = (h.get('world_grip') or {}).get('position'); p = (h.get('solver_palm') or {}).get('position')
            if not g or not p or g == [0, 0, 0] or p == [0, 0, 0] or h.get('owned'):
                continue
            series[i]['every'].append((d['tick'], L(p, g)))
            if d.get('source_kind') != i + 1:
                continue
            series[i]['own'].append((d['tick'], L(p, g)))
            cp = ((d.get('consumer_landmarks') or {}).get('palm') or {}).get('position')
            if cp and L(cp, g) < 5:
                series[i]['drawn'].append((d['tick'], L(cp, g)))
for i in (0, 1):
    for name, s in series[i].items():
        offs = [x[1] for x in s]; jumps = [abs(s[n][1] - s[n - 1][1]) for n in range(1, len(s)) if s[n][0] - s[n - 1][0] <= 250]
        print(f"hand {i} {name:5}: frames={len(offs)} offset p50={pct(offs,.5):.3f} p90={pct(offs,.9):.3f} max={max(offs) if offs else 0:.3f}"
              f"  jumps>5cm={sum(1 for j in jumps if j > .05)}/{len(jumps)}")
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
fig, axes = plt.subplots(2, 1, figsize=(14, 7), sharex=True)
t0 = min(s[0][0] for i in (0, 1) for s in series[i].values() if s)
for i, ax in enumerate(axes):
    for name, color, label in (('every', '#d62728', 'palm paired with any record (stale palm vs newer grip: the v558 "jumps")'),
                               ('drawn', '#ff7f0e', 'submitted palette palm vs latest grip (leads by root motion while walking)'),
                               ('own', '#1f77b4', 'palm paired with its own arm upload (same publication)')):
        s = series[i][name]
        ax.plot([(x[0] - t0) / 1000 for x in s], [x[1] * 100 for x in s], lw=.7, color=color, label=label)
    ax.set_ylabel(f"{'left' if i == 0 else 'right'} hand\n|palm - grip| (cm)"); ax.set_ylim(0, 60); ax.grid(alpha=.3)
    if i == 0:
        ax.legend(loc='upper right', fontsize=8)
axes[1].set_xlabel('seconds (tracked frames of ' + path.replace('\\', '/').split('/')[-2] + ')')
fig.suptitle('Drawn palm vs controller grip: the jumps were a record-pairing artifact; same-publication residual is flat')
fig.tight_layout(); fig.savefig(png, dpi=110); print('wrote', png)
