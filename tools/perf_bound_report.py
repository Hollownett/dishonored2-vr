#!/usr/bin/env python3
"""Is the game CPU-bound or GPU-bound? One row per sim-rig run, then a verdict line.

usage: perf_bound_report.py RUN [RUN ...] [--before PREVIOUS.json] [--json OUT.json]
RUN is a sim run folder or a status/result json. The data is the "frame_split" block ({"adapter":..., "split":...}) of
artifacts/xr-live-status.json (XR runs), else of artifacts/dxgi-probe-result.json, the only file a flat run
(--probe-mode paired|single) writes. The row says which file it used ("source").
gpu_busy is the SUM of the native command-list durations over the interval; gpu_span is the elapsed first-start-to-last-end
span, which also counts idle gaps between lists. The verdict uses gpu_busy only; gpu_span is shown so a span-only "GPU-bound"
(span .97 while the lists sum to .80) is visible as the gap it is.
Verdict per run: GPU-BOUND when gpu_busy >= .90; CPU-BOUND(render) or CPU-BOUND(game) when gpu_busy < .70 and that thread's
busy fraction is >= .85 (render first); otherwise MIXED. INVALID runs (missing file or block, no gameplay, no frames, an
adapter that is not NVIDIA) are listed with the reason and never averaged in. The overall VERDICT line is GPU-BOUND,
CPU-BOUND(render), CPU-BOUND(game), MIXED or INVALID. Gameplay: the status file says it; the result file has no gameplay flag,
so a flat row counts every counted Present (menu frames included) and its note says so. Each row lists the knob files that
were in build/ when the run launched (run-info.json): compare A/B rows only when those match. --json writes the rows;
a later run passes that file as --before to print the delta.
"""
import argparse
import json
import sys
from pathlib import Path

GPU_BOUND = 0.90
GPU_LOW = 0.70
THREAD_BOUND = 0.85
MODES = {"xr", "paired", "single"}


def _load(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def _run_info(run):
    try:
        info = _load(run / "run-info.json")
        return info if isinstance(info, dict) else {}
    except (OSError, ValueError):
        return {}


def _num(v):
    return v if isinstance(v, (int, float)) and not isinstance(v, bool) else None


def classify(row):
    """-> verdict string for one row dict. INVALID rows carry their reason in parentheses."""
    if row.get("invalid"):
        return f"INVALID ({row['invalid']})"
    if row["gameplay"] is False or not row["frames"]:
        return "INVALID (no gameplay frames)"
    adapter = row["adapter"]
    name = adapter.get("name") if isinstance(adapter, dict) else None
    if not isinstance(name, str) or not name:
        return "INVALID (no adapter info)"
    if "nvidia" not in name.lower():
        return "INVALID (wrong adapter)"
    gpu, render, game = row["gpu_busy"], row["render_busy"], row["game_busy"]
    if gpu is None:
        return "MIXED"
    if gpu >= GPU_BOUND:
        return "GPU-BOUND"
    if gpu < GPU_LOW:
        if render is not None and render >= THREAD_BOUND:
            return "CPU-BOUND(render)"
        if game is not None and game >= THREAD_BOUND:
            return "CPU-BOUND(game)"
    return "MIXED"


def _blank(path, mode, knobs, reason):
    row = {"run": str(path), "mode": mode, "source": None, "adapter": None, "gameplay": None, "frames": 0, "interval_p50": None,
           "interval_p95": None, "eye0": None, "eye1": None, "eye1_share": None, "hook": None, "gpu_busy": None, "gpu_span": None,
           "phase_ms": None, "phase_lists": None, "mod_gpu": None, "render_busy": None, "game_busy": None, "user_knobs": knobs, "note": "", "invalid": reason}
    row["verdict"] = classify(row)
    return row


def _dict_of_nums(v):
    """A {name: number} block (gpu_phase_ms_p50, mod_gpu_ms_p50) or None; non-numeric entries drop the whole block."""
    if not isinstance(v, dict) or not v:
        return None
    return {k: x for k, x in v.items()} if all(_num(x) is not None for x in v.values()) else None


def _split_of(doc):
    fs = doc.get("frame_split") if isinstance(doc, dict) else None
    if isinstance(fs, dict) and isinstance(fs.get("split"), dict):
        return fs["split"], fs.get("adapter")
    return None, None


def load_run(path):
    """Never raises for a bad run: it becomes an INVALID row with the reason."""
    p = Path(path)
    run = p.parent if p.is_file() else p
    if p.is_file() and p.parent.name == "artifacts":
        run = p.parent.parent
    info = _run_info(run)
    mode = info.get("probe_mode") if info.get("probe_mode") in MODES else "?"
    knobs = info.get("user_knobs") if isinstance(info.get("user_knobs"), list) else None
    if p.is_file():
        candidates = [p]
    else:
        candidates = [p / "artifacts" / "xr-live-status.json", p / "artifacts" / "dxgi-probe-result.json"]
    failure = None
    if info.get("return_code") not in (None, 0) or info.get("reason"):
        failure = str(info.get("reason") or f"return code {info.get('return_code')}")[:100]
    seen_any = False
    for c in candidates:
        try:
            doc = _load(c)
        except OSError:
            continue
        except ValueError:
            seen_any = True
            continue
        seen_any = True
        split, adapter = _split_of(doc)
        if split is None:
            continue
        status = c.name == "xr-live-status.json" or (c.name != "dxgi-probe-result.json" and "gameplay" in doc)
        if adapter is None and (run / "adapter.txt").is_file():
            try:
                adapter = json.loads((run / "adapter.txt").read_text())
            except ValueError:
                adapter = None
        gameplay = bool(doc.get("gameplay")) if status else None
        row = {"run": str(path), "mode": mode, "source": c.name, "adapter": adapter if isinstance(adapter, dict) else None,
               "gameplay": gameplay, "frames": _num(split.get("frames")) or 0,
               "interval_p50": _num(split.get("interval_ms_p50")), "interval_p95": _num(split.get("interval_ms_p95")),
               "eye0": _num(split.get("eye0_ms_p50")), "eye1": _num(split.get("eye1_ms_p50")),
               "eye1_share": _num(split.get("eye1_share")), "hook": _num(split.get("hook_ms_p50")),
               "gpu_busy": _num(split.get("gpu_busy")), "gpu_span": _num(split.get("gpu_span")),
               "phase_ms": _dict_of_nums(split.get("gpu_phase_ms_p50")), "phase_lists": _dict_of_nums(split.get("gpu_phase_lists_p50")),
               "mod_gpu": _dict_of_nums(split.get("mod_gpu_ms_p50")), "render_busy": _num(split.get("render_thread_busy")),
               "game_busy": _num(split.get("game_thread_busy")), "user_knobs": knobs,
               "note": "" if status else "result file: no gameplay flag, every counted Present is included"}
        if row["gpu_busy"] is None and row["gameplay"] is not False and row["frames"]:
            row["note"] = (row["note"] + "; " if row["note"] else "") + "no gpu_busy (native-gpu-timing knob off?)"
        if failure:
            row["invalid"] = f"run failed: {failure}"
        if mode == "paired":
            row["note"] = (row["note"] + "; " if row["note"] else "") + "paired includes GPU-trace overhead"
        row["verdict"] = classify(row)
        return row
    if failure:
        return _blank(path, mode, knobs, f"run failed: {failure}")
    return _blank(path, mode, knobs, "unreadable status" if seen_any else "no status or result file")


def _f(v, spec=".2f"):
    return "n/a" if v is None else format(v, spec)


def format_phases(r):
    """Second line of a row: GPU ms (and list count) per frame phase, then the mod's own GPU spans. '' when the run has neither."""
    out = []
    if r.get("phase_ms"):
        lists = r.get("phase_lists") or {}
        parts = [f"{k} {_f(v)} ms ({int(lists[k]) if k in lists else '?'} lists)" for k, v in r["phase_ms"].items()]
        out.append("  gpu phases p50: " + " / ".join(parts) + f" / sum of p50s {_f(sum(r['phase_ms'].values()))} ms")
    if r.get("mod_gpu"):
        out.append("  mod gpu p50: " + " / ".join(f"{k} {_f(v, '.3f')}" for k, v in r["mod_gpu"].items()) + " ms")
    return "\n".join(out)


def format_row(r):
    name = r["adapter"].get("name") if isinstance(r["adapter"], dict) else None
    knobs = "unknown" if r["user_knobs"] is None else (", ".join(r["user_knobs"]) or "none")
    return (f"{Path(r['run']).name}: mode {r['mode']} / source {r['source'] or 'none'} / adapter {name if isinstance(name, str) else 'unknown'} / "
            f"interval p50 {_f(r['interval_p50'])} p95 {_f(r['interval_p95'])} ms / eye0 {_f(r['eye0'])} eye1 {_f(r['eye1'])} ms / "
            f"eye1_share {_f(r['eye1_share'])} / hook {_f(r['hook'])} ms / gpu_busy {_f(r['gpu_busy'])} span {_f(r['gpu_span'])} / "
            f"render {_f(r['render_busy'])} game {_f(r['game_busy'])} busy / build knobs: {knobs} / {r['verdict']}"
            + (f" [{r['note']}]" if r["note"] else ""))


def valid(rows):
    return [r for r in rows if not r["verdict"].startswith("INVALID")]


def overall(rows):
    ok = valid(rows)
    if not ok:
        return "INVALID"
    kinds = {r["verdict"] for r in ok}
    return kinds.pop() if len(kinds) == 1 else "MIXED"


METRICS = (("interval p50", "interval_p50", "ms"), ("interval p95", "interval_p95", "ms"), ("eye0", "eye0", "ms"),
           ("eye1", "eye1", "ms"), ("eye1_share", "eye1_share", ""), ("hook", "hook", "ms"), ("gpu_busy", "gpu_busy", ""))


def _mean(rows, key):
    vals = [r[key] for r in rows if r.get(key) is not None]
    return sum(vals) / len(vals) if vals else None


def format_delta(before, after):
    b, a = valid(before), valid(after)
    if not b or not a:
        return "delta vs before: n/a (no valid run on one side)"
    parts = []
    for label, key, unit in METRICS:
        mb, ma = _mean(b, key), _mean(a, key)
        parts.append(f"{label} n/a" if mb is None or ma is None else f"{label} {ma - mb:+.2f}{unit}")
    kb = {tuple(r.get("user_knobs") or ()) for r in b} | {tuple(r.get("user_knobs") or ()) for r in a}
    warn = "" if len(kb) == 1 else " [WARNING: the runs had different knob files in build/; not a like-for-like comparison]"
    return f"delta vs before ({len(b)} valid run(s) -> {len(a)}): " + " / ".join(parts) + warn


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("runs", nargs="+")
    ap.add_argument("--before")
    ap.add_argument("--json", dest="json_out")
    a = ap.parse_args(argv)
    try:
        before = None
        if a.before:
            before = _load(a.before)["rows"]
            if not isinstance(before, list) or not all(isinstance(r, dict) and isinstance(r.get("verdict"), str) for r in before):
                raise ValueError(f"{a.before}: not a perf_bound_report --json file")
    except (OSError, ValueError, KeyError, TypeError) as e:
        print(f"perf_bound_report: --before: {e}", file=sys.stderr)
        return 1
    rows = [load_run(r) for r in a.runs]
    for r in rows:
        print(format_row(r))
        extra = format_phases(r)
        if extra:
            print(extra)
    print(f"VERDICT: {overall(rows)}")
    if before is not None:
        print(format_delta(before, rows))
    if a.json_out:
        try:
            Path(a.json_out).write_text(json.dumps({"rows": rows}, indent=1) + "\n")
        except OSError as e:
            print(f"perf_bound_report: {e}", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
