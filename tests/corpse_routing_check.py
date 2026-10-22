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


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    check((root / "src" / "dxgi_probe.cpp").read_text(encoding="utf-8"))
    check_hold((root / "src" / "native_grab.hpp").read_text(encoding="utf-8"), (root / "src" / "dxgi_probe.cpp").read_text(encoding="utf-8"))
    check_wake((root / "src" / "native_weapon_hands.hpp").read_text(encoding="utf-8"), (root / "src" / "dxgi_probe.cpp").read_text(encoding="utf-8"))
    print("corpse_routing_check ok")
    sys.exit(0)
