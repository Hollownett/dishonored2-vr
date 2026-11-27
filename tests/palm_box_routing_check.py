"""Task 18 (palm box settle) source routing guard for dxgi_probe.cpp: the settle runs only for a free hand, never feeds the clamp, is turned off by
build/legacy-palm-sphere, and decays rather than vanishing. Source-only: not compiled, native-execution or headset proof."""
from pathlib import Path
import json

root = Path(__file__).resolve().parents[1]
probe = (root / "src" / "dxgi_probe.cpp").read_text(encoding="utf-8", errors="replace")


def need(ok: bool, what: str) -> None:
    if not ok:
        raise AssertionError("palm box routing: " + what)


start = probe.index("const d2vr::Vec3 pre_settle=g.position;")
block = probe[start:probe.index("// B14 (DeepSeek P1/P2)", start)]
free = block[block.index("const bool settle_free="):block.index(";", block.index("const bool settle_free="))]
for token in ("palm_box_on", "!armed", "clamp_updated", "clamped.valid", "!clamp_yield", "!met_prop", "!clamped.reacquired",
              "clamped.state!=d2vr::ClampState::released", "probes.valid", "probes.box_valid", "held_clamp_live.holding", "native_grab.holding(side)"):
    need(token in free, "settle_free needs " + token)
need("settle_free && settle_fresh && hand_clamp.held_planes(side,held_now)>0" in block, "settles only while the side holds planes, on a fresh (validated, not held) frame")
need("const bool settle_fresh=clamped.validated && !clamped.held;" in block, "fresh = validated and not held")
need("hand_clamp.settle(side,target.basis,g.position,target.position,probes.box,shape)" in block and "hand_clamp.pressing(side,target.basis,target.position,probes.box,shape)" in block, "settle only while pressing, from the clamp's drawn hand toward the target, with the drawn fingers")
need("finger_turn_last[side]" in block, "the phalanges are posed from the finger IK's last drawn turns")
need("settle_offset_next(settle_offset[side],settle_free,settle_fresh,pressing,want,dt,hand_clamp.config().release_speed," in block, "the offset helper: 0 when not free, kept when not fresh, decay at release_speed when not pressing, capped")
# after the clamp, the meet and the blade; before the clamp is told its hand
need(probe.index("const auto met=native_grab.hand_contact().meet_clamp(") < start < probe.index("hand_clamp.note_drawn(side,clamp_root,composed.noted)"),
     "settle sits after the meet and before note_drawn")
need(start > probe.index("g.position=composed.drawn;"), "settle sits after the blade composition")
# nothing feeds the settled hand back to the clamp
need("hand_clamp.note_drawn(side,clamp_root,composed.noted)" in probe, "the clamp is told composed.noted (pre-settle)")
need("clamp_hold.note(side,clamp_epoch,clamp_root,clamp_updated?pre_settle:g.position)" in probe, "the re-acquire hold keeps the pre-settle hand")
need("palm_box_on=!fs::exists(root/L\"build\"/L\"legacy-palm-sphere\")" in probe, "build/legacy-palm-sphere knob")
need("clamp_probe_set(d2vr::hand_proxies(" in probe and "palm_local,*geometry[side])" in probe, "the probe set carries the palm box")
need('\\"settle\\":[' in probe, "status hand_clamp.settle")
need("overlap_miss || !hit->normal_valid || hit->hand_body || wrong_way" in probe, "free-hand probe answers: a palm/back start inside geometry, no face, own hand body or wrong facing are misses")
need("post_weapon_segment(side,3,palm.position-in*.02,sweep.to,.03)" in probe, "palm cast: the 3 cm tube over the whole palm, started 2 cm behind the palm frame (its front at the palm skin)")
need("const bool overlap_miss=hit->initial_overlap;" in probe, "every free-hand cast that starts inside geometry is a miss (its normal is no face)")
need('\\"probe_rejects\\":[' in probe, "status probe_rejects")
features = {f["id"]: f for f in json.loads((root / "tools" / "default_features.json").read_text())["features"]} \
    if isinstance(json.loads((root / "tools" / "default_features.json").read_text()), dict) else \
    {f["id"]: f for f in json.loads((root / "tools" / "default_features.json").read_text())}
row = features.get("palm-box")
need(row is not None and row["file"] == "legacy-palm-sphere" and row["kind"] == "disable" and row["default"] is True, "launcher row")
print("PASS palm box routing: free-hand gate, no clamp feedback, pre-settle hold, legacy-palm-sphere knob, status, launcher row")
