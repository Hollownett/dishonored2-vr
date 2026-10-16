#!/usr/bin/env python3
"""Plots and index.html for the offline push lab: python tools/push_lab_plots.py <out_dir>   (tools/push_lab.sh runs it).
Reads what push_lab.exe wrote (scenarios.json, <name>.csv, <name>-proxies.csv) and writes <name>.png per scenario and index.html."""
import html
import json
import sys
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.collections import PatchCollection
from matplotlib.patches import Circle, Rectangle

PALM, FINGER, PROP, HAND, EST = '#e8833a', '#3a7bd5', '#c0392b', '#888888', '#2c6fbb'
STATUS_COLOUR = {'pass': '#1e8c4a', 'fail': '#c0392b', 'info': '#777777'}
WIDTH = 9.5  # inches of axes width: sets the height of the equal-aspect views


def load(path):
    return np.genfromtxt(path, delimiter=',', names=True)


def outline(ax, view, centre, size, **kw):
    """The prop's outline in a view: a circle for the bottle in plan, else the rectangle of its bounds."""
    a, b = view
    if view == (0, 1) and size[0] < 0.1:
        ax.add_patch(Circle((centre[a], centre[b]), size[0] / 2, fill=False, **kw))
    else:
        ax.add_patch(Rectangle((centre[a] - size[a] / 2, centre[b] - size[b] / 2), size[a], size[b], fill=False, **kw))


def draw_view(ax, view, d, prox, meta, label, xlim=None):
    a, b = view
    key = 'xyz'
    n = len(d)
    dt = d['t'][1] - d['t'][0]
    size, start = meta['size'], meta['start']
    prop = np.stack([d['prop_x'], d['prop_y'], d['prop_z']], 1)
    px, py = prox[key[a]], prox[key[b]]            # frames x 21 sphere centres in this view
    radius = prox['r'].reshape(n, -1)
    # hand proxy spheres over time: a faded trail, every 0.25 s, the last frame bold
    step = max(1, round((0.25 if n * dt < 4.5 else 0.5) / dt))
    frames = sorted(set(range(0, n, step)) | {n - 1})
    alphas = np.linspace(0.12, 0.6, len(frames))
    for k, f in enumerate(frames):
        last = f == frames[-1]
        patches = [Circle((px[f, i], py[f, i]), radius[f, i]) for i in range(px.shape[1])]
        colours = [PALM if i == 0 else FINGER for i in range(px.shape[1])]
        ax.add_collection(PatchCollection(patches, facecolor=colours if last else 'none', edgecolor=colours, alpha=0.4 if last else alphas[k], linewidth=1.0 if last else 0.7, zorder=2))
        if last:
            ax.add_collection(PatchCollection(patches, facecolor='none', edgecolor=colours, linewidth=1.2, zorder=2))
    # the prop: start (dashed), every ~0.5 s (faint), end (solid), the path of its centre
    for f in range(0, n, max(1, round(0.5 / dt))):
        outline(ax, view, prop[f], size, edgecolor=PROP, alpha=0.15, linewidth=0.7, zorder=3)
    outline(ax, view, start, size, edgecolor=PROP, linestyle='--', linewidth=1.2, zorder=4)
    outline(ax, view, prop[-1], size, edgecolor=PROP, linewidth=1.8, zorder=4)
    ax.plot(prop[:, a], prop[:, b], color=PROP, linewidth=1.0, zorder=4)
    ax.plot(d['hand_' + key[a]], d['hand_' + key[b]], color=PALM, linewidth=0.6, linestyle=':', zorder=3)
    # push vectors: the prop's velocity as the mod wrote it (arrow = 0.12 s of travel), every 4th push
    push = np.flatnonzero(d['push'] > 0)[::4]
    if len(push):
        ax.quiver(prop[push, a], prop[push, b], d['w' + key[a]][push] * 0.12, d['w' + key[b]][push] * 0.12, angles='xy', scale_units='xy', scale=1,
                  width=0.0022, color='#7a1f14', alpha=0.85, zorder=5)
    if meta['wall'] is not None:
        if a == 0:
            ax.axvline(meta['wall'], color='black', linewidth=3, zorder=6)
    if view == (0, 2):
        ax.axhline(0, color='black', linewidth=1.5, zorder=6)
        ax.axhspan(-0.05, 0, color='#cccccc', zorder=1)
    ax.set_ylabel(label)
    ax.grid(alpha=0.25, linewidth=0.5)
    lo_x = min(px.min(), prop[:, a].min() - size[a] / 2)
    hi_x = max(px.max(), prop[:, a].max() + size[a] / 2)
    lo_y = min((py - radius).min(), prop[:, b].min() - size[b] / 2)
    hi_y = max((py + radius).max(), prop[:, b].max() + size[b] / 2)
    if meta['wall'] is not None:
        hi_x = max(hi_x, meta['wall'] + 0.02)
    if view == (0, 2):
        lo_y = min(lo_y, -0.03)
    pad = 0.03
    xlim = xlim or (lo_x - pad, hi_x + pad)
    ax.set_xlim(*xlim)
    ax.set_ylim(lo_y - pad, hi_y + pad)
    ax.set_aspect('equal', adjustable='box', anchor='NW')
    return (hi_y - lo_y + 2 * pad) / (xlim[1] - xlim[0]), (lo_x - pad, hi_x + pad)


def lanes(ax, t, flags, label_x):
    """Thin flag lanes along the top of an axes (axes-fraction y)."""
    for k, (name, key, colour) in enumerate(flags):
        y0 = 0.93 - 0.075 * k
        ax.fill_between(t, y0, y0 + 0.06, where=key > 0, step='post', transform=ax.get_xaxis_transform(), color=colour, linewidth=0)
        ax.text(label_x, y0 + 0.03, name, transform=ax.transAxes, fontsize=7, va='center', color=colour)


def plot(out, meta):
    name = meta['name']
    d = load(out / f'{name}.csv')
    p = load(out / f'{name}-proxies.csv')
    n = len(d)
    prox = {k: p[k].reshape(n, -1) for k in ('x', 'y', 'z')}
    prox['r'] = p['r']
    # shape the figure by the data: equal-aspect views get the height their extents need
    probe = plt.figure()
    pa = probe.add_subplot(111)
    ratio_xy, xlim_xy = draw_view(pa, (0, 1), d, prox, meta, 'y')
    ratio_xz, xlim_xz = draw_view(pa, (0, 2), d, prox, meta, 'z')
    plt.close(probe)
    xlim = (min(xlim_xy[0], xlim_xz[0]), max(xlim_xy[1], xlim_xz[1]))
    ratio_xy *= (xlim_xy[1] - xlim_xy[0]) / (xlim[1] - xlim[0])
    ratio_xz *= (xlim_xz[1] - xlim_xz[0]) / (xlim[1] - xlim[0])
    h_xy = float(np.clip(WIDTH * ratio_xy, 1.4, 4.6))
    h_xz = float(np.clip(WIDTH * ratio_xz, 1.5, 4.6))
    terms = 'error' in d.dtype.names and bool(np.isfinite(d['error']).any())   # the CSV carries the control terms (lab with the world-cast model)
    heights = [h_xy, h_xz, 1.7, 2.3] + ([2.1] if terms else [])
    fig = plt.figure(figsize=(WIDTH + 2.2, sum(heights) + 2.2), dpi=85)
    gs = fig.add_gridspec(len(heights), 1, height_ratios=heights, hspace=0.42, left=0.08, right=0.82, top=1 - 1.3 / fig.get_figheight(), bottom=0.5 / fig.get_figheight())
    ax_xy = fig.add_subplot(gs[0])
    ax_xz = fig.add_subplot(gs[1])
    ax_v = fig.add_subplot(gs[2])
    ax_c = fig.add_subplot(gs[3], sharex=ax_v)
    ax_k = fig.add_subplot(gs[4], sharex=ax_v) if terms else None
    draw_view(ax_xy, (0, 1), d, prox, meta, 'y (m)', xlim)
    draw_view(ax_xz, (0, 2), d, prox, meta, 'z (m)', xlim)
    ax_xy.set_title('top-down (x-y): hand spheres every 0.25 s (palm orange, fingers blue, faded = older), prop outline: start dashed / end solid, red arrows = written velocity x 0.12 s',
                    fontsize=8, loc='left')
    ax_xz.set_title('side (x-z)', fontsize=8, loc='left')
    ax_xz.set_xlabel('x (m)')
    t = d['t']
    ax_v.plot(t, d['hand_speed'], color=HAND, linewidth=0.8, label='hand speed (true)')
    ax_v.plot(t, d['est_speed'], color=EST, linewidth=1.2, label='hand speed (HandMotion estimate the contact uses)')
    ax_v.plot(t, d['prop_speed'], color=PROP, linewidth=1.5, label='prop speed')
    ax_v.set_ylabel('m/s')
    if 'tilt_deg' in d.dtype.names:
        ax_t = ax_v.twinx()
        ax_t.plot(t, d['tilt_deg'], color='#2e8b57', linewidth=1.0, linestyle='--', label='prop tilt (deg, right)')
        ax_t.set_ylabel('tilt (deg)', color='#2e8b57')
        ax_t.set_ylim(0, max(10, d['tilt_deg'].max() * 1.2))
        ax_t.axhline(8, color='#2e8b57', linewidth=0.5, linestyle=':')
    ax_v.legend(fontsize=7, loc='upper right', ncol=3)
    ax_v.grid(alpha=0.25, linewidth=0.5)
    ax_v.set_title('speeds', fontsize=8, loc='left')
    ax_v.set_ylim(top=max(ax_v.get_ylim()[1], 0.1) * 1.4)
    lanes(ax_c, t, [('touching', d['touching'], '#b8860b'), ('driving', d['driving'], '#2c6fbb'), ('push written', d['push'], PROP), ('sticky window', d['engaged'], '#888888')], 1.1 if meta['gap_applies'] else 1.01)
    ax_c.plot(t, d['overlap'] * 1000, color=PALM, linewidth=1.2, label='deepest proxy overlap (all proxies)')
    ax_c.plot(t, d['depth'] * 1000, color=EST, linewidth=1.0, linestyle='--', label='depth the contact acts on')
    ax_c.set_ylabel('penetration (mm)')
    (ax_k or ax_c).set_xlabel('time (s)')
    ax_c.set_ylim(top=max(ax_c.get_ylim()[1], 5) * 1.5)
    ax_c.grid(alpha=0.25, linewidth=0.5)
    ax_c.legend(fontsize=7, loc='upper left', bbox_to_anchor=(0.0, 0.7))
    ax_c.set_title('contact flags (lanes) and penetration' + ('; right axis: gap, prop back face to hand front (mm)' if meta['gap_applies'] else ''), fontsize=8, loc='left')
    if meta['gap_applies']:
        ax2 = ax_c.twinx()
        ax2.plot(t, d['gap'] * 1000, color='#555555', linewidth=1.0)
        ax2.axhline(0, color='#555555', linewidth=0.5, linestyle=':')
        ax2.set_ylabel('gap (mm)', color='#555555')
        ax2.set_ylim(*np.clip(ax2.get_ylim(), -300, 300))
    if ax_k is not None:   # the terms behind each write: error = gap - deadband (mm), the cast's shift of the drawn hand (cm), trim and friction (cm/s)
        shift = np.sqrt(d['shift_x'] ** 2 + d['shift_y'] ** 2 + d['shift_z'] ** 2)
        ax_k.plot(t, d['error'] * 1000, color='#c0392b', linewidth=1.2, label='error = gap - deadband (mm)')
        ax_k.plot(t, shift * 1000, color='#8e44ad', linewidth=1.0, label='world cast shift of the drawn hand (mm; not applied while the hand meets the prop)')
        ax_k.plot(t, d['trim'] * 1000, color='#2e8b57', linewidth=1.0, label='trim (mm/s)')
        ax_k.plot(t, d['friction'] * 1000, color='#b8860b', linewidth=1.0, label='friction given back (mm/s)')
        if 'meet_gap' in d.dtype.names:   # the drawn hand meets what it pushes
            way_out = np.sqrt(d['meet_x'] ** 2 + d['meet_y'] ** 2 + d['meet_z'] ** 2)
            ax_k.plot(t, way_out * 1000, color='#117a8b', linewidth=1.0, linestyle='--', label='drawn hand moved out of the prop (mm)')
            ax_k.plot(t, np.where(d['meet_gap'] < 0.15, d['meet_gap'], np.nan) * 1000, color='#0b2e59', linewidth=1.3, label='nearest drawn proxy to the prop surface (mm; < 0 = inside)')
        ax_k.axhline(0, color='#555555', linewidth=0.5)
        ax_k.set_ylabel('mm, mm/s')
        ax_k.set_ylim(max(ax_k.get_ylim()[0], -150), min(ax_k.get_ylim()[1], 300))
        ax_k.grid(alpha=0.25, linewidth=0.5)
        ax_k.legend(fontsize=7, loc='upper right', ncol=2)
        ax_k.set_title('control terms behind the writes', fontsize=8, loc='left')
    if meta['reversal'] is not None:
        for ax in (ax_v, ax_c):
            ax.axvline(meta['reversal'], color='black', linewidth=0.8, linestyle=':')
    m = meta['metrics']
    colour = STATUS_COLOUR[meta['status']]
    fig.text(0.08, 1 - 0.32 / fig.get_figheight(), meta['title'], fontsize=13, fontweight='bold', va='top')
    fig.text(0.08, 1 - 0.62 / fig.get_figheight(),
             f"[{meta['status'].upper()}]  expect: {meta['expect']}", fontsize=9, color=colour, va='top')
    fig.text(0.08, 1 - 0.88 / fig.get_figheight(),
             f"displaced {m['horizontal'] * 100:.2f} cm, lateral drift {m['drift'] * 1000:.2f} mm, lift {m['lift'] * 1000:.2f} mm, peak prop speed {m['peak']:.3f} m/s, "
             f"max depth {m['depth'] * 1000:.1f} mm, {m['pushes']} pushes over {n} frames", fontsize=8.5, color='#333333', va='top')
    fig.savefig(out / f'{name}.png')
    plt.close(fig)


def cm(v, digits=2, scale=100):
    return '-' if v is None else f'{v * scale:.{digits}f}'


def index(out, doc):
    rows, details = [], []
    for s in doc['scenarios']:
        m = s['metrics']
        colour = STATUS_COLOUR[s['status']]
        failed = '; '.join(html.escape(c['name']) for c in s['checks'] if not c['ok'])
        rows.append(
            f"<tr><td><a href='#{s['name']}'>{s['name']}</a></td><td style='color:{colour};font-weight:bold'>{s['status'].upper()}</td>"
            f"<td>{cm(m['horizontal'])}</td><td>{cm(m['gap']) if s['gap_applies'] else '-'}</td><td>{m['drift'] * 1000:.2f}</td><td>{m['lift'] * 1000:.2f}</td>"
            f"<td>{m['peak']:.3f}</td><td>{m['hand_peak']:.3f}</td><td>{m['jitter'] * 1000:.3f}</td><td>{cm(m['creep'], 2, 1000)}</td><td>{cm(m['ripple'], 0, 1000)}</td><td>{m['depth'] * 1000:.1f}</td>"
            f"<td>{m['pushes']}</td><td class='l'>{failed}</td></tr>")
        checks = ''.join(
            f"<tr><td style='color:{'#1e8c4a' if c['ok'] else '#c0392b'}'>{'ok' if c['ok'] else 'FAIL'}</td><td class='l'>{html.escape(c['name'])}</td><td>{html.escape(c['value'])}</td><td>{html.escape(c['limit'])}</td></tr>"
            for c in s['checks'])
        extra = f" Prop mass read as {s['mass']:.3g} kg." + (' Hand jitter on.' if s['jitter'] else '') + (f" Hand sampled in {s['step'] * 1000:.0f} ms steps." if s['step'] else '')
        details.append(
            f"<h2 id='{s['name']}'>{html.escape(s['title'])} <span style='color:{STATUS_COLOUR[s['status']]}'>[{s['status'].upper()}]</span></h2>"
            f"<p>{html.escape(s['desc'])}{extra}<br>Expected: {html.escape(s['expect'])}. <a href='{s['name']}.csv'>{s['name']}.csv</a></p>"
            f"<img src='{s['name']}.png'><table class='checks'><tr><th></th><th class='l'>check</th><th>value</th><th>limit</th></tr>{checks}</table>")
    cfg = ', '.join(f'{k}={v:g}' for k, v in doc['config'].items())
    page = f"""<!doctype html><meta charset='utf-8'><title>push lab</title>
<style>body{{font:14px/1.45 system-ui,sans-serif;margin:1.5em;max-width:1500px}}table{{border-collapse:collapse;margin:.6em 0}}td,th{{border:1px solid #ccc;padding:3px 8px;text-align:right}}
.l{{text-align:left}}img{{max-width:100%}}h2{{margin-top:2em}}.checks td{{font-size:12px}}code{{background:#eee;padding:0 3px}}</style>
<h1>Offline push lab</h1>
<p>Hand-to-prop pushing at 90 Hz with the <b>current</b> <code>src/hand_contact.hpp</code> (hand_proxies, HandMotion, prop_mass, prop_shape, hand_contact with the service's sticky window) and a simple prop: gravity, a table with Coulomb friction (mu 0.4),
an optional static wall (hard stop), velocity writes as the unit test's sims apply them. The hand is the unit test's synthetic hand (fingers +X unless noted). Limits are the ones <code>tests/hand_contact_check.cpp</code> asserts; <i>lab</i> marks a limit the unit test does not have.
Engine dynamics: the hand writes at 90 Hz, the world steps at 62.5 Hz and takes table friction off before it moves the prop; a tipping model (friction torque at the base, no gravity restoring, calibrated to the v503 live 45 degrees in 2 s; the green dashed trace). Not modelled: the engine's contact solver, the drawn-pose hold-off of the world cast (the hand is drawn where it is tracked).
Hand jitter (rest scenarios) is an assumed model: 0.5 mm white noise + 1 mm 9 Hz tremor + 3 mm 0.6 Hz sway per axis, fixed seed.</p>
<p>Config: {html.escape(cfg)}</p>
<table><tr><th class='l'>scenario</th><th>result</th><th>prop displaced (cm)</th><th>gap prop face - hand front (cm)</th><th>max lateral drift (mm)</th><th>max lift (mm)</th><th>max prop speed (m/s)</th><th>peak hand speed est. (m/s)</th>
<th>prop jitter p-p, last 1 s (mm)</th><th>creep of the prop beside a still hand, last 2 s (mm)</th><th>speed ripple while pushed, p-p (mm/s)</th><th>max depth (mm)</th><th>pushes</th><th class='l'>failed checks</th></tr>{''.join(rows)}</table>
<p><a href='summary.csv'>summary.csv</a></p>{''.join(details)}"""
    (out / 'index.html').write_text(page, encoding='utf-8')


def main():
    out = Path(sys.argv[1])
    doc = json.loads((out / 'scenarios.json').read_text(encoding='utf-8'))
    for s in doc['scenarios']:
        plot(out, s)
    index(out, doc)


if __name__ == '__main__':
    main()
