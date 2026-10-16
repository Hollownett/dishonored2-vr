"""Replay a headset run's synchronized-diagnostics.jsonl: the drawn (IK) palm against the controller grip.

A native held object (bottle, oil tank) was seated on the drawn palm (held_palm_frame), so its target moved
by exactly this residual; since v600 it is seated on the controller grip (offset 0). Prints the jump statistics
and writes a PNG.

  python tools/replay_held_anchor.py <run>/synchronized-diagnostics.jsonl out.png
"""
import json, math, sys

path, png = sys.argv[1], sys.argv[2]
series = {0: [], 1: []}
with open(path) as f:
    for line in f:
        try:
            d = json.loads(line)
        except Exception:
            continue
        if not d.get('tracking_present'):
            continue
        for i, h in enumerate((d.get('hands') or [])[:2]):
            g = h.get('world_grip', {}).get('position'); p = h.get('solver_palm', {}).get('position')
            if not g or not p or g == [0, 0, 0] or p == [0, 0, 0]:
                series[i].append(None); continue
            series[i].append((d['tick'], [p[k] - g[k] for k in range(3)]))
report = {}
for i in (0, 1):
    offs = []; jumps = []; prev = None
    for s in series[i]:
        if s is None:
            prev = None; continue
        tick, r = s; offs.append(math.sqrt(sum(x * x for x in r)))
        if prev and tick - prev[0] <= 50:
            jumps.append(math.sqrt(sum((r[k] - prev[1][k]) ** 2 for k in range(3))))
        prev = s
    o = sorted(offs); j = sorted(jumps)
    report[i] = dict(frames=len(o), offset_med=o[len(o) // 2], offset_p90=o[int(.9 * len(o))], offset_max=o[-1],
                     jump_p90=j[int(.9 * len(j))], jump_p99=j[int(.99 * len(j))], jump_max=j[-1],
                     jumps_over_5cm=sum(1 for x in j if x > .05), pairs=len(j))
    print(f"hand {i}: " + ", ".join(f"{k}={v:.3f}" if isinstance(v, float) else f"{k}={v}" for k, v in report[i].items()))
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
fig, axes = plt.subplots(2, 1, figsize=(14, 7), sharex=True)
t0 = next(s for s in series[0] if s)[0]
for i, ax in enumerate(axes):
    pts = [s for s in series[i] if s]
    t = [(s[0] - t0) / 1000 for s in pts]; m = [math.sqrt(sum(x * x for x in s[1])) * 100 for s in pts]
    ax.plot(t, m, lw=.6, color='#1f77b4', label='before: object seated on the drawn palm (offset from the controller grip, cm)')
    ax.plot(t, [0] * len(t), lw=1.2, color='#2ca02c', label='after: object seated on the controller grip (offset 0)')
    ax.set_ylabel(f"{'left' if i == 0 else 'right'} hand\n|drawn palm - grip| (cm)"); ax.set_ylim(0, 60); ax.grid(alpha=.3)
    if i == 0:
        ax.legend(loc='upper right', fontsize=8)
axes[1].set_xlabel('seconds (tracked frames of ' + path.replace('\\', '/').split('/')[-2] + ')')
fig.suptitle('Held-object anchor: the drawn (IK) palm jumps against the controller grip; the object followed it')
fig.tight_layout(); fig.savefig(png, dpi=110); print('wrote', png)
