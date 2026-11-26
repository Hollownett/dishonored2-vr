"""The other forearm is solid (2026-11-26, final review I1: forearm contact had no sim evidence). Both games.

The left forearm is held across the chest (the left hand 30 cm to the right of its rest, so the forearm runs from the left elbow to in front of
the sternum); the right hand passes down through it at 0.1 m/s (from 15 cm above it to 35 cm below), holds there, comes back up, then sweeps
sideways across it at its height, right to left and back. Captures (`d2vr spacewarp capture 2`) with the right hand pressed on the forearm.
Pass (xr-live-status.json arm_collision.forearm_contact): frames > 0 and max_mm (the correction that kept the hand out of the forearm) > 0
with the contact on; frames 0 with --knob no-forearm-contact (the A/B: the hand passes through). The hand is checked against the latched arm
joints, so frames should accrue every frame of the contact (no on/off flicker: compare frames with the contact's duration).
`D2VR_SPOT=doto tools/linux/sim_serial.sh LOG --game doto sim --scenario forearm_block --settle 45 --lead 54 --knob diagnostics --candidate-dll ...`
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
L = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(L)

# offsets from the simulated default hands (left (-0.2, 1.3, -0.4), right (0.2, 1.3, -0.4)); x right, y up, z backward
LEFT_ACROSS = [0.30, 0.0, 0.0]          # the left wrist in front of the sternum: the forearm runs across the chest
ABOVE = [-0.20, 0.15, 0.10]             # the right hand 15 cm above the forearm's middle
BELOW = [-0.20, -0.35, 0.10]            # 35 cm below it: the straight path passes through the forearm
SIDE_R = [0.05, -0.08, 0.10]            # at the forearm's height, right of it
SIDE_L = [-0.45, -0.08, 0.10]           # left of it: the path crosses the forearm
CAP = "d2vr spacewarp capture 2"
STEP_MS = 100


def build(lead_ms):
    segments = L.lead_in(lead_ms)
    t = lead_ms

    def hold(ms, right, cons=None):
        nonlocal t
        s = {"start_ms": t, "end_ms": t + ms, "head_pitch_degrees": -30, "input": dict(L.NEUTRAL),
             "left_hand_offset": list(LEFT_ACROSS), "right_hand_offset": list(right)}
        if cons:
            segments.append(dict(s, end_ms=t + L.FRAME_MS, console=cons))
            s["start_ms"] = t + L.FRAME_MS
        segments.append(s)
        t += ms

    def glide(ms, a, b):
        steps = max(1, ms // STEP_MS)
        for i in range(steps):
            hold(ms // steps, L.lerp(a, b, (i + 1) / steps))

    hold(1500, [0.0, 0.0, 0.0])
    glide(1000, [0.0, 0.0, 0.0], ABOVE)
    hold(800, ABOVE)
    for _ in range(2):                       # down through the forearm at 0.1 m/s, a capture pressed on it, back up
        glide(2500, ABOVE, BELOW)
        hold(600, BELOW)
        hold(800, BELOW, cons=CAP)
        glide(2500, BELOW, ABOVE)
        hold(600, ABOVE)
    glide(800, ABOVE, SIDE_R)
    hold(600, SIDE_R)
    for _ in range(2):                       # sideways across it at its height
        glide(2500, SIDE_R, SIDE_L)
        hold(800, SIDE_L, cons=CAP)
        glide(2500, SIDE_L, SIDE_R)
        hold(600, SIDE_R)
    glide(1000, SIDE_R, [0.0, 0.0, 0.0])
    hold(1500, [0.0, 0.0, 0.0])
    return segments
