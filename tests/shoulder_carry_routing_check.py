"""Shoulder carry (v1019) routing guards.

A corpse put on the shoulder is handed to the game's own body carry (arkPlayerStateCarryCorpse): the render thread docks it where the
hand let go and publishes the placement, the input thread presses the game's Use while that hand's native target is still the body,
and the game's carry owns it from then on. These guards pin that wiring: the press is only ever the game's Use, never while a body is
carried (a Use press is the game's drop) and never for a body whose Loot action is offered (the press would loot it), the v637 auto-drop
stands aside only for the carry the placement started, and the dock yields as soon as the game takes the body.
"""
from pathlib import Path
import sys


def check(probe: str, policy: str, decision: str) -> None:
    assert '#include "shoulder_carry.hpp"' in probe, "dxgi_probe must include the handoff policy"
    # The policy header is pure and states its own order: a carried body and a Loot offer are decided before any press.
    assert "if(in.carrying)" in policy and "if(in.loot)" in policy
    assert policy.index("if(in.carrying)") < policy.index("if(in.loot)") < policy.index("out.press=true"), "carry and Loot gates precede the press"
    assert "shoulder_carry_window_ms" in policy and "shoulder_carry_retry_ms" in policy and "shoulder_carry_max_presses" in policy
    # Render thread: the dock reads the game's carry and yields to it, and publishes the placement the input thread presses for.
    assert "in.native_carry=carrying_native" in probe, "the dock must see the game's own carry"
    assert "if(dock.take)" in probe and "shoulder_place_body=in.entity;shoulder_place_hand=side;" in probe
    assert "shoulder_place_tick=grab_now" in probe
    # Input thread: the trigger, its held Use, the started latch, and the auto-drop standing aside for that carry only.
    assert "shoulder_carry_trigger.update(ci)" in probe
    assert "shoulder_press_hold=co.press;" in probe, "the game's carry is its held Use on the body, not a tap"
    assert "if(co.press) shoulder_press_at=now_c;" in probe and "if(co.started) ++shoulder_started;" in probe
    assert "if(carrying && shoulder_press_at && now_c>=shoulder_press_at && now_c-shoulder_press_at<=3000) shoulder_native_carry=true;" in probe, \
        "the carry is the placement's when it appears within 3 s of the held Use (v1022: the v1019 latch missed most carries)"
    assert "legacy_corpse_auto_drop=fs::exists(root/L\"build\"/L\"legacy-corpse-auto-drop\");" in probe
    auto_drop = probe[probe.index("if(legacy_corpse_auto_drop && carrying"):]
    assert "++native_carry_auto_drops" in auto_drop[:400], "the v637 auto-drop is opt-in now (it dropped every placed carry in v1019)"
    assert "legacy_corpse_carry=fs::exists(" in probe[:probe.index("shoulder_carry_on=!fs::exists(")], "the legacy knob must be read before the shoulder knobs use it"
    # The press is the game's Use, and only that press skips the v637 body guard. It also holds the interaction query for the
    # placing hand (the game's Use must act on that hand's body, not on whatever the other hand points at).
    assert "const auto shoulder_claim=[&](unsigned h) {return shoulder_body && shoulder_at && shoulder_press_hold && shoulder_place_hand.load()==h &&" in probe
    assert "controls_now-shoulder_at<d2vr::shoulder_carry_window_ms" in probe, "the query claim is bounded by the handoff window"
    assert "shoulder_claim(h)" in probe and "native_lock_hand==static_cast<int>(shoulder_hand)" in probe
    assert "const bool shoulder_press=shoulder_press_hold && shoulder_hand<2 && shoulder_body_now && shoulder_at_now &&" in probe
    assert "use_now-shoulder_at_now<d2vr::shoulder_carry_window_ms" in probe, "a stale placement can never keep Use held"
    assert "if(shoulder_press) input.x=true;" in probe
    guard_at = probe.index("if(input.x && !legacy_corpse_carry && !shoulder_press) {")
    assert "if(!carrying && carry_body && !loot_body) {input.x=false;++body_use_blocked;}" in probe[guard_at:guard_at + 1200]
    assert probe.index("if(shoulder_press) input.x=true;") < guard_at, "the handoff press is set before the guard that skips it"
    # The trigger reads the native Use target and the Loot offer of the placing hand, never a guess.
    assert "native_interaction_aim.target_owner(static_cast<int>(hand),now_c)" in probe
    assert "native_interaction_aim.loot_offered(body,now_c)" in probe
    assert "native_interaction_aim.loot_offer_state(static_cast<int>(hand))" in probe, "the status must show what the engine offers on the body"
    # Knobs: on by default, the whole feature or only the native handoff can be turned off.
    assert 'L"build"/L"no-shoulder-carry"' in probe and 'L"build"/L"no-shoulder-native"' in probe
    assert "shoulder_dock_enabled=shoulder_carry_on &&" in probe, "the shoulder zone is the trigger point and is on by default now"
    assert "shoulder_native_on=shoulder_carry_on &&" in probe
    # Status fields the next headset session reads.
    for field in ('\\"shoulder_carry\\":', '\\"placed\\":', '\\"presses\\":', '\\"started\\":', '\\"loot\\":', '\\"failed\\":', '\\"takeoffs\\":', '\\"offer\\":'):
        assert field in probe, field
    # The dock's own squeeze is never the take: the game's carry is.
    assert "native_carry{}" in decision and "o.take=true" in decision
    # v1038: a carried body takes the right arm only. The game's CarryCorpse locks and empties the left hand's equipment too (no powers, no
    # weapons); the EquipChange hook takes that request out and the mod's left-hand state stops treating the left slot as gone.
    runtime = (Path(__file__).resolve().parents[1] / "src" / "native_manual_equip_runtime.hpp").read_text(encoding="utf-8")
    assert "carry_left_equip_code(code,requester,item!=0)" in runtime and "self.keep_left_hand_in_carry(*machine,state,params);" in runtime
    assert "rig::value<std::uintptr_t>(reader,*pool+0x10+64*8)!=state" in runtime, "only the left hand's EquipChange (state 64)"
    assert "carry_left_equip_code" in policy and "carry_corpse_requester=45" in policy and "Both hands have not been unequipped" in policy and "class CarryLeftRedraw" in policy
    assert "Op::carry_left_redraw" in probe and "carry_left_unequips" in runtime
    assert "const bool left_kept=body_carry && !state->carry.hold_object() &&" in probe
    assert "hand_weapon_bits=body_carry?(left_kept && state->slots[0] && !slot0_spent?1u:0u):" in probe, "the left weapon bit survives a body carry"
    assert "left_power=!power_native_fingers && (!body_carry || left_kept)" in probe and "left_gadget=(!body_carry || left_kept)" in probe
    assert "NativeManualEquipRuntime::left_holds_item.store(state->slots[0] && !slot0_spent" in probe, "the hook keeps only an item the hand holds"
    assert "carry_keeps_left=!fs::exists(root/L\"build\"/L\"carry-locks-hands\")" in probe, "the knob that restores the game's lock"


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    check((root / "src" / "dxgi_probe.cpp").read_text(encoding="utf-8"),
          (root / "src" / "shoulder_carry.hpp").read_text(encoding="utf-8"),
          (root / "src" / "grip_decision.hpp").read_text(encoding="utf-8"))
    print("shoulder_carry_routing_check ok")
    sys.exit(0)
