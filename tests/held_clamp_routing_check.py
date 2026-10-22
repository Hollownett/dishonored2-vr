"""Source wiring of plan B task B4 (the held-object clamp and safe release): the clamp sits between the pose computation and the publication, the layer
is only ever written inside native_held_object.hpp (through the ledger), the forced drop only writes held_use_until, the held sweeps exclude the held entity,
a release check begins before the layer restore can be armed, and the knob and status exist."""
import json
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
probe = (root / "src/dxgi_probe.cpp").read_text(encoding="utf-8")
held = (root / "src/native_held_object.hpp").read_text(encoding="utf-8")
aim = (root / "src/native_interaction_aim.hpp").read_text(encoding="utf-8")
grab = (root / "src/native_grab.hpp").read_text(encoding="utf-8")

def function(source, signature):
    start = source.index(signature)
    depth = 0
    for i in range(source.index("{", start), len(source)):
        depth += source[i] == "{"
        depth -= source[i] == "}"
        if depth == 0:
            return source[start:i + 1]
    raise AssertionError(signature)

publish = function(probe, "static void publish_held_object(")
# the clamp comes after the target pose is computed and before it is published; the published pose is the clamped one
pose = publish.index("origin=from+(seat.origin-from)")
clamp = publish.index("L.clamp.update(in)")
published = publish.index("published=origin;published_pose=")
target = publish.index("d2vr::NativeHeldObject::Target t{")
publication = publish.index("d2vr::NativeHeldObject::publish(t)")
assert pose < clamp < published < target < publication, "clamp must sit after the pose and before the publication"
assert "origin=out.target.position" in publish, "the clamped target replaces the requested position"
assert "post_held_sweeps(side,out.sweeps,*e)" in publish, "the held sweeps exclude the held entity (*e)"
assert "L.clamp.note_published(" in publish
assert "in.hits[i]=d2vr::to_clamp_hit(*raw_hits[i])" in publish and "raw_hits[i]->entity==*e" in publish, "hits go back by proxy index, converted at the clamp boundary"
assert "serial>L.posted_scope" in publish, "an answer counts only after a game-thread scope ran since the post"

# every hold end goes through held_clear (the release check begins before the restore can be armed)
assert probe.count("d2vr::NativeHeldObject::clear(") == 1, "only held_clear calls NativeHeldObject::clear"
body = function(probe, "static void held_clear(")
assert body.index("held_clamp_end()") < body.index("NativeHeldObject::clear("), "begin the release before clear arms the restore"
end = function(probe, "static void held_clamp_end()")
assert "begin_safe_release()" in end
step = function(probe, "static void held_clamp_release_step()")
assert "allow_restore()" in step and "release_depen(" in step
assert step.index("L.safe.update(in)") < step.index("if(out.restore_layer) {finish(")  # allow_restore only via finish, after SafeRelease says so
assert "weapon_serial(L.side,0,d2vr::NativeInteractionAim::producer_release)" in step, "SafeRelease sees an answer only when one arrived, and only its own"
assert "held_hits(L.side,now_ms,d2vr::NativeInteractionAim::producer_release)" in step and "producer_release);" in step, "the release reads only answers tagged release"
assert "weapon_serial(side,0,d2vr::NativeInteractionAim::producer_held)" in publish, "the held clamp reads only answers tagged held"
# a release probe is a real cast: never the zero-length {c,c} construction (the engine does not run casts shorter than 1e-3)
assert "release_probe_sweeps(proxies,pose)" in step and "{c,c," not in step and "post_held_sweeps(L.side,sweeps" not in step, "zero-length release probes"
assert "release_probe_depth(" in step
# the depenetration push names entity, id and handle, uses the HoldObject body mapping (not native_rigid_body), and a new hold cancels it
assert "native_grab.release_depen(L.entity,L.entity_id,body->handle," in step and "exclusion_body(image_base,read_self,L.entity)" in step
begin = function(probe, "static void held_clamp_begin(")
assert "native_grab.cancel_release_depen()" in begin, "a new hold cancels a pending depenetration push"
apply = function(grab, "void apply_release_depen()")
assert "NativeHeldObject::exclusion_body(base_,read_,entity)" in apply and "native_rigid_body(" not in apply, "the HoldObject body is mapped the layer swap's way"
assert "depen_.take(" in apply and "write_velocity(reinterpret_cast<void*>(world),d.handle," in apply
# the slots carry a producer
assert "producer_release" in aim and "weapon_producer_[side][slot]=segments[side][slot].producer" in aim

# layer writes live in native_held_object.hpp only, and the restore goes through the ledger
for name, text in (("dxgi_probe.cpp", probe), ("native_grab.hpp", grab), ("native_interaction_aim.hpp", aim)):
    assert "set_filter(" not in text and "call_filter(" not in text and "restore_layer(" not in text, name + " must not write the layer"
assert "restore_hold_" in held and "static void allow_restore()" in held and "static void begin_safe_release()" in held
assert "if(!restore_hold_.load(std::memory_order_acquire)) layer_restore_.store(true" in function(held, "static void clear(unsigned reason=0)")
assert "ledger_.service(" in held

# the forced drop only writes held_use_until (the game's own Use press)
forced = re.search(r"else if\(holding && entity && held_clamp_drop_request\.exchange\(0\)\) \{[^}]*\}", probe)
assert forced, "forced drop branch missing"
assert "held_use_until=now_h+100" in forced.group(0) and "input." not in forced.group(0)

# no engine call types beyond the existing ones
for rva in ("0xc85860", "0xc90de0"):
    for name, text in (("dxgi_probe.cpp", probe), ("native_held_object.hpp", held), ("native_grab.hpp", grab), ("native_interaction_aim.hpp", aim)):
        assert rva not in text, f"{rva} appears in {name}"

# the depenetration write is the existing velocity setter, validated
depen = function(grab, "void apply_release_depen()")
assert "write_velocity(" in depen and "GetTickCount64()" in depen
assert "release_all()" in depen

# knob, status, launcher row
assert 'held_clamp_enabled=!fs::exists(root/L"build"/L"no-held-clamp");' in probe
assert '\\"held_clamp\\":{' in probe
for key in ("sweeps", "hits", "clamped_frames", "cast_us", "no_bounds", "overlap_on_release", "release_depen_frames", "release_overlap_unresolved",
            "drawn_hand_gap_m", "layer_at_release", "forced_drops"):
    assert f'\\"{key}\\":' in probe, key
features = {f["file"]: f for f in json.loads((root / "tools/default_features.json").read_text(encoding="utf-8"))["features"]}
assert features["no-held-clamp"]["kind"] == "disable" and features["no-held-clamp"]["default"] is True

# the drawn-grip precedence: hand body (not built yet), the A12 grasp anchor, then the clamp's shift, all before publish_hand
drawn = probe.index("drawn_anchor.update(side,seat,hands.grips[side].position")
shift = probe.index("held_clamp_live.applied[side]={}")
hand = probe.index("publish_hand(side,hands.grips[side],raw_grips[side])")
assert drawn < shift < hand
# the released-object height watch reads the held item the way the layer swap finds it (native_rigid_body does not map a HoldObject item)
watch = aim[aim.index("auto watched_z"):aim.index("release_watch_json")]
assert "exclusion_body(" in watch and "native_rigid_body(" not in watch
print("held_clamp_routing_check ok")
