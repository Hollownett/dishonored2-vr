#pragma once
namespace d2vr {
// Which consumers may use this frame's hand-aimed left ray.
//  shots: shots and powers (native shot aim, left aim, blink aim, gameplay trace). They leave the hand only while it is free to aim: not in a
//         sword swing, no head-owned camera, the left grip below .5 and the right trigger below .3 (sword block), the holster and swing
//         timers expired, and a focused, tracked controller. A grip that is the hold of the hand's weapon (grip_holds_weapon: a fist round a
//         grenade or mine, and in dual wield a drawn gun, which is held by squeezing it) is no Use or grab, so it does not count.
//  reticle: the crosshair glyph. It stays visible while the user attacks or grips (as before the shared ray): it only needs a valid hand
//         pose and a view the game itself does not own.
struct RangedAimInputs {
    bool swing_tip{},authored_camera{},authored_view{},focused{},action_synced{},pose_valid{},controller_active{};
    bool holster_clear{},swing_clear{},grip_holds_weapon{};
    float left_grip{},right_trigger{};
};
struct RangedAimGate {bool shots{},reticle{};};
inline RangedAimGate ranged_aim_gate(const RangedAimInputs& in) noexcept {
    RangedAimGate out{};
    out.reticle=!in.authored_view && in.pose_valid;
    out.shots=!in.swing_tip && !in.authored_camera && in.focused && in.action_synced && in.pose_valid && in.controller_active &&
        (in.grip_holds_weapon || in.left_grip<.5f) && in.holster_clear && in.swing_clear && in.right_trigger<.3f;
    return out;
}
} // namespace d2vr
