"""Summarise one or more tools/win_perf_sim.py run folders (artifacts/perf-night/<run>).

For each run: settings, frame work per gameplay frame (present-to-present minus xrWaitFrame and pacing sleep), the CPU
frame split, the per-pass GPU table (build/native-gpu-timing), the mod's GPU spans and, when present, the thread sample.

    python tools/perf_night_report.py artifacts/perf-night/r1-baseline [more runs ...]
"""
import csv
import json
import sys
from pathlib import Path


def pct(values, p):
    v = sorted(values)
    return v[min(len(v) - 1, int(p * len(v)))] if v else float("nan")


def frame_work(run):
    path = run / "xr-frame-timing.csv"
    if not path.is_file():
        return None
    rows = [r for r in csv.DictReader(open(path, encoding="utf-8", errors="replace")) if r.get("gameplay") == "1" and r.get("submitted") == "1"]
    if len(rows) < 30:
        return {"frames": len(rows)}
    rows = rows[len(rows) // 10:]  # drop the first 10 % (load settling)
    work, interval, gap, prexr, lists, fsr = [], [], [], [], [], []
    for a, b in zip(rows, rows[1:]):
        if int(b["present"]) != int(a["present"]) + 1:
            continue
        d = (int(b["start_cpu_ns"]) - int(a["start_cpu_ns"])) / 1e6
        interval.append(d)
        work.append(d - int(b["wait_cpu_ns"]) / 1e6)
        gap.append(int(b["engine_cpu_gap_ns"]) / 1e6)
        prexr.append(int(b["pre_xr_cpu_ns"]) / 1e6)
        if b.get("native_gpu_list_sum_ns") not in (None, "", "0"):
            lists.append(int(b["native_gpu_list_sum_ns"]) / 1e6)
        f = sum(int(b.get(c) or 0) for c in ("fsr_gpu_left_ns", "fsr_gpu_right_ns")) / 1e6
        if f:
            fsr.append(f)
    last = rows[-1]
    return {"frames": len(rows), "size": f"{last['width']}x{last['height']}", "aa": last["temporal_aa"],
            "fraction": int(last["render_fraction_permille"]) / 1000, "render": f"{last['fsr_render_left_width']}x{last['fsr_render_left_height']}",
            "work_p50": pct(work, .5), "work_p90": pct(work, .9), "interval_p50": pct(interval, .5),
            "engine_gap_p50": pct(gap, .5), "pre_xr_p50": pct(prexr, .5),
            "gpu_lists_p50": pct(lists, .5) if lists else None, "fsr_gpu_p50": pct(fsr, .5) if fsr else None}


def summary(run):
    run = Path(run)
    print(f"=== {run.name}")
    try:
        args = json.loads((run / "run-args.json").read_text())
        print("args:", {k: args[k] for k in ("scenario", "resolution", "eye_size", "aa_mode", "knob", "disable") if k in args})
    except OSError:
        pass
    log = run / "runner.log"
    if log.is_file():
        for line in log.read_text().splitlines():
            if any(w in line for w in ("ERROR", "session over", "saves", "knobs cleaned", "INCOMPLETE")):
                print("runner:", line)
    w = frame_work(run)
    if w:
        print("frames:", {k: (round(v, 2) if isinstance(v, float) else v) for k, v in w.items()})
    try:
        st = json.loads((run / "xr-live-status.json").read_text(encoding="utf-8", errors="replace"))
    except (OSError, ValueError):
        st = None
    if st:
        fs = (st.get("frame_split") or {}).get("split") or {}
        print("frame_split:", {k: (round(v, 3) if isinstance(v, float) else v) for k, v in fs.items() if k not in ("gpu_phase_lists_p50",)})
        print("perf:", st.get("perf"), "| pacer:", {k: st.get("pacer", {}).get(k) for k in ("k", "locks", "work_ms_p50")})
        print("shadow_share frames:", (st.get("shadow_share") or {}).get("frames_shared"), "| alternate_eye:", (st.get("alternate_eye") or {}).get("state"),
              "| mono_view:", st.get("mono_view"))
        gp = st.get("gpu_passes")
        if isinstance(gp, dict) and gp.get("passes"):
            total = sum(p["total_ms"] for p in gp["passes"])
            print(f"gpu_passes: frames {gp.get('frames')} tagged total {total:.2f} ms, untagged {gp['untagged']['total_ms']:.2f} ms")
            for p in gp["passes"][:22]:
                print(f"   {p['name'].replace('ARK_RENDERPASS_', ''):<44} {p['total_ms']:6.2f}  L {p['eye0_ms_p50']:5.2f}  R {p['eye1_ms_p50']:5.2f}  lists {p['lists_p50']}")
            print("   mod_gpu_ms_p50:", gp.get("mod_gpu_ms_p50"))
        else:
            print("gpu_passes:", "absent" if gp is None else str(gp)[:200])
    t = run / "threads.txt"
    if t.is_file():
        print("--- threads (head)")
        print("\n".join(t.read_text(errors="replace").splitlines()[:45]))
    print()


if __name__ == "__main__":
    for r in sys.argv[1:]:
        summary(r)
