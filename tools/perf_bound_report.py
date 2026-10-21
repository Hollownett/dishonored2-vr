#!/usr/bin/env python3
"""Is the game CPU-bound or GPU-bound? One row per sim-rig run, then a verdict line.

usage: perf_bound_report.py RUN [RUN ...] [--before PREVIOUS.json] [--json OUT.json] [--any-adapter]
RUN is a sim run folder (artifacts/xr-live-status.json inside it, plus run-info.json and adapter.txt when present) or an
xr-live-status.json. Reads the "frame_split" and "adapter" status blocks (plan E task 1).
Verdict per run: GPU-BOUND when gpu_busy >= .90; CPU-BOUND(render) or CPU-BOUND(game) when gpu_busy < .70 and that thread's
busy fraction is >= .85 (render first); otherwise MIXED. INVALID runs (no gameplay, no frames, wrong adapter) are listed but
never averaged in. This laptop must run on the NVIDIA GPU, so an adapter without "NVIDIA" in its name is INVALID unless
--any-adapter. --json writes the rows; a later run passes that file as --before to print the delta.
"""
import argparse
import json
import sys
from pathlib import Path

GPU_BOUND = 0.90
GPU_LOW = 0.70
THREAD_BOUND = 0.85
MODES = {6: "xr", 3: "paired", 2: "single"}


def _load(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def _mode(run):
    try:
        override = _load(run / "run-info.json").get("override", "")
        return MODES.get(int(override.split()[2]), "?")
    except (OSError, ValueError, IndexError, AttributeError, TypeError):
        return "?"


def classify(row, any_adapter=False):
    """-> verdict string for one row dict (keys as in load_run)."""
    if not row["gameplay"] or not row["frames"]:
        return "INVALID (no gameplay frames)"
    name = row["adapter"].get("name") if isinstance(row["adapter"], dict) else None
    if not any_adapter:
        if not name:
            return "INVALID (no adapter info)"
        if "nvidia" not in name.lower():
            return "INVALID (wrong adapter)"
    gpu, render, game = row["gpu_busy"], row["render_busy"], row["game_busy"]
    if gpu is None:
        return "MIXED (no gpu_busy)"
    if gpu >= GPU_BOUND:
        return "GPU-BOUND"
    if gpu < GPU_LOW:
        if render is not None and render >= THREAD_BOUND:
            return "CPU-BOUND(render)"
        if game is not None and game >= THREAD_BOUND:
            return "CPU-BOUND(game)"
    return "MIXED"


def load_run(path, any_adapter=False):
    run = Path(path)
    status = run if run.is_file() else run / "artifacts" / "xr-live-status.json"
    if run.is_file():
        run = run.parent
    doc = _load(status)
    fs = doc.get("frame_split")
    if not isinstance(fs, dict):
        raise ValueError(f"{status}: no frame_split block")
    adapter = doc.get("adapter")
    if adapter is None and (run / "adapter.txt").is_file():
        try:
            adapter = json.loads((run / "adapter.txt").read_text())
        except ValueError:
            adapter = None
    row = {"run": str(path), "mode": _mode(run), "adapter": adapter, "gameplay": bool(doc.get("gameplay")),
           "frames": fs.get("frames") or 0, "interval_p50": fs.get("interval_ms_p50"), "interval_p95": fs.get("interval_ms_p95"),
           "eye0": fs.get("eye0_ms_p50"), "eye1": fs.get("eye1_ms_p50"), "eye1_share": fs.get("eye1_share"),
           "hook": fs.get("hook_ms_p50"), "gpu_busy": fs.get("gpu_busy"), "render_busy": fs.get("render_thread_busy"),
           "game_busy": fs.get("game_thread_busy")}
    row["verdict"] = classify(row, any_adapter)
    return row


def _f(v, spec=".2f"):
    return "n/a" if v is None else format(v, spec)


def format_row(r):
    name = r["adapter"].get("name") if isinstance(r["adapter"], dict) else None
    return (f"{Path(r['run']).name}: mode {r['mode']} / adapter {name or 'unknown'} / interval p50 {_f(r['interval_p50'])} "
            f"p95 {_f(r['interval_p95'])} ms / eye0 {_f(r['eye0'])} eye1 {_f(r['eye1'])} ms / eye1_share {_f(r['eye1_share'])} / "
            f"hook {_f(r['hook'])} ms / gpu_busy {_f(r['gpu_busy'])} / render {_f(r['render_busy'])} game {_f(r['game_busy'])} busy / {r['verdict']}")


def valid(rows):
    return [r for r in rows if not r["verdict"].startswith("INVALID")]


def overall(rows):
    ok = valid(rows)
    if not ok:
        return "INVALID (no usable run)"
    kinds = {r["verdict"] for r in ok}
    return kinds.pop() if len(kinds) == 1 else "MIXED"


METRICS = (("interval p50", "interval_p50", "ms"), ("interval p95", "interval_p95", "ms"), ("eye0", "eye0", "ms"),
           ("eye1", "eye1", "ms"), ("eye1_share", "eye1_share", ""), ("hook", "hook", "ms"), ("gpu_busy", "gpu_busy", ""))


def _mean(rows, key):
    vals = [r[key] for r in rows if r[key] is not None]
    return sum(vals) / len(vals) if vals else None


def format_delta(before, after):
    b, a = valid(before), valid(after)
    if not b or not a:
        return "delta vs before: n/a (no valid run on one side)"
    parts = []
    for label, key, unit in METRICS:
        mb, ma = _mean(b, key), _mean(a, key)
        parts.append(f"{label} n/a" if mb is None or ma is None else f"{label} {ma - mb:+.2f}{unit}")
    return f"delta vs before ({len(b)} valid run(s) -> {len(a)}): " + " / ".join(parts)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("runs", nargs="+")
    ap.add_argument("--before")
    ap.add_argument("--json", dest="json_out")
    ap.add_argument("--any-adapter", action="store_true", help="accept a non-NVIDIA adapter (not for this laptop)")
    a = ap.parse_args(argv)
    try:
        rows = [load_run(r, a.any_adapter) for r in a.runs]
        before = _load(a.before)["rows"] if a.before else None
        for r in rows:
            print(format_row(r))
        print(f"VERDICT: {overall(rows)}")
        if before is not None:
            print(format_delta(before, rows))
        if a.json_out:
            Path(a.json_out).write_text(json.dumps({"rows": rows}, indent=1) + "\n")
    except (OSError, ValueError, KeyError, TypeError) as e:
        print(f"perf_bound_report: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
