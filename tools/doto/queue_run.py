"""Wait until tools/doto/doto_sim.py's safety checks pass (no game running, no game in front, the PC idle for
--idle-minutes), then run doto_sim.py with the remaining arguments. Gives up after --max-wait minutes.

    python tools/doto/queue_run.py --max-wait 60 -- --name boot2 --dll build/doto/d2vr_render_probe_arm_v145705.dll ...
"""
import argparse
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))
import doto_sim as d  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--max-wait", type=float, default=60)
    ap.add_argument("--idle-minutes", type=float, default=15)
    ap.add_argument("rest", nargs=argparse.REMAINDER)
    a = ap.parse_args()
    rest = a.rest[1:] if a.rest[:1] == ["--"] else a.rest
    end = time.time() + a.max_wait * 60
    c = d.conflicts(a.idle_minutes)
    while c and time.time() < end:
        time.sleep(20)
        c = d.conflicts(a.idle_minutes)
    if c:
        print("gave up waiting:", "; ".join(c))
        return 1
    print(time.strftime("%H:%M:%S"), "PC free: running", " ".join(rest), flush=True)
    return subprocess.call([sys.executable, str(HERE / "doto_sim.py"), "--idle-minutes", str(a.idle_minutes), *rest])


if __name__ == "__main__":
    sys.exit(main())
