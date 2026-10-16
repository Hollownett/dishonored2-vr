#!/usr/bin/env python3
"""One-page HTML proof report from simulated VR selftest logs.

    python tools/regress_report.py --version 502
    python tools/regress_report.py --logs "artifacts/build-logs/v502-*.log" --out-dir DIR
    python tools/regress_report.py --selfcheck

Logs carry "PASS|FAIL <check> - <detail>" lines, optional "status <name>: <value>" lines and a run folder
(artifacts/runs/...-arm-v<N>). The run's xr-live-status.json and timed left-eye captures (xr-t<sec>-left.rgba,
sized by xr-transfer-left.json) are summarised. Writes <out>/<version>-<YYYYmmdd-HHMM>/index.html plus 320 px
thumbnails and full-size PNGs beside it. Simulated-VR evidence only: not headset visual quality or comfort."""
import argparse
import collections
import datetime
import glob
import html
import json
import os
import re
import shutil
import sys
import tempfile

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
THUMB_WIDTH = 320
STRIP_MAX = 6
CHECK_RE = re.compile(r'^(PASS|FAIL) (.*)$')
STATUS_RE = re.compile(r'^status ([\w.-]+): (.*)$')
RUN_RE = re.compile(r'artifacts[\\/]+runs[\\/]+([^\\/"\'\r\n]*?-arm-v\d+)(?![^\\/"\'\s,\]}])')
CAPTURE_RE = re.compile(r'^xr-t(\d+(?:\.\d+)?)-left\.rgba$')
RESAMPLE = getattr(Image, 'Resampling', Image).LANCZOS
CSS = """
body{font:14px/1.4 "Segoe UI",system-ui,sans-serif;margin:20px;color:#1d2330;background:#fff}
h1{font-size:20px;margin:0 0 4px}h2{font-size:16px;margin:22px 0 8px}h3{font-size:14px;margin:0 0 6px}
.meta{margin:2px 0;color:#48505e}.muted{color:#6b7280}
table{border-collapse:collapse;width:100%;margin-top:12px}
th,td{border:1px solid #d5d9e0;padding:5px 7px;text-align:left;vertical-align:top}
th{background:#f1f3f6}
ul.checks{list-style:none;margin:0;padding:0}ul.checks li{margin:0 0 2px}
.detail{color:#48505e}
.kv{margin:0 0 2px}
.res{font-weight:700;color:#fff;text-align:center;white-space:nowrap}
.badge{display:inline-block;min-width:36px;text-align:center;border-radius:3px;padding:0 4px;font-size:11px;font-weight:700;color:#fff}
.pass{background:#1f7a3a}.fail{background:#b42318}.nochecks{background:#6b7280}
.strip{margin:14px 0}
.row{display:flex;flex-wrap:wrap;gap:10px}
figure{margin:0}figure img{display:block;width:320px;height:auto;border:1px solid #c9ced6}
figcaption{font-size:12px;color:#48505e;text-align:center;margin-top:2px}
"""


def esc(value):
    return html.escape(str(value), quote=True)


def clip(text, limit=160):
    return text if len(text) <= limit else text[:limit - 3] + '...'


def fmt(value):
    if value is None:
        return '-'
    if isinstance(value, bool):
        return str(value).lower()
    if isinstance(value, float):
        return f'{value:.4g}'
    if isinstance(value, dict):
        return ', '.join(f'{k} {fmt(v)}' for k, v in value.items())
    if isinstance(value, list):
        return '[' + ', '.join(fmt(v) for v in value) + ']'
    return str(value)


def read_json(path):
    try:
        with open(path, encoding='utf-8') as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def write_json(path, data):
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(data, f)


def parse_log(path):
    """Return ([(PASS|FAIL, check, detail)], {status name: value}, first run folder name or None)."""
    checks, status, run_name = [], {}, None
    with open(path, encoding='utf-8', errors='replace') as f:
        for raw in f:
            line = raw.rstrip('\r\n')
            if run_name is None:
                m = RUN_RE.search(line)
                if m:
                    run_name = m.group(1)
            m = CHECK_RE.match(line)
            if m:
                name, _, detail = m.group(2).partition(' - ')
                checks.append((m.group(1), name.strip(), detail.strip()))
                continue
            m = STATUS_RE.match(line)
            if m:
                try:
                    status[m.group(1)] = json.loads(m.group(2))
                except ValueError:
                    status[m.group(1)] = m.group(2).strip()
    return checks, status, run_name


def key_rows(status, live):
    """Key numbers: log status lines win for finger_ik/hand_contact, xr-live-status.json fills the rest."""
    live = live if isinstance(live, dict) else {}
    sword = live.get('sword_grip') if isinstance(live.get('sword_grip'), dict) else {}
    held = live.get('held_object') if isinstance(live.get('held_object'), dict) else {}
    rows = [(f'sword_grip.{k}', fmt(sword[k])) for k in ('use_diag', 'doors', 'stealth') if k in sword]
    if 'placed' in held or 'throws' in held:
        rows.append(('held_object placed / throws', f'{fmt(held.get("placed"))} / {fmt(held.get("throws"))}'))
    for name in ('hand_contact', 'finger_ik'):
        value = status[name] if name in status else live.get(name)
        if value is not None:
            rows.append((name, fmt(value)))
    rows += [(f'status {k}', fmt(v)) for k, v in status.items() if k not in ('hand_contact', 'finger_ik')]
    return rows


def list_captures(run_dir):
    found = []
    for entry in os.listdir(run_dir):
        m = CAPTURE_RE.match(entry)
        if m:
            found.append((float(m.group(1)), os.path.join(run_dir, entry)))
    return sorted(found)


def capture_size(run_dir, live):
    for source in (read_json(os.path.join(run_dir, 'xr-transfer-left.json')), live):
        if isinstance(source, dict) and source.get('width') and source.get('height'):
            return int(source['width']), int(source['height'])
    return None


def pick_evenly(items, count):
    if len(items) <= count:
        return list(items)
    last = len(items) - 1
    picks = sorted({round(i * last / (count - 1)) for i in range(count)})
    return [items[i] for i in picks]


def write_capture(path, size, sec, report_dir, base):
    width, height = size
    with open(path, 'rb') as f:
        data = f.read()
    if len(data) != width * height * 4:
        raise ValueError(f'{len(data)} bytes, expected {width * height * 4} for {width}x{height} RGBA')
    image = Image.frombytes('RGBA', (width, height), data).convert('RGB')
    stem = f'{base}-t{sec:g}'.replace('.', '_')
    full, thumb = f'{stem}-full.png', f'{stem}-thumb.png'
    image.save(os.path.join(report_dir, full))
    thumb_height = max(1, round(height * THUMB_WIDTH / width))
    image.resize((THUMB_WIDTH, thumb_height), RESAMPLE).save(os.path.join(report_dir, thumb))
    return {'sec': sec, 'label': f't = {sec:g} s', 'full': full, 'thumb': thumb}


def build_scenario(log_path, version, root, report_dir):
    stem = os.path.splitext(os.path.basename(log_path))[0]
    prefix = f'v{version}-' if version else ''
    sc = {'name': stem[len(prefix):] if prefix and stem.startswith(prefix) else stem,
          'log': os.path.basename(log_path), 'base': re.sub(r'[^A-Za-z0-9._-]+', '_', stem),
          'checks': [], 'keys': [], 'run': None, 'captures': [], 'images': [], 'notes': []}
    try:
        sc['checks'], status, sc['run'] = parse_log(log_path)
        run_dir = None
        if sc['run'] is None:
            sc['notes'].append('no run folder named in log')
        else:
            run_dir = os.path.join(root, 'artifacts', 'runs', sc['run'])
            if not os.path.isdir(run_dir):
                sc['notes'].append('run folder missing')
                run_dir = None
        live = read_json(os.path.join(run_dir, 'xr-live-status.json')) if run_dir else None
        sc['keys'] = key_rows(status, live)
        if run_dir:
            sc['captures'] = list_captures(run_dir)
            size = capture_size(run_dir, live)
            if sc['captures'] and size is None:
                sc['notes'].append('capture size unknown (no xr-transfer-left.json)')
            elif size:
                for sec, path in pick_evenly(sc['captures'], STRIP_MAX):
                    try:
                        sc['images'].append(write_capture(path, size, sec, report_dir, sc['base']))
                    except (OSError, ValueError) as exc:
                        sc['notes'].append(f't = {sec:g} s capture unreadable: {exc}')
    except Exception as exc:  # one broken log must not sink the whole report
        sc['notes'].append(f'error: {exc}')
    if not sc['checks']:
        sc['overall'] = 'NO CHECKS'
    elif any(c[0] == 'FAIL' for c in sc['checks']):
        sc['overall'] = 'FAIL'
    else:
        sc['overall'] = 'PASS'
    return sc


def embed(index):
    """index-embedded.html beside index.html: every thumbnail inlined as a data URI (links to full-size PNGs stay relative)."""
    import base64
    folder = os.path.dirname(index)
    text = open(index, encoding='utf-8').read()
    def inline(m):
        path = os.path.join(folder, m.group(1))
        if not os.path.isfile(path):
            return m.group(0)
        import io
        buf = io.BytesIO()
        Image.open(path).convert('RGB').save(buf, 'JPEG', quality=70)  # photographic eye captures: JPEG keeps the page phone-sized
        return 'src="data:image/jpeg;base64,' + base64.b64encode(buf.getvalue()).decode('ascii') + '"'
    out = os.path.join(folder, 'index-embedded.html')
    open(out, 'w', encoding='utf-8').write(re.sub(r'src="([^"]+-thumb\.png)"', inline, text))
    return out


def render_strip(s, cls):
    figures = ''.join(
        f'<figure><a href="{esc(im["full"])}"><img src="{esc(im["thumb"])}" width="{THUMB_WIDTH}" '
        f'alt="{esc(s["name"])} left eye {esc(im["label"])}"></a>'
        f'<figcaption>{esc(im["label"])}</figcaption></figure>'
        for im in s['images'])
    body = f'<div class="row">{figures}</div>' if figures else '<p class="muted">no captures</p>'
    return (f'<section class="strip" id="strip-{s["base"]}"><h3>{esc(s["name"])} '
            f'<span class="badge {cls}">{esc(s["overall"])}</span> '
            f'<small>{len(s["captures"])} left-eye captures</small></h3>{body}</section>')


def render_html(label, stamp, scenarios):
    tally = collections.Counter(s['overall'] for s in scenarios)
    checks = [c for s in scenarios for c in s['checks']]
    fails = sum(1 for c in checks if c[0] == 'FAIL')
    images = sum(len(s['images']) for s in scenarios)
    rows, strips = [], []
    for s in scenarios:
        cls = {'PASS': 'pass', 'FAIL': 'fail'}.get(s['overall'], 'nochecks')
        checks_html = ''.join(
            f'<li><span class="badge {st.lower()}">{st}</span> <b>{esc(name)}</b> '
            f'<span class="detail" title="{esc(detail)}">{esc(clip(detail))}</span></li>'
            for st, name, detail in s['checks']) or '<li class="muted">no PASS/FAIL lines</li>'
        keys_html = ''.join(f'<div class="kv"><b>{esc(k)}</b> {esc(v)}</div>' for k, v in s['keys'])
        keys_html = keys_html or '<div class="muted">no status data</div>'
        if s['images']:
            captures = f'{len(s["captures"])} left-eye captures, {len(s["images"])} in strip'
        else:
            captures = 'no captures'
        notes = ''.join(f'<div class="muted">{esc(n)}</div>' for n in s['notes'])
        rows.append(
            f'<tr id="row-{s["base"]}"><td><b>{esc(s["name"])}</b><br><small>{esc(s["log"])}</small>'
            f'<br><small>run {esc(s["run"] or "none")}</small></td>'
            f'<td class="res {cls}">{esc(s["overall"])}</td>'
            f'<td><ul class="checks">{checks_html}</ul></td>'
            f'<td>{keys_html}</td><td>{captures}{notes}</td></tr>')
        strips.append(render_strip(s, cls))
    if rows:
        table_rows = ''.join(rows)
        table = ('<table><thead><tr><th>Scenario</th><th>Result</th><th>Checks</th><th>Key status</th>'
                 '<th>Captures</th></tr></thead><tbody>' + table_rows + '</tbody></table>')
        strip_html = ''.join(strips)
    else:
        table = '<p class="muted">No logs matched.</p>'
        strip_html = '<p class="muted">No scenarios.</p>'
    summary = (f'{len(scenarios)} scenarios: {tally.get("PASS", 0)} PASS, {tally.get("FAIL", 0)} FAIL, '
               f'{tally.get("NO CHECKS", 0)} no checks. {len(checks)} checks ({fails} FAIL). '
               f'{images} capture images.')
    return '\n'.join([
        '<!doctype html>',
        '<html lang="en"><head><meta charset="utf-8">',
        f'<title>VR regression proof v{esc(label)} {esc(stamp)}</title>',
        f'<style>{CSS}</style></head>',
        '<body>',
        f'<h1>Simulated VR regression proof: v{esc(label)}</h1>',
        f'<p class="meta">Generated {esc(stamp)}. {esc(summary)}</p>',
        '<p class="meta">Simulated-VR selftests only. These checks do not establish headset visual quality, '
        'comfort or complete gameplay.</p>',
        table,
        '<h2>Left-eye captures</h2>',
        strip_html,
        '</body></html>',
        ''])


def generate(log_paths, version, root, out_dir, label, stamp):
    report_dir = os.path.join(out_dir, f'{label}-{stamp}')
    os.makedirs(report_dir, exist_ok=True)
    scenarios = [build_scenario(path, version, root, report_dir) for path in log_paths]
    index = os.path.join(report_dir, 'index.html')
    with open(index, 'w', encoding='utf-8') as f:
        f.write(render_html(label, stamp, scenarios))
    return index, scenarios


def selfcheck():
    tmp = tempfile.mkdtemp(prefix='regress-report-check-')
    try:
        logs = os.path.join(tmp, 'artifacts', 'build-logs')
        run_dir = os.path.join(tmp, 'artifacts', 'runs', '20261014T000000.000000Z-pid1-arm-v999')
        os.makedirs(logs)
        os.makedirs(run_dir)
        write_json(os.path.join(run_dir, 'xr-transfer-left.json'), {'width': 4, 'height': 4})
        write_json(os.path.join(run_dir, 'xr-live-status.json'), {
            'sword_grip': {'use_diag': [0, 1, 2], 'doors': [0], 'stealth': [0, 0]},
            'held_object': {'placed': 2, 'throws': 1}, 'hand_contact': [1, 2, 3]})
        for sec in ('1', '2.5'):
            with open(os.path.join(run_dir, f'xr-t{sec}-left.rgba'), 'wb') as f:
                f.write(bytes(range(64)))  # 4x4 RGBA
        with open(os.path.join(logs, 'v999-selfcheck-demo.log'), 'w', encoding='utf-8') as f:
            f.write('PASS reached gameplay - logic mask 2\n'
                    'status finger_ik: {"frames": 7, "hits": 0}\n'
                    'status hand_contact: [1, 2, 3]\n'
                    f'run: {run_dir}\n'
                    'PASS nothing clipped - max edge coverage 0.000\n')
        with open(os.path.join(logs, 'v999-selfcheck-missing.log'), 'w', encoding='utf-8') as f:
            f.write('PASS injection - ok\n'
                    'FAIL corpse found - none\n'
                    'run: C:\\nowhere\\artifacts\\runs\\gone-arm-v999\n')
        log_paths = sorted(glob.glob(os.path.join(logs, 'v999-*.log')))
        index, _ = generate(log_paths, '999', tmp, os.path.join(tmp, 'reports'), '999', 'selfcheck')
        text = open(index, encoding='utf-8').read()
        rows = re.findall(r'<tr\b.*?</tr>', text, re.S)
        demo = [r for r in rows if 'selfcheck-demo' in r]
        assert len(demo) == 1 and '>PASS<' in demo[0] and 'frames 7' in demo[0], 'demo table row missing'
        assert any('FAIL' in r for r in rows if 'selfcheck-missing' in r), 'FAIL row missing'
        assert 'held_object placed / throws' in text and '2 / 1' in text, 'key status missing'
        assert 'no captures' in text, 'missing run should say no captures'
        thumb = re.search(r'<img [^>]*src="([^"]+-thumb\.png)"', text)
        full = re.search(r'<a href="([^"]+-full\.png)"', text)
        assert thumb and full, 'no <img> thumbnail or full-size link'
        report_dir = os.path.dirname(index)
        assert Image.open(os.path.join(report_dir, thumb.group(1))).width == THUMB_WIDTH
        assert os.path.isfile(os.path.join(report_dir, full.group(1)))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description='Build a one-page HTML proof report from simulated VR selftest logs.')
    parser.add_argument('--version', help='build version, e.g. 502 (logs: artifacts/build-logs/v<VER>-*.log)')
    parser.add_argument('--logs', nargs='+', metavar='GLOB', help='explicit log glob(s) instead of --version')
    parser.add_argument('--out-dir', default=os.path.join(ROOT, 'artifacts', 'reports'),
                        help='base folder for report folders (default artifacts/reports)')
    parser.add_argument('--embed', action='store_true',
                        help='also write index-embedded.html with the thumbnails inlined (one file to send to a phone)')
    parser.add_argument('--selfcheck', action='store_true',
                        help='build a synthetic run in a temp dir and assert the report')
    args = parser.parse_args(argv)
    if args.selfcheck:
        selfcheck()
        print('selfcheck OK')
        return 0
    version = args.version.lstrip('vV') if args.version else None
    if not version and not args.logs:
        parser.error('give --version VER or --logs GLOB')
    patterns = args.logs or [os.path.join(ROOT, 'artifacts', 'build-logs', f'v{version}-*.log')]
    found = {}
    for pattern in patterns:
        for path in glob.glob(pattern):
            if os.path.isfile(path):
                found.setdefault(os.path.normcase(os.path.realpath(path)), path)
    log_paths = sorted(found.values(), key=os.path.basename)
    label = version or 'report'
    stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M')
    index, scenarios = generate(log_paths, version, ROOT, args.out_dir, label, stamp)
    images = sum(len(s['images']) for s in scenarios)
    print(f'report: {os.path.abspath(index)}')
    if args.embed:
        print(f'embedded: {embed(index)}')
    print(f'scenarios: {len(scenarios)} (from {len(log_paths)} logs); capture images: {images} '
          f'({images} thumbnails + {images} full-size PNGs)')
    if not log_paths:
        print('warning: no logs matched')
    return 0


if __name__ == '__main__':
    sys.exit(main())
