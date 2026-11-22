"""One-screen summary of a DOTO simulated run folder (artifacts/doto-runs/<name>).

    python tools/doto/run_summary.py artifacts/doto-runs/boot2
"""
import json
import re
import sys
from pathlib import Path

KEYS = ("gameplay", "logic_mask", "session_state", "views_valid", "tracked_render_pair", "game_layers_submitted",
        "dropped_layers", "width", "height", "native_controller_reads", "native_joystick_focus_calls",
        "native_pad_window_overrides", "tracked_gameplay_transfer_frames", "authored_camera_layers", "end_frame_rejections")


def load(p):
    try:
        return json.loads(p.read_bytes().decode("utf-8-sig", errors="replace"))
    except (OSError, ValueError):
        return None


def main():
    run = Path(sys.argv[1])
    print(f"== {run}")
    rl = run / "runner.log"
    if rl.exists():
        print("".join(rl.read_text(encoding="utf-8", errors="replace").splitlines(True)[-8:]))
    pl = run / "linux-proxy.log"
    if pl.exists():
        lines = pl.read_text(encoding="utf-8", errors="replace").splitlines()
        for ln in lines:
            if re.search(r"StartProbe|present seen|start \(|ProbeStatus|cannot|error|refus", ln, re.I):
                print("proxy:", ln[24:200])
    for name in ("dxgi-probe-failure.txt", "native-exception-context.txt", "xr-recoveries.log"):
        p = run / name
        if p.exists() and p.stat().st_size:
            print(f"-- {name}:\n" + p.read_text(encoding="utf-8", errors="replace")[:1500])
    st = load(run / "xr-live-status.json")
    if st:
        print("-- xr-live-status:", {k: st.get(k) for k in KEYS if k in st})
        notes = st.get("recoverable_notes") or st.get("notes")
        if notes:
            print("   notes:", str(notes)[:1200])
    res = load(run / "dxgi-probe-result.json")
    if res:
        interesting = {k: v for k, v in res.items() if isinstance(v, (bool, int, float, str)) and
                       re.search(r"fail|error|install|hook|stereo|verified|reason|enabled|active", k, re.I)}
        print("-- dxgi-probe-result (selected):", json.dumps(interesting)[:2500])
    shots = sorted(run.glob("shot-*.png"))
    print(f"-- {len(shots)} window shots" + (f": last {shots[-1].name}" if shots else ""))


if __name__ == "__main__":
    main()
