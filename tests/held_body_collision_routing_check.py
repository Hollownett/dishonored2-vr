from pathlib import Path

# v1030, the user's rule: "object which is hold never collides with the hand and player body". A motor-held prop keeps RIGID_BODY and shoves the player, so a
# physical hold swaps it onto the DEBRIS layer (the engine's own HoldObject rule) through the mod's verified filter writer, and puts its exact word back when
# the hold ends. This pins the wiring: the ledger exists, the dispatcher of the swap runs for both hands every frame (also when the hand lost tracking), the
# knob restores the old behaviour, and the counters reach the live status.
def check(probe: str, header: str) -> None:
    assert '#include "held_body_collision.hpp"' in probe
    assert "static d2vr::HeldBodyCollision held_body_collision;" in probe
    loop = probe.index("const auto held_body=physical_grab_requested?native_grab.held(side):std::nullopt;")
    body = probe[loop:loop + 400]
    assert "held_body->bone<0 && !legacy_held_collision" in body          # corpse limbs keep their own rule
    assert "held_body_collision.hold(side,image_base,read_self,*held_body)" in body
    assert "held_body_collision.release(side,image_base,read_self)" in body  # also when nothing is held: a lost hand restores the word
    assert "legacy_held_collision=fs::exists(root/L\"build\"/L\"legacy-held-collision\")" in probe
    assert '\\"held_collision\\":{\\"swapped\\":' in probe
    # the ledger itself: DEBRIS on the hold, the saved word back on the release, and never a clobber of a changed body
    assert "NativeHeldObject::write_filter(base,read,body.handle,(*filter&~0x1fu)|NativeHeldObject::layer_debris)" in header
    assert "written=NativeHeldObject::write_filter(base,read,e.handle,e.saved);" in header
    assert "*first==e.handle && *second==e.handle && filter && (*filter&0x1fu)==NativeHeldObject::layer_debris" in header


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    check((root / "src/dxgi_probe.cpp").read_text(encoding="utf-8"), (root / "src/held_body_collision.hpp").read_text(encoding="utf-8"))
    print("held_body_collision_routing_check ok")
