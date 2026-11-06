from pathlib import Path


def check(probe: str, grab: str) -> None:
    # sphere grip (finger_sphere.hpp): a physical hold's ball is NativeGrab's, sized once at the bind; the fingers wrap it in the DRAWN palm's frame
    assert "double hold_sphere(unsigned side)" in grab and "lane.sphere=!sphere_grip_?0." in grab
    assert 'sphere_grip_=!std::filesystem::exists(root/L"build"/L"legacy-finger-grab",ec)' in grab
    # v1024 grip ball: a prop is sized by its thin side at the bind; build/legacy-sphere-grip-whole-object keeps v642's ball round the whole box
    assert 'sphere_whole_object_=std::filesystem::exists(root/L"build"/L"legacy-sphere-grip-whole-object",ec)' in grab
    assert "sphere_whole_object_?rig::sphere_grip_radius(body.pose,*body.bounds):rig::sphere_grip_ball_radius(body.pose,*body.bounds)" in grab
    assert "bool sphere_whole_object() const noexcept" in grab and "lane.held?lane.sphere*1000.:0." in grab
    # v1028 flat grip: a wide thin prop (a book, a folder) gets NO shape and a relaxed reach - the hand lies on its face. build/no-grip-slab keeps the ball.
    assert 'grip_slab_=!std::filesystem::exists(root/L"build"/L"no-grip-slab",ec)' in grab
    assert "lane.flat=sphere_grip_ && grip_slab_ && !sphere_whole_object_" in grab and "rig::grip_is_flat(body.pose,*body.bounds)" in grab
    assert "bool hold_flat(unsigned side)" in grab and "lanes_[side].held && lanes_[side].flat" in grab
    assert "native_grab.hold_flat(side)) {--count;shapes[0]={};kinds[0]=nullptr;grab_flat=true;++finger_flat_holds;}" in probe
    assert "grab_flat?d2vr::rig::grip_flat_reach:blending?" in probe   # the relaxed reach, not the hold's closed one
    assert "finger_flat_holds" in probe
    assert "seated.position=seated.position+(goal-centre)" in grab and "body->bone<0 && body->bounds && p.palm.valid" in grab  # props only: a body part is not moved to the palm
    assert "lane.grab.bind(seated,*contact,grab_acquire_distance)" in grab
    assert "grab_sphere=native_grab.hold_sphere(side);" in probe
    swap = probe.index("shapes[0]=d2vr::rig::sphere_grip_shape(palm,*geometry,grab_sphere)")
    assert probe.index("palm=drawn_palm_frame(side,hands.grips[side]); if(holder!=side) profile_step(side,held);") < swap < probe.index("d2vr::rig::finger_contact(*geometry,palm,shapes.data()")
    assert "sphere_latch[side].radius_for(held," in probe and '".?AVarkSeveredLimb@@"' in probe and "!legacy_finger_grab" in probe
    # a native HoldObject limb (dxgi_probe's own latch) gets the same grip ball
    assert "native_grab.sphere_whole_object()?d2vr::rig::sphere_grip_radius(*held_extents):d2vr::rig::sphere_grip_ball_radius(*held_extents)" in probe
    assert "&& !(grab_sphere>0);++slot)" in probe  # no probe shape flickers in under the ball
    assert 'fs::exists(root/L"build"/L"legacy-finger-grab")' in probe


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    check((root / "src/dxgi_probe.cpp").read_text(encoding="utf-8"), (root / "src/native_grab.hpp").read_text(encoding="utf-8"))
    print("finger_sphere_routing_check ok")
