from pathlib import Path


def check(probe: str, grab: str) -> None:
    # sphere grip (finger_sphere.hpp): a physical hold's ball is NativeGrab's, sized once at the bind; the fingers wrap it in the DRAWN palm's frame
    assert "double hold_sphere(unsigned side)" in grab and "lane.sphere=!sphere_grip_?0." in grab
    assert 'sphere_grip_=!std::filesystem::exists(root/L"build"/L"legacy-finger-grab",ec)' in grab
    assert "seated.position=seated.position+(goal-centre)" in grab and "body->bone<0 && body->bounds && p.palm.valid" in grab  # props only: a body part is not moved to the palm
    assert "lane.grab.bind(seated,*contact,grab_acquire_distance)" in grab
    assert "grab_sphere=native_grab.hold_sphere(side);" in probe
    swap = probe.index("shapes[0]=d2vr::rig::sphere_grip_shape(palm,*geometry,grab_sphere)")
    assert probe.index("palm=drawn_palm_frame(side,hands.grips[side]); if(holder!=side) profile_step(side,held);") < swap < probe.index("d2vr::rig::finger_contact(*geometry,palm,shapes.data()")
    assert "sphere_latch[side].radius_for(held," in probe and '".?AVarkSeveredLimb@@"' in probe and "!legacy_finger_grab" in probe
    assert "&& !(grab_sphere>0);++slot)" in probe  # no probe shape flickers in under the ball
    assert 'fs::exists(root/L"build"/L"legacy-finger-grab")' in probe


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    check((root / "src/dxgi_probe.cpp").read_text(encoding="utf-8"), (root / "src/native_grab.hpp").read_text(encoding="utf-8"))
    print("finger_sphere_routing_check ok")
