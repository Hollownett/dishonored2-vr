#!/usr/bin/env python3
"""One-line performance report from xr-live-status.json ("perf" block), optionally compared with an earlier report.

usage: perf_run_report.py STATUS.json [xr-frame-timing.csv] [--before PREVIOUS_STATUS.json] [--json]
Line: p50 / p95 / missed% / mod cpu / fraction. The optional CSV (xr-frame-timing.csv) is only used to report the
frame count, as a cross-check of the in-process window.
"""
import argparse
import json
import sys


def load_perf(path):
    with open(path, encoding="utf-8") as f:
        doc = json.load(f)
    perf = doc.get("perf") if isinstance(doc, dict) and "perf" in doc else doc
    need = ("pace_ms_p50", "pace_ms_p95", "missed_fraction", "mod_cpu_ms_mean", "mod_cpu_ms_p95", "fraction")
    if not isinstance(perf, dict) or any(k not in perf for k in need):
        raise ValueError(f"{path}: no perf block")
    return perf


def format_line(p):
    return (f"p50 {p['pace_ms_p50']:.2f} ms / p95 {p['pace_ms_p95']:.2f} ms / missed {p['missed_fraction'] * 100:.1f}% / "
            f"mod cpu {p['mod_cpu_ms_mean']:.2f} ms (p95 {p['mod_cpu_ms_p95']:.2f}) / fraction {p['fraction']:.2f}")


def format_delta(before, after):
    rows = (("p50", "pace_ms_p50", 1, "ms"), ("p95", "pace_ms_p95", 1, "ms"),
            ("missed", "missed_fraction", 100, "%"), ("mod cpu", "mod_cpu_ms_mean", 1, "ms"),
            ("fraction", "fraction", 1, ""))
    parts = []
    for label, key, scale, unit in rows:
        d = (after[key] - before[key]) * scale
        parts.append(f"{label} {d:+.2f}{unit}")
    return "delta vs before: " + " / ".join(parts)


def csv_frames(path):
    with open(path, encoding="utf-8") as f:
        return max(0, sum(1 for _ in f) - 1)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("status")
    ap.add_argument("csv", nargs="?")
    ap.add_argument("--before")
    a = ap.parse_args(argv)
    try:
        after = load_perf(a.status)
        print(format_line(after))
        if a.csv:
            print(f"frames in csv: {csv_frames(a.csv)} (window frames: {after.get('frames', '?')})")
        if a.before:
            print(format_delta(load_perf(a.before), after))
    except (OSError, ValueError) as e:
        print(f"perf_run_report: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
