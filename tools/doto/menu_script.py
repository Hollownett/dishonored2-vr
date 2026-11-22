"""Write a menu-driving gameplay script into the DOTO home for the next session of a running game (tools/doto/keys.py start).

The mod's simulated controller presses A on menus every --every seconds for --seconds (menu-only segments: inert in gameplay),
optionally with left-stick nudges first (e.g. --down 1 moves the menu cursor down once before the first A).

    python tools/doto/menu_script.py --seconds 110 --every 3
"""
import argparse
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))
import doto_sim as d  # noqa: E402
import win_perf_sim as W2  # noqa: E402

NEUTRAL = {"move_x": 0.0}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seconds", type=int, default=110)
    ap.add_argument("--every", type=float, default=3.0)
    ap.add_argument("--first", type=float, default=2.0)
    ap.add_argument("--down", type=int, default=0, help="left-stick down nudges before the first A")
    a = ap.parse_args()
    segs, t = [], 0
    end = (a.seconds - 6) * 1000

    def seg(start, stop, inp, when="menu"):
        segs.append({"start_ms": int(start), "end_ms": int(stop), "input": inp, "when": when})
    cursor = int(a.first * 1000)
    seg(0, cursor, dict(NEUTRAL), "always")
    for _ in range(a.down):
        seg(cursor, cursor + 200, {"move_y": -1.0})
        seg(cursor + 200, cursor + 700, dict(NEUTRAL))
        cursor += 700
    while cursor + 200 < end:
        seg(cursor, cursor + 200, {"a": True})
        nxt = min(end, cursor + int(a.every * 1000))
        seg(cursor + 200, nxt, dict(NEUTRAL))
        cursor = nxt
    if cursor < end:
        seg(cursor, end, dict(NEUTRAL), "always")
    sgs = W2.load("d2vr_simulated_gameplay_script", d.ROOT / "tools" / "simulated_gameplay_script.py")
    tmp = d.ROOT / "artifacts" / "doto-runs" / "_menu_script.json"
    tmp.write_text(json.dumps({"segments": segs}, indent=1) + "\n", encoding="utf-8")
    script_json, script_cfg = sgs.compile_script(tmp, tmp.parent, a.seconds)
    (d.BUILD / W2.SCRIPT_CFG).write_text(script_cfg, encoding="utf-8")
    (d.ART / W2.SCRIPT_JSON).write_text(script_json, encoding="utf-8")
    print(f"menu script: {len(segs)} segments over {a.seconds} s")


if __name__ == "__main__":
    main()
