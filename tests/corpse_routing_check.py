"""Corpse grab routing guards. Grows in Tasks 10-12; Task 5 pins the diagnostics wiring in src/dxgi_probe.cpp, Task 10 the capped hold in src/native_grab.hpp."""
from pathlib import Path
import re
import sys


def check(source: str) -> None:
    status = source.index('\\"corpse\\":')
    carry = source.index('\\"carry\\":')
    assert status < carry, '"corpse": must precede "carry": in the status line'
    assert "corpse_diagnostics.json()" in source[status - 200:status + 200]
    carry_stops = [m.start() for m in re.finditer(r"carry_trace\.stop\(\)", source)]
    assert len(carry_stops) >= 5, "expected the known carry_trace.stop() sites"
    for pos in carry_stops:
        line_start = source.rfind("\n", 0, pos) + 1
        line_end = source.index("\n", pos)
        assert "corpse_trace.stop()" in source[line_start:line_end], "corpse_trace.stop() must sit with every carry_trace.stop()"
    assert source.count("corpse_trace.stop()") >= len(carry_stops)
    assert 'L"bonelab-trace"' in source and "native-corpse-trace.csv" in source


def check_hold(source: str, probe: str) -> None:
    """Task 10: the capped corpse/prop hold writes only through NativeGrab::write(, the fault latch trips in the fault branches, and the
    runaway lockout is consulted before the capped motor runs."""
    assert "NativeGrab native_grab" in probe and "corpse_diagnostics=native_grab.corpse_diagnostics()" in probe, "the status block must be fed by NativeGrab"
    tick = source[source.index("bool capped_tick("):source.index("void update_lane(")]
    assert "velocity_(" not in tick and "write_velocity(" not in tick, "capped_tick must write through write( only"
    assert tick.count("write(lane,side,body,") == 3, "the prop release, the prop release-by-motor and the capped write are the only writes"
    assert source.count("FaultLatch::trip()") == 1, "the latch trips in one place"
    trip = source.index("void trip_fault_latch()")
    assert "FaultLatch::trip()" in source[trip:trip + 120]
    assert source.count("trip_fault_latch()") >= 3
    lock = tick.index("lockout_.locked(body.entity_id,now_s)")
    motor = tick.index("lane.grab.corpse_motor(")
    assert lock < motor, "EntityLockout must be consulted before corpse_motor"
    assert tick.index("FaultLatch::tripped()") < lock
    assert tick.index("0xd1") < tick.index("RunawayDetector::check(") < motor
    assert "lane.grab.motor(body->pose" in source, "the living-guard yank keeps the old motor"
    assert source.count("load_grab_tuning(") == 1 and "grab-tuning.cfg" in source and "KillSwitches::read(" in source


def check_wake(hands: str, probe: str) -> None:
    """Task 11: START_RAGDOLL posts go through the budget; the refusal pulses the hand; the kill switch reaches the budget and the legacy yank."""
    assert "post_budget.request(*id,false" in hands and "service_wake_retries(base,read,clock_ms())" in hands
    wake = hands[hands.index("static bool wake("):hands.index("static void refuse_wake(")]
    assert wake.count("post(base,read,0xb9") == 1, "wake posts once, after the budget decision"
    assert wake.index("post_budget.request") < wake.index("post(base,read,0xb9")
    assert "if(post_budget.disabled())" in hands[hands.index("static bool ragdoll("):hands.index("static bool ragdoll(") + 200]
    assert "if(post_budget.disabled()) return;" in hands[hands.index("static void hold_down("):hands.index("static void hold_down(") + 120]
    assert hands.count("0x149") == 2, "no new VELOCITY_IMPULSE use (the comment and the legacy yank's own post)"
    assert "take_wake_refusal(h)) game_xr->pulse(h,.6f,.08f)" in probe
    assert "post_budget.disable(kill.no_posts)" in probe and "wake_lockout=&native_grab.lockout()" in probe


def check_drawn(probe: str, grab: str, safety: str) -> None:
    """Task 12: the drawn hand is moved after the clamp and its note_drawn, once per frame (inside the per_frame block), before publish_hand; it reads only
    the hold_anchor snapshot and never the raw controller; build/no-prop-grasp-point is read with the other knobs."""
    block = probe[probe.index("if(render_world_scene() && (tracked_arms_requested || native_weapon_attachment_requested) && per_frame) {"):]
    seat = block.index("drawn_anchor.update(")
    assert block.index("hand_clamp.update(") < block.index("hand_clamp.note_drawn(") < seat, "the override sits after the clamp and note_drawn"
    publish = block.index("publish_hand(side,hands.grips[side],raw_grips[side])")
    assert seat < publish, "the override precedes publish_hand"
    region = block[block.rindex("Task 12 (user decision 6)", 0, seat):publish]
    assert "native_grab.hold_anchor(side)" in region and "raw_grips" not in region, "the override reads only hold_anchor and never raw_grips"
    assert "held_view.capped" in region and "native_grab.prop_grasp_point()" in region and "held_view.corpse ||" in region, "corpses always anchor; props follow the knob"
    assert "drawn_anchor.reset(side)" in region
    assert "note_anchor_gap(" in region and "prop_seat_gap_m" in region
    assert '\\"prop_grasp_point\\":' in probe and '\\"prop_seat_gap_m\\":' in probe, "status fields"
    assert "prop_grasp_point_=!kill.no_prop_grasp_point" in grab and "knob build/no-prop-grasp-point present" in grab, "the knob is read at install and logged"
    assert 'build_dir/"no-prop-grasp-point"' in safety
    assert 'build_dir/"legacy-corpse-lag-release"' in safety and "set_legacy_lag_release(kill.legacy_corpse_lag_release)" in grab, "fix round 1: the lag-release knob is read at install"
    assert "native_grab.holding(side)" in probe[probe.index("clamp_yields(side"):probe.index("clamp_yields(side") + 200], "native holds still yield the clamp"
    assert "gate.grab_held=physical_grab_requested && (native_grab.holding(0) || native_grab.holding(1))" in probe, "a held corpse still keeps the hands busy for the attack gate"


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    check((root / "src" / "dxgi_probe.cpp").read_text(encoding="utf-8"))
    check_hold((root / "src" / "native_grab.hpp").read_text(encoding="utf-8"), (root / "src" / "dxgi_probe.cpp").read_text(encoding="utf-8"))
    check_wake((root / "src" / "native_weapon_hands.hpp").read_text(encoding="utf-8"), (root / "src" / "dxgi_probe.cpp").read_text(encoding="utf-8"))
    check_drawn((root / "src" / "dxgi_probe.cpp").read_text(encoding="utf-8"), (root / "src" / "native_grab.hpp").read_text(encoding="utf-8"), (root / "src" / "ragdoll_safety.hpp").read_text(encoding="utf-8"))
    print("corpse_routing_check ok")
    sys.exit(0)
